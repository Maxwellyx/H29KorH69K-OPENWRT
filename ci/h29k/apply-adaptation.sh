#!/usr/bin/env bash
# Assemble the author's pinned source package on a pinned OpenWrt 6.18 checkout.
# Ubuntu 24.04 / Bash. No downloads, compilation, flashing or offset assumptions.
set -Eeuo pipefail
export LC_ALL=C

OPENWRT_REV=94eb0d7a091f53b3c00182735e32e14b29e796ba
ADAPTATION_REV=8910dc75db7169157565eb6556ba4b33d6489377
MODEM_REV=397e014b4fe4e5c6a5213fcccbf438590f495a05
PROTO_REV=55e682c5e78a5b8447e44315cade04dbca98dbd1
openwrt_dir= adaptation_dir= check_only=0

die() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }
usage() {
  printf '%s\n' 'Usage: bash apply-adaptation.sh --openwrt ABS_DIR --adaptation ABS_DIR [--check-only]'
  printf '%s\n' 'The adaptation checkout must contain both initialized, pinned submodules.'
}
while (($#)); do
  case "$1" in
    --openwrt) (($# >= 2)) || die 'Missing --openwrt value'; openwrt_dir=$2; shift 2 ;;
    --adaptation) (($# >= 2)) || die 'Missing --adaptation value'; adaptation_dir=$2; shift 2 ;;
    --check-only) check_only=1; shift ;;
    --help|-h) usage; exit 0 ;;
    *) die "Unknown argument: $1" ;;
  esac
done
[[ -n "$openwrt_dir" && -n "$adaptation_dir" ]] || { usage; exit 2; }
for command in git python3 tar cmp find grep sha256sum realpath; do
  command -v "$command" >/dev/null || die "Required command missing: $command"
done
[[ "$openwrt_dir" = /* && "$adaptation_dir" = /* ]] || die 'Use absolute Linux paths.'
openwrt_dir=$(realpath -e -- "$openwrt_dir")
adaptation_dir=$(realpath -e -- "$adaptation_dir")
[[ "$openwrt_dir" != "$adaptation_dir" ]] || die 'Source and upstream must be separate directories.'
[[ $(git -C "$openwrt_dir" rev-parse --show-toplevel) = "$openwrt_dir" ]] || die 'OpenWrt must be its own Git checkout.'
[[ $(git -C "$openwrt_dir" rev-parse HEAD) = "$OPENWRT_REV" ]] || die "Expected OpenWrt $OPENWRT_REV. Rebase patches explicitly before changing this pin."
git -C "$adaptation_dir" cat-file -e "$ADAPTATION_REV^{commit}" || die 'Pinned adaptation commit unavailable.'
git -C "$adaptation_dir/modem-pro" cat-file -e "$MODEM_REV^{commit}" || die 'Pinned modem-pro submodule unavailable.'
git -C "$adaptation_dir/luci-proto-modem-pro" cat-file -e "$PROTO_REV^{commit}" || die 'Pinned protocol submodule unavailable.'
grep -Eq '^KERNEL_PATCHVER:=6\.18$' "$openwrt_dir/target/linux/rockchip/Makefile" || die 'Expected Rockchip Linux 6.18.'

# Read only the exact adaptation revision, even when the fork adds CI commits.
# Temporary files live under the runner's TMPDIR and are removed on exit.
stage=$(mktemp -d "${TMPDIR:-/tmp}/h29k-adaptation.XXXXXXXX")
trap '[[ -n "${stage:-}" && -d "$stage" ]] && rm -rf -- "$stage"' EXIT
git -C "$adaptation_dir" archive "$ADAPTATION_REV" | tar -x -C "$stage"
mkdir -p -- "$stage/modem-pro" "$stage/luci-proto-modem-pro"
git -C "$adaptation_dir/modem-pro" archive "$MODEM_REV" | tar -x -C "$stage/modem-pro"
git -C "$adaptation_dir/luci-proto-modem-pro" archive "$PROTO_REV" | tar -x -C "$stage/luci-proto-modem-pro"

# Several patches in the pinned Git objects contain CRLF. Convert patch files
# and shebang scripts to LF in this working copy, without whitespace guessing.
python3 - "$stage" <<'PY'
import pathlib, sys
root = pathlib.Path(sys.argv[1])
for p in root.rglob('*'):
    if not p.is_file() or p.is_symlink():
        continue
    data = p.read_bytes()
    if p.suffix == '.patch' or data.startswith(b'#!'):
        if b'\r\n' in data:
            p.write_bytes(data.replace(b'\r\n', b'\n'))
PY

# Minimal, reviewable integration fix in the staged package only.
# adb-enablemodem only helps Android ADB devices. RM500U uses USB serial/USB net,
# and modem-pro does not invoke adb. Keep its QMI/MBIM/ECM/NCM/RNDIS code intact.
python3 - "$stage/modem-pro/Makefile" <<'PY'
import pathlib, sys
p = pathlib.Path(sys.argv[1])
s = p.read_text()
old = 'DEPENDS:=+kmod-usb-serial-option +kmod-usb-wdm +adb-enablemodem \\\n'
new = 'DEPENDS:=+kmod-usb-serial-option +kmod-usb-wdm \\\n'
if s.count(old) != 1:
    raise SystemExit('Expected modem-pro ADB dependency context changed; refusing to guess.')
p.write_text(s.replace(old, new), newline='\n')
PY

patches=("$stage"/openwrt-patches/patches/*.patch "$stage"/dts/patches/*.patch)
pending_patches=()
for patch_file in "${patches[@]}"; do
  [[ -f "$patch_file" ]] || die "Missing patch: $patch_file"
  if git -C "$openwrt_dir" apply --check -- "$patch_file"; then
    pending_patches+=("$patch_file")
  elif git -C "$openwrt_dir" apply --reverse --check -- "$patch_file" 2>/dev/null; then
    printf 'Already applied: %s\n' "${patch_file#"$stage/"}"
  else
    die "Patch incompatible with checkout: ${patch_file#"$stage/"}. No fuzzy/offset repair is attempted."
  fi
done
# A combined check catches interactions between otherwise independent patches.
if ((${#pending_patches[@]})); then
  git -C "$openwrt_dir" apply --check -- "${pending_patches[@]}"
fi

kernel_dir="$openwrt_dir/target/linux/rockchip/patches-6.18"
[[ -d "$kernel_dir" ]] || die 'Missing upstream patches-6.18 directory.'
disable_paths=()
for archived_patch in "$stage"/dts/kernel-patches-disabled/*.patch; do
  name=${archived_patch##*/}
  rel="target/linux/rockchip/patches-6.18/$name"
  target="$openwrt_dir/$rel"
  if ! git -C "$openwrt_dir" ls-files --error-unmatch -- "$rel" >/dev/null 2>&1; then
    # Four archived HDMI patches are not in this pinned upstream. Absence is
    # expected; never delete an untracked or differently named replacement.
    [[ ! -e "$target" && ! -L "$target" ]] || die "Untracked file occupies disabled patch path: $rel"
    printf 'Absent in pinned upstream, nothing to disable: %s\n' "$rel"
    continue
  fi
  # Verify the working copy equals upstream HEAD before removing a tracked file.
  if [[ -e "$target" ]]; then
    [[ -f "$target" && ! -L "$target" ]] || die "Unexpected file type: $target"
    git -C "$openwrt_dir" show "HEAD:$rel" | cmp -s - "$target" || die "Locally modified disabled patch: $rel"
    disable_paths+=("$target")
  else
    printf 'Already disabled: %s\n' "$rel"
  fi
done
for custom_patch in "$stage"/dts/kernel-patches/*.patch; do
  target="$kernel_dir/${custom_patch##*/}"
  if [[ -e "$target" || -L "$target" ]]; then
    [[ -f "$target" && ! -L "$target" ]] || die "Unexpected custom patch type: $target"
    cmp -s -- "$custom_patch" "$target" || die "Refusing to overwrite differing kernel patch: $target"
  fi
done

package_dir="$openwrt_dir/package/daocaoren"
stamp="$package_dir/.h29k-integration-revision"
if [[ -e "$package_dir" || -L "$package_dir" ]]; then
  [[ -d "$package_dir" && ! -L "$package_dir" && -f "$stamp" ]] || die 'package/daocaoren exists and is not this generated working copy.'
  [[ $(cat "$stamp") = "$ADAPTATION_REV/$MODEM_REV/$PROTO_REV/without-adb-v1" ]] || die 'Existing integration revision differs.'
  # Existing package files must still match the generated staged package.
  while IFS= read -r -d '' source_file; do
    target="$package_dir/${source_file#"$stage/"}"
    [[ -f "$target" ]] && cmp -s -- "$source_file" "$target" || die "Existing staged file differs: $target"
  done < <(find "$stage" -type f -print0)
fi

printf 'Preflight OK: OpenWrt %s; adaptation %s; Linux 6.18.\n' "$OPENWRT_REV" "$ADAPTATION_REV"
printf 'Kernel patch stack still requires target/linux/prepare during the real build.\n'
if ((check_only)); then
  printf 'Check only: no OpenWrt or source files were modified.\n'
  exit 0
fi

# All user-space patch and overwrite checks finish before touching the checkout.
if ((${#pending_patches[@]})); then
  git -C "$openwrt_dir" apply --check -- "${pending_patches[@]}"
  git -C "$openwrt_dir" apply -- "${pending_patches[@]}"
fi
for target in "${disable_paths[@]}"; do
  rm -- "$target"
done
for custom_patch in "$stage"/dts/kernel-patches/*.patch; do
  cp -- "$custom_patch" "$kernel_dir/${custom_patch##*/}"
done
if [[ ! -e "$package_dir" ]]; then
  mkdir -- "$package_dir"
  tar -C "$stage" -cf - . | tar -x -C "$package_dir"
  printf '%s\n' "$ADAPTATION_REV/$MODEM_REV/$PROTO_REV/without-adb-v1" > "$stamp"
fi

manifest="$openwrt_dir/h29k-integration.txt"
{
  printf 'OpenWrt: %s\nAdaptation: %s\nModem: %s\nProtocol: %s\n' "$OPENWRT_REV" "$ADAPTATION_REV" "$MODEM_REV" "$PROTO_REV"
  printf 'Integration fix: removed only +adb-enablemodem from the staged modem-pro Makefile.\n'
  printf 'Integration fix: CRLF normalized to LF for staged patches and shebang scripts.\n'
  printf 'Target: H29K Pro 2025 no battery + RM500U (board compatibility not hardware tested).\n'
  printf 'Required for EVERY make invocation: DEVTOOLS_SUFFIX=M\n'
  printf 'The kernel patch stack is installed, not yet compiled or validated on hardware.\n'
  printf '\nInstalled kernel patches:\n'
  sha256sum "$stage"/dts/kernel-patches/*.patch
} > "$manifest"
printf 'Adaptation installed. Record: %s\n' "$manifest"
printf 'Next: install pinned feeds, copy h29k.seed to .config, make DEVTOOLS_SUFFIX=M defconfig.\n'
