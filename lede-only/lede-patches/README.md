# lede-patches —— lede 侧改动的补丁

**lede 源码一个字节都不改**：所有针对 lede 现有文件的改动都放在
`patches/*.patch` 里，用 `git apply` 施加，随时 `./revert.sh` 还原。
停用的补丁丢进 `patches-disabled/`（不会被应用，但留着随时能恢复）。

跟 `../dts/` 的分工：

| 目录 | 管什么 |
|---|---|
| `../dts/` | 设备树（`target/linux/rockchip/files/.../*.dts`）——改完还要把 dts 同步进内核构建树，所以单独一套 |
| `这里` | 其它 lede 侧改动（包、镜像脚本、启动脚本……） |

## 命令

```sh
./status.sh   # 看每个补丁已应用 / 未应用
./apply.sh    # 应用（幂等，已应用的自动跳过）
./revert.sh   # 还原
```

`~/build-fw.sh` 每次编译前会自动跑 `../dts/apply.sh` 和本目录的 `apply.sh`。

## 补丁清单

### `001-lean-default-settings.patch`

改 `package/lean/default-settings`，两处：

1. **`Makefile` 的 `DEPENDS`**
   原来是 `+luci-compat +@LUCI_LANG_zh-cn +@LUCI_LANG_zh_Hans +@LUCI_LANG_en`。
   `LUCI_LANG_zh-cn` 和 `LUCI_LANG_en` **这两个 kconfig 符号根本不存在**：
   `feeds/luci/luci.mk` 里 `LUCI_LANGUAGES` 把 `zh-cn` 映射成 `zh_Hans`，
   语言列表由各包 `po/` 目录决定，里面没有 `en`。
   两个 `@` 依赖永远不成立 ⇒ 整包被隐藏 ⇒ `CONFIG_PACKAGE_default-settings`
   一直是 `not set`（跟"和别的包冲突"无关）。
   改成 `+luci-compat +@LUCI_LANG_zh_Hans` 后就能正常选中。

2. **`files/zzz-default-settings`**
   - 新增：**主机名按设备树 model 自动设定**（读 `/proc/device-tree/model`，
     不依赖 ubus）：`OPC-H69K`→`H69K`、`OPC-H29K`→`H29K`、`OPC-HT2`→`HT2`、
     `F50-PRO`→`F50-PRO`、其它→`Hinlink`
   - 新增：F50-PRO 的 WAN(eth1)/wan6 接口 + fw4 的 DNS 重定向 / NAT 规则
   - 新增：无线默认开启（删掉 `option disabled`）
   - 删除：把 opkg/apk 源改写成 `mirrors.tencent.com/lede` 的 6 行。
     原版还会把 `openwrt_luci` 源改成 `releases/18.06.9` —— 那是 LEDE 18.06
     的老路径，在 24.10.5 树上必然 404，会把设备的在线源写坏。
