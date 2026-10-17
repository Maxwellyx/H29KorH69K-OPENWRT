#
# Copyright (C) 2020 Nate Ding
#
# This is free software, licensed under the GNU General Public License v3.
# See /LICENSE for more information.
#
# 【OpenWrt 移植说明】
#   1) 原版 `LUCI_PKGARCH:=$(if $(realpath src/Makefile),,all)` 是多余的：
#      feeds/luci/luci.mk 第 12 行已经有同样的默认值
#      `LUCI_PKGARCH?=$(if $(realpath src/Makefile),,all)`，
#      而且 luci.mk 里用的是 ${CURDIR}/src/Makefile（路径基准正确），原版那句
#      的 realpath 基准是调用方 cwd，反而可能判断错。已删掉。
#   2) 原版 `PKG_LINCESE_FILES` 拼错了（少个 s），实际叫 PKG_LICENSE_FILES。
#   3) 加了 +luci-compat：这是 Lua 版 CBI/controller，openwrt 现在的 LuCI 需要它。
#
include $(TOPDIR)/rules.mk

LUCI_Title:=LuCI support for ssd1306 0.91' 128x32 display
LUCI_DEPENDS:=+luci-base +luci-compat +libconfig @TARGET_rockchip_armv8_DEVICE_hinlink_opc-h69k

PKG_VERSION:=20230823
PKG_RELEASE:=1

PKG_LICENSE:=GPL-3.0
PKG_LICENSE_FILES:=LICENSE
PKG_MAINTAINER:=natelol <natelol@github.com>

define Package/luci-app-oled/conffiles
/etc/config/oled
endef

include $(TOPDIR)/feeds/luci/luci.mk

# GCC 14（openwrt）把一批老写法升格成硬错误（函数指针不匹配 / 隐式函数声明 …）；
# 这份代码是 GCC 13 时代写的，这里降回警告，保持与真机已验证固件一致。
TARGET_CFLAGS += -Wno-error=incompatible-pointer-types -Wno-error=implicit-function-declaration -Wno-error=implicit-int -Wno-error=int-conversion -Wno-error=return-mismatch -Wno-error=declaration-missing-parameter-type

# call BuildPackage - OpenWrt buildroot signature
