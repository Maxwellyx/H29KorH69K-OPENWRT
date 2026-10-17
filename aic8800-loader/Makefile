#
# aic8800-loader —— 启动完成后再加载 aic8800 SDIO WiFi 驱动
#
# 为什么不在 preinit 里自动加载（AUTOLOAD）：
#   驱动用 AUTOLOAD 会在 kmodloader(preinit) 阶段 insmod，一旦探测卡住，
#   整个启动就停在那里 → 看门狗复位 → 开机一半循环重启。
#   所以改成启动完成后由本脚本在后台加载，并把全过程写进日志，便于排查。
#
include $(TOPDIR)/rules.mk

PKG_NAME:=aic8800-loader
PKG_VERSION:=1
PKG_RELEASE:=1

include $(INCLUDE_DIR)/package.mk

define Package/aic8800-loader
  SECTION:=net
  CATEGORY:=Network
  TITLE:=Load aic8800 SDIO wifi driver after boot (bsp then fdrv)
  DEPENDS:=+kmod-aic8800-sdio +aic8800-sdio-firmware
endef

# 纯文件包：没有源码，必须显式给一个空的 Build/Compile，
# 否则默认会在空的 PKG_BUILD_DIR 里跑 make 而报错。
define Build/Compile
	true
endef

define Package/aic8800-loader/install
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/aic8800.init $(1)/etc/init.d/aic8800
endef

$(eval $(call BuildPackage,aic8800-loader))
