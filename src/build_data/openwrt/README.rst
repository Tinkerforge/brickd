Support files for OpenWrt
=========================

Brick Daemon can be compiled for OpenWrt. The following two files are required
for this:

* ``brickd.hotplug`` is a hotplug2 script that sends an USR1 signal to brickd
  when a Brick is connected or removed from the USB bus. This approach is used
  instead of the normal libudev based hotplug detection in brickd.

* ``Makefile`` is a OpenWrt package definition Makefile that is needed to build
  brickd as an OpenWrt package.

To include the package into your OpenWrt build simply link or copy the
``src/build_data/openwrt`` folder to the package directory of your OpenWrt
build tree, select the ``brickd2`` package in the menuconfig and build.
