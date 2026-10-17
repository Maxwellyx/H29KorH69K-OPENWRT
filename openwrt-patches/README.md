# openwrt-patches —— 必须改上游 OpenWrt 文件的补丁

和设备树无关、但又非改不可的改动放这里。设备树 + 设备定义在 `../dts/`。

```bash
~/openwrt/package/daocaoren/openwrt-patches/status.sh
~/openwrt/package/daocaoren/openwrt-patches/apply.sh     # 幂等
~/openwrt/package/daocaoren/openwrt-patches/revert.sh
```

两个补丁，都改的是**上游自己的文件**，都能一键还原。

---

## `001-kmod-fbdev-allow-rockchip.patch`

改 `package/kernel/linux/modules/video.mk`，**两处**（同一文件必须合成一个补丁）：

```diff
-FBDEV_TARGETS:=@(TARGET_bcm27xx||TARGET_sunxi||TARGET_x86_legacy||TARGET_x86_geode)
+FBDEV_TARGETS:=@(TARGET_bcm27xx||TARGET_sunxi||TARGET_x86_legacy||TARGET_x86_geode||TARGET_rockchip)

 define KernelPackage/fb-tft
   DEPENDS:= \
-    @GPIO_SUPPORT @TARGET_bcm27xx +kmod-backlight \
+    @GPIO_SUPPORT @(TARGET_bcm27xx||TARGET_rockchip) +kmod-backlight \
     +kmod-fb +kmod-fb-sys-fops +kmod-fb-sys-ram +kmod-spi-bitbang
```

**为什么要改**：上游把整个 fbdev（framebuffer）模块组**只开放给 4 个目标**
（树莓派 / sunxi / x86）。而 H29K 的屏幕驱动链是

```
tft-display → kmod-fb-tft-gc9307 → kmod-fb-tft → kmod-fb   ← 全被 FBDEV_TARGETS 挡住
```

于是 rockchip 上这串包在 menuconfig / `make defconfig` 里**全部不可见**，表现是
`.config` 里只剩 `CONFIG_DEFAULT_tft-display=y`，`CONFIG_PACKAGE_tft-display` 死活选不上。

实测证据：

- 手动往 `.config` 写 `CONFIG_PACKAGE_tft-display=y` 再 `make defconfig`，会被 kconfig 直接丢掉
- 同一次试验里 `kmod-spi-bitbang` / `kmod-spi-dev` **能站住**（它们没有这个限制）
  ⇒ 证明不是"配置写错"，而是符号根本不可见
- 生成的 `tmp/.config-package.in` 里明确写着
  `depends on (TARGET_bcm27xx||TARGET_sunxi||TARGET_x86_legacy||TARGET_x86_geode)`

`FBDEV_TARGETS` 定义在 `video.mk:11`，被 `KernelPackage/fb`（第 118 行）使用；
`fb-sys-fops` / `fb-sys-ram` 通过 `+kmod-fb` **传递继承**这个限制。
而 `KernelPackage/fb-tft`（第 263 行）**没用 `$(FBDEV_TARGETS)`**，它自己写死了
`@TARGET_bcm27xx` —— 所以**必须改两处**，只改一处不够（我第一版就只改了 `fb-tft`，
结果一点用没有）。

**lede 那边没有这个限制**（对比 `~/lede/package/kernel/linux/modules/video.mk`：
`KernelPackage/fb` 用的是 `DEPENDS:=@DISPLAY_SUPPORT`），所以 lede 上能编。

改法是**保守**的：只把 `TARGET_rockchip` 加进白名单，不是把限制整个删掉。

### 相关：`video-support` / `audio-support` 这类"功能门"必须显式选中

`KernelPackage/fb` 的 `DEPENDS` 里有 **`video-support`**（没带 `+`，是硬依赖
⇒ Kconfig 里变成 `depends on PACKAGE_video-support`）。而 `video-support` 自己的默认值是
`default m if ALL||ALL_KMODS`，**正常编译时没人选它**。

所以光打上面的补丁还不够 —— `.config` 里没有 `CONFIG_PACKAGE_video-support=y` 的话，
`kmod-fb` 依旧不可见（第二个坑，我在这上面又兜了一圈）。

上游其它目标是在 target 的 `DEFAULT_PACKAGES` 里加它：

```
target/linux/x86/Makefile:23:        DEFAULT_PACKAGES += video-support
target/linux/bcm27xx/Makefile:28:    DEFAULT_PACKAGES += video-support
target/linux/sunxi/Makefile:30:      DEFAULT_PACKAGES += video-support
...
```

