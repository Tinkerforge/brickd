include $(TOPDIR)/rules.mk

PKG_NAME:=brickd2
PKG_VERSION:=2013-07-24
PKG_RELEASE=$(PKG_SOURCE_VERSION)

PKG_SOURCE_PROTO:=git
PKG_SOURCE_URL:=https://github.com/Tinkerforge/brickd.git
PKG_SOURCE_SUBDIR:=$(PKG_NAME)-$(PKG_VERSION)
PKG_SOURCE_VERSION:=3d05fa4381e65bfb35ef7825b08b03851ff508f3
PKG_SOURCE:=$(PKG_NAME)-$(PKG_VERSION)-$(PKG_SOURCE_VERSION).tar.gz
PKG_MAINTAINER:=Bjoern Riemer <bjoern.riemer@web.de>

include $(INCLUDE_DIR)/package.mk

define Package/brickd2
	SECTION:=FOKUS
	CATEGORY:=FOKUS
	DEPENDS:=+libusb-1.0
	TITLE:=Thinkerforge Brick Daemon 2.0 (C implementation)
endef

define Build/Configure
	$(INSTALL_DATA) ./files/Makefile-openwrt $(PKG_BUILD_DIR)/src/brickd
endef

define Build/Compile
	$(TARGET_CONFIGURE_OPTS) \
		CFLAGS="$(TARGET_CFLAGS) -I$(STAGING_DIR)/usr/include -I$(PKG_BUILD_DIR)" \
		LDFLAGS="$(TARGET_LDFLAGS)" \
		$(MAKE) -C $(PKG_BUILD_DIR)/src/brickd -f Makefile-openwrt all
endef

define Package/brickd2/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/src/brickd/brickd $(1)/usr/bin/
	$(INSTALL_DIR) $(1)/etc/hotplug.d/usb
	$(INSTALL_DATA) ./files/11-brick $(1)/etc/hotplug.d/usb/11-brick
endef

$(eval $(call BuildPackage,brickd2))