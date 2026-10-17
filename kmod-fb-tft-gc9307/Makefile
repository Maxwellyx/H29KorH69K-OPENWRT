#
# GC9307 LCD 控制器的 fbtft fb 驱动（自带源码的内核外模块，不改动 lede 原始代码）
#
# 源码 src/gc9307.c 取自 E:\OPENWRT\资料\DCR\Hinlink\De\gc9307-driver-patch
#   那份补丁原本是把该文件加进内核树 drivers/staging/fbtft/ 并改 Kconfig/Makefile；
#   这里改成内核外编译，只需要能拿到 fbtft 的内部头文件 fbtft.h（src/Makefile 里指过去）。
#
# 命名（2026-10-10 规范化，用户要求）：
#   · 包名/目录名 kmod-fb-tft-gc9307 -> kmod-fb-tft-gc9307
#   · 模块名 fb_gc9307.ko -> gc9307.ko（src/Makefile 的 obj-m、FILES、AUTOLOAD 同步）
#   · DRVNAME "fb_gc9307" -> "gc9307"（只影响 lsmod/驱动的 driver.name、/sys/class/graphics/fbX/name）
#   · ⚠ 千万不要改 src 里的 of_device_id：设备树用 compatible = "sitronix,gc9307" / "gc9307"
#     绑定驱动，改 compatible 才会真的挂不上。DRVNAME 跟绑定无关。
#
include $(TOPDIR)/rules.mk
include $(INCLUDE_DIR)/kernel.mk

PKG_NAME:=kmod-fb-tft-gc9307
PKG_RELEASE:=2
# 注意：本目录/PKG_NAME 用完整包名 kmod-xxx（与生成的 ipk 同名，符合 OpenWrt 习惯）；
#       但下面 KernelPackage/fb-tft-gc9307 里不能带 kmod- 前缀。
#       因为 include/kernel.mk:271 是 $(eval $(call BuildPackage,kmod-$(1)))，
#       ipk 名由内核宏自动加 kmod-；若这里也写 kmod- 就会变成 kmod-kmod-xxx。
#       ipk 名不变 ⇒ tftdisplay 的 DEPENDS(+kmod-xxx) 与 .config 选择都不用改。
PKG_LICENSE:=GPL-2.0
PKG_BUILD_PARALLEL:=1

include $(INCLUDE_DIR)/package.mk

# ---------------------------------------------------------------------------
# GCC 14（openwrt 工具链）把一批原本只是警告的写法升格成了硬错误：
#   -Wincompatible-pointer-types / -Wimplicit-function-declaration / -Wimplicit-int
#   / -Wint-conversion / -Wreturn-mismatch / -Wdeclaration-missing-parameter-type
# 这份代码是 GCC 13（lede）时代写的、真机上跑通了，这里把它们降回警告，
# 保证与设备上已验证的固件行为一致。
# ---------------------------------------------------------------------------
TARGET_CFLAGS += -Wno-error=incompatible-pointer-types -Wno-error=implicit-function-declaration -Wno-error=implicit-int -Wno-error=int-conversion -Wno-error=return-mismatch -Wno-error=declaration-missing-parameter-type

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) ./src/* $(PKG_BUILD_DIR)/
endef

define Build/Compile
	$(KERNEL_MAKE) M="$(PKG_BUILD_DIR)" modules
endef

define KernelPackage/fb-tft-gc9307
  SECTION:=kernel
  SUBMENU:=Video Support
  TITLE:=FB driver for the GC9307 LCD Controller (out-of-tree, module gc9307)
  DEPENDS:=+kmod-fb-tft
  FILES:=$(PKG_BUILD_DIR)/gc9307.ko
  AUTOLOAD:=$(call AutoLoad,09,gc9307)
endef

define KernelPackage/fb-tft-gc9307/description
  FB driver for the GC9307 LCD Controller (172x320).
  Built out-of-tree from src/gc9307.c against the in-kernel fbtft core.
endef

$(eval $(call KernelPackage,fb-tft-gc9307))
