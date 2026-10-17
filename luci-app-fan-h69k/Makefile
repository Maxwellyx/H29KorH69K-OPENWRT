#
# luci-app-fan-h69k —— HINLINK OPC-H69K 的 PWM 风扇控制（Lua LuCI 应用）
#
# 注意：这里刻意【不】用 feeds/luci/luci.mk，因为本包只装 files/*（纯脚本 + Lua），
#       没有 po 目录参与 luci.mk 的编译流程，用 package.mk 更简单可控。
#
include $(TOPDIR)/rules.mk

PKG_NAME:=luci-app-fan-h69k
PKG_VERSION:=1.0
PKG_RELEASE:=3

PKG_MAINTAINER:=Daocaoren <168620188@qq.com>
PKG_LICENSE:=GPL-3.0

LUCI_TITLE:=LuCI App for HINLINK H69K PWM fan control
LUCI_DESCRIPTION:=PWM fan control for HINLINK OPC-H69K based on CPU temperature.
LUCI_DEPENDS:=+luci-base +luci-compat +luci-lua-runtime +kmod-hwmon-pwmfan +kmod-hwmon-gpiofan @TARGET_rockchip_armv8_DEVICE_hinlink_opc-h69k

PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)

include $(INCLUDE_DIR)/package.mk

define Package/$(PKG_NAME)
	SECTION:=luci
	CATEGORY:=LuCI
	SUBMENU:=3. Applications
	TITLE:=$(LUCI_TITLE)
	PKGARCH:=all
	DEPENDS:=$(LUCI_DEPENDS)
endef

define Package/$(PKG_NAME)/description
	$(LUCI_DESCRIPTION)
endef

define Build/Compile
endef

define Package/$(PKG_NAME)/install
	$(CP) ./files/* $(1)/
endef

define Package/$(PKG_NAME)/postinst
#!/bin/sh
[ -n "$${IPKG_INSTROOT}" ] || {
	/etc/init.d/hinlinkfaninit enable 2>/dev/null
}
exit 0
endef

$(eval $(call BuildPackage,$(PKG_NAME)))
