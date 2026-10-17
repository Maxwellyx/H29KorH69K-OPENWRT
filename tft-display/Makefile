
include $(TOPDIR)/rules.mk

# 包名 tft-display（2026-10-10 规范化，用户要求）。
#   ⚠ 仅「包名/目录名」变了；包内部一律保持 tftdisplay 不动，避免破坏设备上已有配置：
#     /usr/bin/tftdisplay、/etc/init.d/tftdisplay、UCI /etc/config/tftdisplay、
#     /etc/tftdisplay.{idle,brightness,battery,lan} 都还是原名。
PKG_NAME:=tft-display
PKG_VERSION:=2.19
PKG_RELEASE:=32
PKG_SOURCE_PROTO:=git
PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)
PKG_LICENSE:=GPL
PKG_LICENSE_FILES:=LICENSE
PKG_MAINTAINER:=Fish <168620188@qq.com>

include $(INCLUDE_DIR)/package.mk

define Package/$(PKG_NAME)
  SECTION:=utils
  CATEGORY:=Utilities
  SUBMENU:=3. Applications
  TITLE:=LVGL System Information Display
  DEPENDS:=+kmod-spi-bitbang \
   +kmod-spi-dev \
   +kmod-fb-tft-gc9307 \
   +kmod-input-touchscreen-axs5106 \
   +kmod-backlight-pwm \
   +kmod-saradc-rockchip
endef

define Package/$(PKG_NAME)/description
A system information display using LVGL for framebuffer on OpenWrt.
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	cp -r ./src $(PKG_BUILD_DIR)/
endef

# ⚠️ CFLAGS / LDFLAGS 用**环境变量**方式传（`VAR=... $(MAKE) ...`），不要写成 `$(MAKE) ... CFLAGS=...`：
#    命令行变量会让子 Makefile 里所有 `CFLAGS +=` 全部失效（GNU make 的规定）。
#    src/Makefile 的 include 路径（-Ilvgl 等）就是由 lvgl/lvgl.mk 里的 `CFLAGS +=` 加的，
#    一旦失效就报 `fatal error: lvgl/lvgl.h: No such file or directory`。
# -Wno-error=incompatible-pointer-types：
#   openwrt 这棵树用 GCC 14，它把「函数指针类型不匹配」从警告升格成错误；
#   这份代码是 GCC 13（lede）时代的，那边只报警告、真机上跑通了。
#   已知并已修正的一处：main.c 的 load_complete_cb（LVGL 单参数回调）。
#   这个开关给其余同类写法兜底，保证行为与设备上已验证的固件一致。
define Build/Compile
	CC="$(TARGET_CC)" CFLAGS="$(TARGET_CFLAGS) -Wno-error=incompatible-pointer-types -Wno-error=implicit-function-declaration -Wno-error=implicit-int -Wno-error=int-conversion -Wno-error=return-mismatch -Wno-error=declaration-missing-parameter-type" LDFLAGS="$(TARGET_LDFLAGS) -lm" \
	TFT_VERSION="$(PKG_VERSION)-$(PKG_RELEASE)" \
	$(MAKE) -C $(PKG_BUILD_DIR)/src
endef

define Package/$(PKG_NAME)/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/src/tftdisplay $(1)/usr/bin/tftdisplay

	$(CP) ./files/* $(1)/

endef

define Package/$(PKG_NAME)/postinst
#!/bin/sh
[ -n "$${IPKG_INSTROOT}" ] || { 
    /etc/init.d/tftdisplay enable   # 设置开机启动
    /etc/init.d/tftdisplay restart  # 安装/升级后立即生效
}
exit 0

endef

$(eval $(call BuildPackage,$(PKG_NAME)))