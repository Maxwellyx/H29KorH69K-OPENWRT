# lede-only —— 只对 coolsnowwolf/lede 有意义的东西

这个目录里的内容**在这棵 OpenWrt 上游树上不参与编译**，搬过来只是留个备案，
方便和 lede 那边的做法对照。原样保留，别删。

## `dts-patches/`

lede 那份设备树补丁集（`patches/` + `patches-disabled/`）。

**为什么在上游用不了**：它们的目标文件是

```
target/linux/rockchip/files/arch/arm64/boot/dts/rockchip/rk3528-opc-h29k.dts
```

`target/linux/rockchip/files/` 这个目录**上游根本没有** —— lede 把设备树放在那里，
上游 OpenWrt 则把设备树以内核补丁的形式放在
`target/linux/rockchip/patches-6.18/`（例如 `101-arm64-dts-rockchip-Add-HINLINK-H28K.patch`）。
路径都对不上，补丁必然打不上。

上游的做法见 `../dts/`（已按上游布局重做）。

## `lede-patches/`

lede 那份 `001-lean-default-settings.patch`，改的是 `package/lean/default-settings`。

**为什么在上游用不了**：`package/lean/` 是 coolsnowwolf/lede 专有的目录，上游没有。

其中的**我们自己的改动**已经搬到独立包 `../hinlink-settings/`（不改上游一行）。
剩下那些是 lede 发行版自己的品牌行为（`services`→`nas` 菜单改名、root 密码固定为
`password`、`DISTRIB_DESCRIPTION` 写死 `LEDE`、关掉 dnsmasq 日志、改 opkg 源到腾讯镜像……），
**上游 OpenWrt 不该继承**，没有搬。

## 对照表

| lede 的做法 | 上游 openwrt 的做法 |
|---|---|
| `dts/patches/001-....patch` 改 `target/linux/rockchip/files/.../rk3528-opc-h29k.dts` | `dts/kernel-patches/102-...patch`（新增内核 dts）+ `dts/patches/001-...patch`（改 `image/armv8.mk`） |
| `lede-patches/001-lean-default-settings.patch` 改 `package/lean/default-settings` | 独立包 `hinlink-settings` |
| `keys/key-build*`（usign，给 opkg） | 追加 `keys/private-key.pem` + `public-key.pem`（EC P-256，给 apk） |
| `~/build-fw.sh` | 见 `../README-openwrt.md` |