我们**不改 `target/linux/rockchip/Makefile`**，而是放进设备定义的 `DEVICE_PACKAGES`，
这样只影响我们这两块板子。

**同一类坑在 H69K 上还有一次**：`kmod-drm-rockchip` 会因为
`select PACKAGE_kmod-drm-dw-hdmi-qp`（HDMI 音频）而继承
`kmod-sound-core`（来自 `sound.mk:83` 的 `AddDepends/sound` 宏），而
`kmod-sound-core` 自己 `depends on PACKAGE_audio-support` ⇒ 所以 H69K 的设备定义里
必须有 `audio-support` + `kmod-sound-core` 两个，否则整条 DRM 链不可见。

---

## `002-uboot-add-generic-rk35xx.patch`

改 `package/boot/uboot-rockchip/Makefile`，**四处**（同一文件，合成一个补丁）：

```diff
 define U-Boot/rk3528/Default
   ...
 endef
 
+define U-Boot/generic-rk3528
+  $(U-Boot/rk3528/Default)
+  NAME:=Generic RK3528 board
+  BUILD_DEVICES:= \
+    hinlink_opc-h29k
+endef
+
 define U-Boot/hinlink-h28k-rk3528

 ...（rk3568 段同理，加 U-Boot/generic-rk3568 → hinlink_opc-h69k）...

 UBOOT_TARGETS := \
   ...
   rock-pi-e-v3-rk3328 \
+  generic-rk3528 \
   hinlink-h28k-rk3528 \
   ...
   fastrhino-r66s-rk3568 \
+  generic-rk3568 \
   hinlink-h66k-rk3568 \
```

**为什么要改**：两个设备的 `UBOOT_DEVICE_NAME` 分别是 `generic-rk3528` / `generic-rk3568`，
镜像组装会去 `$(STAGING_DIR_IMAGE)/<名字>-u-boot-rockchip.bin` 取 u-boot
（`target/linux/rockchip/image/Makefile:43` 的 `dd ... seek=64`）。
上游的 rk3528/rk3568 设备里**都没有 generic**，不补就没法产出那两个文件。

**为什么用 generic**（实证，详见 `../dts/README.md`）：

| | `generic-rk3528` | `generic-rk3568` |
|---|---|---|
| u-boot 2026.07 里有 defconfig | ✔（1128 字节） | ✔（1837 字节） |
| 编译产物 `u-boot-rockchip.bin` | ✔ 9196544 字节 | ✔ 同机制 |
| lede 那边实际在用的 | ✔ `generic-rk3528-idbloader.img` + `-u-boot.itb` | ✔ 同 |
| 相比借 h28k/h66k 的优势 | 无需依赖上游的 u-boot 板级补丁，mainline 原装 | 同 |

`BUILD_DEVICES := hinlink_opc-h29k` / `hinlink_opc-h69k` 的作用是让 u-boot 包在选中
对应设备时自动为 y（`include/u-boot.mk:106-109`）。实测两个设备各自都带上：

```
CONFIG_PACKAGE_u-boot-generic-rk3528=y + CONFIG_PACKAGE_trusted-firmware-a-rk3528=y
CONFIG_PACKAGE_u-boot-generic-rk3568=y + CONFIG_PACKAGE_trusted-firmware-a-rk3568=y
```

⚠️ 上游的 ATF/TPL 版本比 lede 新（rk3528: v1.20/v1.11 vs lede 的 v1.17/v1.10），
**DDR 起不来的话这是第一个要试的变量**。

---

## 加新补丁的写法

```bash
cd ~/openwrt
cp <要改的文件> /tmp/new
# ... 改 /tmp/new ...
diff -u --label a/<路径> --label b/<路径> <原文件> /tmp/new \
  > package/daocaoren/openwrt-patches/patches/00N-描述.patch
```

**用 `diff -u` 生成、不要手写补丁**。手写时 hunk 头的行数极易数错 —— 踩过：
写成 `@@ -205,8 +205,23 @@`（实际应为 `+205,22`），`git apply` 报
`error: corrupt patch at line 26`。

**同一文件的多处改动必须合成一个补丁** —— lede 那边踩过：叠加两个补丁会让幂等判定崩掉
（正向反向 `git apply --check` 都不成立 ⇒ 报"失败" ⇒ `exit 1` ⇒ 编译脚本 `set -e` 直接中止）。
