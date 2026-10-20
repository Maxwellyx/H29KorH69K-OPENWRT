#!/usr/bin/env bash
set -euo pipefail
umask 022

HERE="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ADAPTATION_DIR="$(cd -- "$HERE/../.." && pwd)"
: "${OPENWRT_DIR:?Set OPENWRT_DIR to an empty Linux build directory}"
: "${BUILD_LOG_DIR:?Set BUILD_LOG_DIR}"
: "${FIRMWARE_OUTPUT_DIR:?Set FIRMWARE_OUTPUT_DIR}"
export DEVTOOLS_SUFFIX=M
mkdir -p -- "$BUILD_LOG_DIR" "$FIRMWARE_OUTPUT_DIR"

run_make() {
  make -C "$OPENWRT_DIR" DEVTOOLS_SUFFIX=M "$@"
}

prepare() {
  [[ ! -e "$OPENWRT_DIR" ]] || { echo "Build directory already exists: $OPENWRT_DIR" >&2; exit 1; }
  mkdir -p -- "$OPENWRT_DIR"
  local url sha
  url="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["openwrt"]["url"])' "$HERE/source-lock.json")"
  sha="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["openwrt"]["sha"])' "$HERE/source-lock.json")"
  git -C "$OPENWRT_DIR" init
  git -C "$OPENWRT_DIR" remote add origin "$url"
  git -C "$OPENWRT_DIR" fetch --depth=1 origin "$sha"
  git -C "$OPENWRT_DIR" -c advice.detachedHead=false checkout --detach FETCH_HEAD
  [[ "$(git -C "$OPENWRT_DIR" rev-parse HEAD)" == "$sha" ]]
  python3 - "$HERE/source-lock.json" "$OPENWRT_DIR/feeds.conf" <<'PY'
import json, sys
lock = json.load(open(sys.argv[1], encoding="utf-8"))
with open(sys.argv[2], "w", encoding="utf-8", newline="\n") as out:
    for name, item in lock["feeds"].items():
        out.write(f'src-git {name} {item["url"]}^{item["sha"]}\n')
PY
  bash "$HERE/apply-adaptation.sh" --openwrt "$OPENWRT_DIR" --adaptation "$ADAPTATION_DIR"
  (cd "$OPENWRT_DIR" && ./scripts/feeds update -a && ./scripts/feeds install -a)
  {
    echo "Adaptation commit: $(git -C "$ADAPTATION_DIR" rev-parse HEAD)"
    git -C "$ADAPTATION_DIR" submodule status --recursive
    echo "OpenWrt commit: $(git -C "$OPENWRT_DIR" rev-parse HEAD)"
    cat "$OPENWRT_DIR/feeds.conf"
    git -C "$OPENWRT_DIR" diff --stat
  } > "$BUILD_LOG_DIR/source-versions.txt"
  cp -- "$HERE/source-lock.json" "$BUILD_LOG_DIR/source-lock.json"
}

configure() {
  cp -- "$HERE/h29k.seed" "$OPENWRT_DIR/.config"
  run_make defconfig
  cp -- "$OPENWRT_DIR/.config" "$BUILD_LOG_DIR/resolved.config"
  local symbol
  for symbol in \
    TARGET_rockchip_armv8_DEVICE_hinlink_opc-h29k \
    PACKAGE_luci PACKAGE_luci-app-modemd-pro PACKAGE_modemd-pro \
    PACKAGE_luci-proto-modem-pro PACKAGE_kmod-aic8800-sdio \
    PACKAGE_kmod-usb-serial-option PACKAGE_kmod-usb-net-cdc-ether \
    PACKAGE_kmod-usb-net-cdc-ncm PACKAGE_kmod-usb-net-rndis \
    PACKAGE_video-support PACKAGE_tft-display; do
    grep -Fxq "CONFIG_${symbol}=y" "$OPENWRT_DIR/.config" || {
      echo "Required build option was dropped: CONFIG_${symbol}=y" >&2
      exit 1
    }
  done
  (cd "$OPENWRT_DIR" && ./scripts/diffconfig.sh) > "$BUILD_LOG_DIR/diffconfig.txt"
}

download() {
  run_make download -j8 V=s
}

compile() {
  local jobs="${BUILD_JOBS:-2}"
  if ! run_make -j"$jobs" V=s; then
    echo "Parallel build failed; retrying once with one job to retain a clear failure log."
    run_make -j1 V=s
  fi
}

package() {
  local target_dir="$OPENWRT_DIR/bin/targets/rockchip/armv8"
  local -a images
  shopt -s nullglob
  images=("$target_dir"/*hinlink_opc-h29k-squashfs-sysupgrade.img.gz)
  [[ ${#images[@]} -eq 1 ]] || { echo "Expected exactly one H29K squashfs image" >&2; exit 1; }
  cp -- "${images[0]}" "$FIRMWARE_OUTPUT_DIR/"
  local image_name
  image_name="$(basename -- "${images[0]}")"
  local fwtool="$OPENWRT_DIR/staging_dir/host/bin/fwtool"
  [[ -x "$fwtool" ]] || { echo "Built fwtool is missing" >&2; exit 1; }
  "$fwtool" -i "$FIRMWARE_OUTPUT_DIR/image-metadata.json" "$FIRMWARE_OUTPUT_DIR/$image_name"
  python3 "$HERE/verify-image.py" \
    "$FIRMWARE_OUTPUT_DIR/$image_name" "$FIRMWARE_OUTPUT_DIR/image-metadata.json" \
    > "$FIRMWARE_OUTPUT_DIR/image-verification.json"
  local info
  for info in config.buildinfo feeds.buildinfo version.buildinfo profiles.json; do
    [[ ! -f "$target_dir/$info" ]] || cp -- "$target_dir/$info" "$FIRMWARE_OUTPUT_DIR/"
  done
  for info in "$target_dir"/*hinlink_opc-h29k*.manifest; do
    cp -- "$info" "$FIRMWARE_OUTPUT_DIR/"
  done
  cp -- "$BUILD_LOG_DIR/source-versions.txt" "$FIRMWARE_OUTPUT_DIR/"
  cp -- "$BUILD_LOG_DIR/resolved.config" "$FIRMWARE_OUTPUT_DIR/"
  cp -- "$HERE/source-lock.json" "$FIRMWARE_OUTPUT_DIR/"
  cp -- "$HERE/FIRMWARE-NOTES.txt" "$FIRMWARE_OUTPUT_DIR/"
  (cd "$FIRMWARE_OUTPUT_DIR" && find . -maxdepth 1 -type f ! -name SHA256SUMS -print0 | sort -z | xargs -0 sha256sum > SHA256SUMS)
  if [[ -n "${GITHUB_STEP_SUMMARY:-}" ]]; then
    {
      echo '### H29K firmware built'
      echo
      echo "Image: \`$image_name\`"
      echo
      echo 'Download the H29K-RM500U artifact above. It includes SHA256SUMS, the resolved configuration and source versions.'
      echo
      echo 'Build and disk-image checks passed; boot, screen and cellular behavior still require testing on the actual H29K Pro 2025 board.'
    } >> "$GITHUB_STEP_SUMMARY"
  fi
}

step="${1:-}"
case "$step" in
  prepare|configure|download|compile|package)
    "$step" 2>&1 | tee "$BUILD_LOG_DIR/$step.log"
    ;;
  *) echo "Usage: bash build.sh {prepare|configure|download|compile|package}" >&2; exit 2 ;;
esac
