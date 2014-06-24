include $(TOPDIR)/rules.mk

PKG_NAME:=brickd2
PKG_VERSION:=2013-08-8
PKG_RELEASE=$(PKG_SOURCE_VERSION)

PKG_SOURCE_PROTO:=git
PKG_SOURCE_URL:=https://github.com/Tinkerforge/brickd.git
PKG_SOURCE_SUBDIR:=$(PKG_NAME)-$(PKG_VERSION)
PKG_SOURCE_VERSION:=master
PKG_SOURCE:=$(PKG_NAME)-$(PKG_VERSION)-$(PKG_SOURCE_VERSION).tar.gz
PKG_MAINTAINER:=Bjoern Riemer <bjoern.riemer@web.de>

include $(INCLUDE_DIR)/package.mk

define Package/brickd2
	SECTION:=Tinkerforge
	CATEGORY:=Tinkerforge
	DEPENDS:=+libusb-1.0
	TITLE:=Tinkerforge Brick Daemon 2.0 (C implementation)
endef

define Build/Prepare
	$(Build/Prepare/Default)
	if [ ! -d $(PKG_BUILD_DIR)/src/daemonlib ]; then git clone https://github.com/Tinkerforge/daemonlib.git $(PKG_BUILD_DIR)/src/daemonlib; fi
endef

define Build/Compile
	$(TARGET_CONFIGURE_OPTS) \
		CFLAGS="$(TARGET_CFLAGS) -I$(STAGING_DIR)/usr/include -I$(PKG_BUILD_DIR)" \
		LDFLAGS="$(TARGET_LDFLAGS)" \
		WITH_LIBUDEV=no WITH_LOGGING=yes WITH_USB_REOPEN_ON_SIGUSR1=no \
		$(MAKE) -C $(PKG_BUILD_DIR)/src/brickd all
endef

define Package/brickd2/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/src/brickd/brickd $(1)/usr/bin/
	$(INSTALL_DIR) $(1)/etc/hotplug.d/usb
	$(INSTALL_DATA) ./brick.hotplug $(1)/etc/hotplug.d/usb/11-brick
endef

$(eval $(call BuildPackage,brickd2))
