This is a special version of libusbx for brickd build with WDK 7 for x86.

Based on libusbx github.org commit 7b62a0a171ac0141a3d12237ab496c49cccd79df
(libusbx version 1.0.17 plus a few fixes) with the libusbx-brickd.patch
applied to it.

The MinGW import lib libusb-1.0.dll.a was created from libusb-1.0.def using:
dlltool -k -d libusb-1.0.def -l libusb-1.0.dll.a

Known issues:
- Windows XP: Submitted transfers are not correctly aborted on USB device
  disconnect. This results in leaking the underlying fake file descriptor.
  Currently libsubx has a hard limit of 4096 fake file descriptors. When
  libusbx runs out of fake file descriptors then new transfers cannot be
  created anymore. Workaround: brickd restart.
