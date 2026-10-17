# SPDX-License-Identifier: GPL-2.0+
#
# Copyright (c) 2023-2025 AnYun
#

include $(TOPDIR)/rules.mk

PKG_NAME:=aic8800
PKG_RELEASE:=2

PKG_SOURCE_PROTO:=git
PKG_SOURCE_URL:=https://github.com/radxa-pkg/aic8800.git
PKG_SOURCE_DATE:=2026-03-30
PKG_SOURCE_VERSION:=89f865b80f5f2ba6c0711c560e1d0191e87a1bf0
PKG_MIRROR_HASH:=skip

PKG_FLAGS:=nonshared

include $(INCLUDE_DIR)/kernel.mk
include $(INCLUDE_DIR)/package.mk

define Package/aic8800-firmware/Default
  SECTION:=firmware
  CATEGORY:=Firmware
  TITLE:=Aicsemi aic8800 $(1) firmware
endef

Package/aic8800-pcie-firmware = $(call Package/aic8800-firmware/Default,PCIe)
Package/aic8800-sdio-firmware = $(call Package/aic8800-firmware/Default,SDIO)
Package/aic8800-usb-firmware = $(call Package/aic8800-firmware/Default,USB)

define KernelPackage/aic8800/Default
  SUBMENU:=Wireless Drivers
  TITLE:=Aicsemi aic8800 Wi-Fi $(1) support
  DEPENDS:=+kmod-cfg80211 +@DRIVER_11N_SUPPORT +@DRIVER_11AC_SUPPORT +@DRIVER_11AX_SUPPORT
endef

define KernelPackage/aic8800-btusb
  SUBMENU:=Bluetooth Support
  TITLE:=Aicsemi aic8800 Bluetooth USB support
  DEPENDS:=+aic8800-usb-firmware +kmod-usb-core +kmod-bluetooth
  FILES:=$(PKG_BUILD_DIR)/src/USB/driver_fw/drivers/aic_btusb/aic_btusb.ko
  AUTOLOAD:=$(call AutoProbe,aic_btusb)
endef

define KernelPackage/aic8800-pcie
  $(call KernelPackage/aic8800/Default,PCIe)
  DEPENDS+= +aic8800-pcie-firmware @PCIE_SUPPORT
  FILES:=$(PKG_BUILD_DIR)/src/PCIE/driver_fw/driver/aic8800/aic8800_fdrv/aic8800D80_fdrv.ko
  AUTOLOAD:=$(call AutoProbe,aic8800D80_fdrv)
endef

define KernelPackage/aic8800-sdio
  $(call KernelPackage/aic8800/Default,SDIO)
  DEPENDS+= +aic8800-sdio-firmware +kmod-mmc +kmod-bluetooth
  FILES:= \
	$(PKG_BUILD_DIR)/src/SDIO/driver_fw/driver/aic8800/aic8800_bsp/aic8800_bsp.ko \
	$(PKG_BUILD_DIR)/src/SDIO/driver_fw/driver/aic8800/aic8800_fdrv/aic8800_fdrv.ko
  # AUTOLOAD 去掉：不在 preinit(kmodloader) 阶段加载（卡住会导致启动死机+看门狗复位）
  # AUTOLOAD:=$(call AutoProbe,aic8800_bsp aic8800_fdrv)
endef

define KernelPackage/aic8800-usb
  $(call KernelPackage/aic8800/Default,USB)
  DEPENDS+= +aic8800-usb-firmware +kmod-usb-core
  FILES:= \
	$(PKG_BUILD_DIR)/src/USB/driver_fw/drivers/aic8800/aic_load_fw/aic_load_fw.ko \
	$(PKG_BUILD_DIR)/src/USB/driver_fw/drivers/aic8800/aic8800_fdrv/aic8800DC_fdrv.ko
  AUTOLOAD:=$(call AutoProbe,aic_load_fw aic8800DC_fdrv)
endef

