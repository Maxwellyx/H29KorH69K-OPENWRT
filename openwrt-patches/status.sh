#!/bin/bash
#
# 看 openwrt-patches/ 里各补丁的状态。
#
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
TOPDIR="$(cd "$HERE/../../.." && pwd)"
cd "$TOPDIR" || exit 1

for p in "$HERE"/patches/*.patch; do
	[ -f "$p" ] || continue
	n="$(basename "$p")"
	if git apply --check -R "$p" 2>/dev/null; then
		echo "  [已应用]   $n"
	elif git apply --check "$p" 2>/dev/null; then
		echo "  [未应用]   $n"
	else
		echo "  [不一致]   $n  ← 上游变了，或文件被手工改过"
	fi
done

echo
echo "这棵树的改动："
git status --porcelain | grep -v '^?? package/daocaoren/' | sed 's/^/  /'
echo "  （空 = 上游源码干净）"
