#!/bin/bash
#
# 应用"必须改上游 OpenWrt 文件"的补丁。幂等：已应用的自动跳过。
#
# 和设备树无关的改动放这里；设备树/设备定义在 ../dts/。
# 两套都用 git apply，可反向还原，上游源码始终能 git checkout 回干净状态。
#
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
TOPDIR="$(cd "$HERE/../../.." && pwd)"
cd "$TOPDIR" || exit 1

[ -d .git ] || { echo "⚠ $TOPDIR 不是 git 仓库，不能用 git apply"; exit 1; }

applied=0; skipped=0; failed=0
for p in "$HERE"/patches/*.patch; do
	[ -f "$p" ] || continue
	n="$(basename "$p")"
	if git apply --check "$p" 2>/dev/null; then
		git apply "$p" && { echo "  + 应用 $n"; applied=$((applied + 1)); }
	elif git apply --check -R "$p" 2>/dev/null; then
		echo "  = 已应用，跳过 $n"; skipped=$((skipped + 1))
	else
		echo "  ! 失败（上游可能变了，需要重新生成补丁）: $n"; failed=$((failed + 1))
	fi
done

echo
echo "应用 $applied，跳过 $skipped，失败 $failed"
echo
echo "当前这棵树里被改动的文件："
git status --porcelain | grep -v '^?? package/daocaoren/' | sed 's/^/  /'

exit $failed
