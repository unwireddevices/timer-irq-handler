#
# Copyright (C) 2008-2012 OpenWrt.org
#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#

include $(TOPDIR)/rules.mk
include $(INCLUDE_DIR)/kernel.mk

PKG_NAME:=timer-irq-handler
PKG_RELEASE:=2

include $(INCLUDE_DIR)/package.mk

define KernelPackage/timer-irq-handler
  SUBMENU:=Other modules
  DEPENDS:=@!LINUX_3_3
  TITLE:=Timer IRQ handler
  FILES:=$(PKG_BUILD_DIR)/timer-irq-handler.ko
  AUTOLOAD:=$(call AutoLoad,30,timer-irq-handler,1)
  KCONFIG:=
endef

define KernelPackage/timer-irq-handler/description
 This is a kernel to userspace timer IRQ translator for AR9331 devices.
endef

MAKE_OPTS:= \
	ARCH="$(LINUX_KARCH)" \
	CROSS_COMPILE="$(TARGET_CROSS)" \
	SUBDIRS="$(PKG_BUILD_DIR)"

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) ./src/* $(PKG_BUILD_DIR)/
endef

define Build/Compile
	$(MAKE) -C "$(LINUX_DIR)" \
		$(MAKE_OPTS) \
		modules
endef

$(eval $(call KernelPackage,timer-irq-handler))
