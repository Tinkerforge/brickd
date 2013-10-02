This is a special version of libusbx for brickd build with WDK 7 for x86.

Based on libusbx github.org commit a9cd54f24d566062a461d27f615365f41a3d11e8
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