# 第三方驱动代码对 GCC 14 / 新内核的 -Werror 不友好，这里统一降级为警告
AIC_WERROR_RELAX:= -Wno-error=missing-prototypes -Wno-error=unused-function \
        -Wno-error=unused-variable -Wno-error=unused-but-set-variable \
        -Wno-error=maybe-uninitialized -Wno-error=implicit-function-declaration \
        -Wno-error=int-conversion -Wno-error=incompatible-pointer-types \
        -Wno-error=return-mismatch -Wno-error=declaration-missing-parameter-type \
        -Wno-error=format -Wno-error=array-bounds -Wno-error=stringop-overflow \
        -Wno-error=unused-result -Wno-error=restrict -Wno-error=missing-declarations \
        -Wno-error=expansion-to-defined -Wno-error=attribute-warning \
        -Wno-error=unused-const-variable -Wno-error=address-of-packed-member \
        -Wno-error=flex-array-member-not-at-end -Wno-error=strict-prototypes \
        -Wno-error=old-style-definition -Wno-error=missing-field-initializers \
        -Wno-error=shift-negative-value -Wno-error=overflow -Wno-error=tautological-compare \

# ---------------------------------------------------------------------------
# cfg80211 接口参数兼容开关
#
# openwrt backports <= 6.18.39：
#   add_station/del_station/change_station/get_station/dump_station、
#   add_key/get_key/del_key/set_default_mgmt_key 以及 cfg80211_new_sta/
#   cfg80211_del_sta 的“接口”参数是 struct net_device *
# openwrt backports >= 7.2：
#   全部改成了 struct wireless_dev *（为支持 NAN / MLD）
#
# 驱动若仍按 net_device 解释，netdev_priv() 会解引用错误内存
# ⇒ 内核 oops / 看门狗复位（现象：hostapd 启动即崩、客户端一连上整机重启）。
# 这里直接探测驱动实际编译时使用的那份 cfg80211.h，命中新 API 就打开开关。
# ---------------------------------------------------------------------------
AIC_MAC80211_CFG80211_H := $(STAGING_DIR)/usr/include/mac80211/net/cfg80211.h
ifneq ($(wildcard $(AIC_MAC80211_CFG80211_H)),)
  ifneq ($(shell grep -c 'del_station)(struct wiphy \*wiphy, struct wireless_dev' $(AIC_MAC80211_CFG80211_H) 2>/dev/null),0)
    AIC_CFG80211_COMPAT := -DRWNX_CFG80211_OPS_USE_WDEV=1
    $(info aic8800: cfg80211 使用 wireless_dev 版接口，已启用 RWNX_CFG80211_OPS_USE_WDEV)
  endif
endif

NOSTDINC_FLAGS:= \
	$(KERNEL_NOSTDINC_FLAGS) \
	-I$(PKG_BUILD_DIR) \
	-I$(PKG_BUILD_DIR)/include \
	-I$(STAGING_DIR)/usr/include/mac80211-backport \
	-I$(STAGING_DIR)/usr/include/mac80211-backport/uapi \
	-I$(STAGING_DIR)/usr/include/mac80211 \
	-I$(STAGING_DIR)/usr/include/mac80211/uapi \
	-include backport/autoconf.h \
	-include backport/backport.h \
	-DBUILD_OPENWRT

define Build/Compile

ifneq ($(CONFIG_PACKAGE_kmod-aic8800-btusb),)
	+$(KERNEL_MAKE) $(PKG_JOBS) \
		M="$(PKG_BUILD_DIR)/src/USB/driver_fw/drivers/aic_btusb" \
		NOSTDINC_FLAGS="$(NOSTDINC_FLAGS)" \
		KCFLAGS="$(AIC_WERROR_RELAX) $(AIC_CFG80211_COMPAT)" \
		CONFIG_AIC_FW_PATH="/lib/firmware/aic8800/usb" \
		modules
endif
ifneq ($(CONFIG_PACKAGE_kmod-aic8800-sdio),)
	+$(KERNEL_MAKE) $(PKG_JOBS) \
		M="$(PKG_BUILD_DIR)/src/SDIO/driver_fw/driver/aic8800" \
		NOSTDINC_FLAGS="$(NOSTDINC_FLAGS)" \
		KCFLAGS="$(AIC_WERROR_RELAX) $(AIC_CFG80211_COMPAT)" \
		CONFIG_AIC_FW_PATH="/lib/firmware/aic8800/sdio" \
		CONFIG_AIC8800_BTLPM_SUPPORT=n \
		CONFIG_SDIO_BT=y \
		modules
