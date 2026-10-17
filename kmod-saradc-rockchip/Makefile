#
# Rockchip SARADC 驱动（从 LEDE-Develop 移植为独立包）
#
# 原定义在 target/linux/rockchip/modules.mk 里；
# 为了不改动 lede 原始代码，这里作为独立包放在 package/daocaoren/ 下。
#
# 驱动本体是 <内核树>/drivers/iio/adc/rockchip_saradc.c，
# 本包只负责把内核已经编出来的 .ko 打包。
#
include $(TOPDIR)/rules.mk
include $(INCLUDE_DIR)/kernel.mk

PKG_NAME:=kmod-saradc-rockchip
PKG_RELEASE:=1
PKG_LICENSE:=GPL-2.0

include $(INCLUDE_DIR)/package.mk

define Build/Configure
endef

define Build/Compile
endef

define KernelPackage/saradc-rockchip
  SECTION:=kernel
  SUBMENU:=Industrial I/O Modules
  TITLE:=Rockchip SARADC support
  DEPENDS:=@TARGET_rockchip +kmod-industrialio-triggered-buffer
  KCONFIG:= \
	CONFIG_RESET_CONTROLLER=y \
	CONFIG_ROCKCHIP_SARADC
  FILES:= \
	$(LINUX_DIR)/drivers/iio/adc/rockchip_saradc.ko
  AUTOLOAD:=$(call AutoProbe,rockchip_saradc)
endef

define KernelPackage/saradc-rockchip/description
  Support for the SARADC found in SoCs from Rockchip
endef

$(eval $(call KernelPackage,saradc-rockchip))
