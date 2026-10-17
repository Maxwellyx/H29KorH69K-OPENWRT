#
# hinlink-settings —— Hinlink 系列设备的出厂默认设置
#
# 来源：lede 树里 package/daocaoren/lede-patches/001-lean-default-settings.patch
#       的那部分**我们自己加的**改动。
#
# 为什么在上游要做成独立包而不是打补丁：
#   上游 OpenWrt 没有 package/lean/ 这个目录（那是 coolsnowwolf/lede 专有的），
#   那个补丁无处可打。而我们要做的事（主机名、时区、无线默认开、F50-PRO 网络）
#   本来就只是一个 /etc/uci-defaults 脚本，做成独立包反而更干净：
#   **一个字节都不改上游源码**。
#
# 注意：本包只包含 daocaoren 自己加的东西。lede 那份 default-settings 里还有一大堆
#       lede 品牌的私货（services→nas 菜单改名、DISTRIB_DESCRIPTION 写死 LEDE、
#       dnsmasq 日志关掉……），那些是 lede 发行版自己的行为，**上游 OpenWrt 不该继承**，
#       所以没有搬过来。
#
include $(TOPDIR)/rules.mk

PKG_NAME:=hinlink-settings
PKG_VERSION:=1.1
PKG_RELEASE:=2
PKG_MAINTAINER:=daocaoren
PKG_LICENSE:=GPL-2.0

include $(INCLUDE_DIR)/package.mk

define Package/hinlink-settings
  SECTION:=base
  CATEGORY:=Base system
  TITLE:=Hinlink factory defaults (hostname / wifi ssid+key / root password / F50-PRO WAN)
  URL:=https://github.com/daocaoren
endef

define Package/hinlink-settings/description
  Hinlink 系列设备（OPC-H29K / OPC-H69K / OPC-HT2 / F50-PRO）的出厂默认设置。

  只装三个文件，全部在 daocaoren 里，**不改上游任何源码**：

    /etc/uci-defaults/zzz-hinlink-settings   首次启动跑一次
    /etc/init.d/hinlink-defaults             每次启动调一次（幂等，做过即退出）
    /usr/libexec/hinlink-defaults            上面两者共用的幂等实现

  做的事：

    - 主机名、**无线 SSID** 都按设备树 model 自动设定（同一来源）：
      H29K / H69K / HT2 / F50-PRO / Hinlink
    - 无线加密 psk2，密码固定 password（所有 AP 接口）
    - root 登录密码固定 password（$1$ MD5-crypt，LuCI 不再提示"未设置密码"）
    - 时区 CST-8 + 阿里/腾讯/国家授时中心 NTP
    - 无线默认开启（去掉 /etc/config/wireless 里的 option disabled）
    - F50-PRO 专属：WAN 走 eth1，并在 /etc/nftables.d/ 写 DNS 重定向 + NAT（fw4）

  上面的默认值都带一次性标记（/etc/.hinlink-rootpass-done、/etc/.hinlink-wifi-done），
  **用户自己改过之后不会被再覆盖**。
endef

define Package/hinlink-settings/install
	$(INSTALL_DIR) $(1)/etc/uci-defaults
	$(INSTALL_BIN) ./files/etc/uci-defaults/zzz-hinlink-settings \
		$(1)/etc/uci-defaults/zzz-hinlink-settings

	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/etc/init.d/hinlink-defaults \
		$(1)/etc/init.d/hinlink-defaults

	$(INSTALL_DIR) $(1)/usr/libexec
	$(INSTALL_BIN) ./files/usr/libexec/hinlink-defaults \
		$(1)/usr/libexec/hinlink-defaults
	$(INSTALL_DIR) $(1)/etc/hotplug.d/ieee80211
	$(INSTALL_BIN) ./files/etc/hotplug.d/ieee80211/50-hinlink-menu-refresh $(1)/etc/hotplug.d/ieee80211/50-hinlink-menu-refresh
endef

# ---------------------------------------------------------------------------
# 纯配置文件包：没有源码，必须显式覆盖 Build/Compile —— OpenWrt 默认会到
# $(PKG_BUILD_DIR) 里跑 make，而那个目录里没有 Makefile，于是报：
#   make[3]: *** No targets specified and no makefile found.  Stop.
# 空 recipe 的写法照抄上游 package/base-files/Makefile。
# ---------------------------------------------------------------------------
define Build/Compile/Default

endef
Build/Compile = $(Build/Compile/Default)

$(eval $(call BuildPackage,hinlink-settings))