endif
ifneq ($(CONFIG_PACKAGE_kmod-aic8800-pcie),)
	+$(KERNEL_MAKE) $(PKG_JOBS) \
		M="$(PKG_BUILD_DIR)/src/PCIE/driver_fw/driver/aic8800/aic8800_fdrv" \
		NOSTDINC_FLAGS="$(NOSTDINC_FLAGS)" \
		KCFLAGS="$(AIC_WERROR_RELAX) $(AIC_CFG80211_COMPAT)" \
		CONFIG_AIC_FW_PATH="/lib/firmware/aic8800/pcie" \
		modules
endif
ifneq ($(CONFIG_PACKAGE_kmod-aic8800-usb),)
	+$(KERNEL_MAKE) $(PKG_JOBS) \
		M="$(PKG_BUILD_DIR)/src/USB/driver_fw/drivers/aic8800" \
		NOSTDINC_FLAGS="$(NOSTDINC_FLAGS)" \
		KCFLAGS="$(AIC_WERROR_RELAX) $(AIC_CFG80211_COMPAT)" \
		CONFIG_AIC_FW_PATH="/lib/firmware/aic8800/usb" \
		modules
endif
endef

define Package/aic8800-pcie-firmware/install
	$(INSTALL_DIR) $(1)/lib/firmware/aic8800/pcie
	$(CP) \
		$(PKG_BUILD_DIR)/src/PCIE/driver_fw/fw/aic8800D80/* \
		$(1)/lib/firmware/aic8800/pcie/
endef

define Package/aic8800-sdio-firmware/install
	$(INSTALL_DIR) $(1)/lib/firmware/aic8800/sdio
	$(CP) \
		$(PKG_BUILD_DIR)/src/SDIO/driver_fw/fw/aic8800/* \
		$(1)/lib/firmware/aic8800/sdio/
	$(CP) \
		$(PKG_BUILD_DIR)/src/SDIO/driver_fw/fw/aic8800D80/* \
		$(1)/lib/firmware/aic8800/sdio/
	$(CP) \
		$(PKG_BUILD_DIR)/src/SDIO/driver_fw/fw/aic8800D80N/* \
		$(1)/lib/firmware/aic8800/sdio/
	$(CP) \
		$(PKG_BUILD_DIR)/src/SDIO/driver_fw/fw/aic8800D80X2/* \
		$(1)/lib/firmware/aic8800/sdio/
	$(CP) \
		$(PKG_BUILD_DIR)/src/SDIO/driver_fw/fw/aic8800DC/* \
		$(1)/lib/firmware/aic8800/sdio/
endef

define Package/aic8800-usb-firmware/install
	$(INSTALL_DIR) $(1)/lib/firmware/aic8800/usb
	$(CP) \
		$(PKG_BUILD_DIR)/src/USB/driver_fw/fw/aic8800/* \
		$(1)/lib/firmware/aic8800/usb/
	$(CP) \
		$(PKG_BUILD_DIR)/src/USB/driver_fw/fw/aic8800D80/* \
		$(1)/lib/firmware/aic8800/usb/
	$(CP) \
		$(PKG_BUILD_DIR)/src/USB/driver_fw/fw/aic8800D80N/* \
		$(1)/lib/firmware/aic8800/usb/
	$(CP) \
		$(PKG_BUILD_DIR)/src/USB/driver_fw/fw/aic8800D80X2/* \
		$(1)/lib/firmware/aic8800/usb/
	$(CP) \
		$(PKG_BUILD_DIR)/src/USB/driver_fw/fw/aic8800DC/* \
		$(1)/lib/firmware/aic8800/usb/
	$(CP) \
		$(PKG_BUILD_DIR)/src/USB/driver_fw/fw/aic8800DLN/* \
		$(1)/lib/firmware/aic8800/usb/
endef

$(eval $(call BuildPackage,aic8800-pcie-firmware))
$(eval $(call BuildPackage,aic8800-sdio-firmware))
$(eval $(call BuildPackage,aic8800-usb-firmware))
$(eval $(call KernelPackage,aic8800-btusb))
$(eval $(call KernelPackage,aic8800-pcie))
$(eval $(call KernelPackage,aic8800-sdio))
$(eval $(call KernelPackage,aic8800-usb))
