#!/bin/bash
#
# 还原 openwrt-patches/apply.sh 做的事。
#
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
TOPDIR="$(cd "$HERE/../../.." && pwd)"
cd "$TOPDIR" || exit 1

[ -d .git ] || { echo "⚠ $TOPDIR 不是 git 仓库"; exit 1; }

# 倒序还原：同一文件上若有叠加补丁，必须倒着来
reverted=0; skipped=0; failed=0
for p in $(ls -1 "$HERE"/patches/*.patch 2>/dev/null | sort -r); do
	n="$(basename "$p")"
	if git apply --check -R "$p" 2>/dev/null; then
		git apply -R "$p" && { echo "  - 还原 $n"; reverted=$((reverted + 1)); }
	elif git apply --check "$p" 2>/dev/null; then
		echo "  = 本来就没应用，跳过 $n"; skipped=$((skipped + 1))
	else
		echo "  ! 失败（文件被别的东西改过？）: $n"; failed=$((failed + 1))
	fi
done

echo
echo "还原 $reverted，跳过 $skipped，失败 $failed"
echo "还原后这棵树的状态："
git status --porcelain | grep -v '^?? package/daocaoren/' | sed 's/^/  /'
echo "  （空 = 干净）"

exit $failed
