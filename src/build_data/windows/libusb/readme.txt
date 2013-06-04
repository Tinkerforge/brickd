special version of libusbx for brickd build with WDK 7 for x86

based on libusbx github.org commit ce75e9af3f9242ec328b0dc2336b69ff24287a3c
with the libusbx-brickd.patch applied to it

the mingw import lib libusb-1.0.dll.a was created from libusb-1.0.def using
dlltool -k -d libusb-1.0.def -l libusb-1.0.dll.a

known issues:
- Windows XP: submitted transfers are not correctly aborted on USB device
  disconnect. this results in leaking the underlying fake file descriptor.
  currently libsubx has a hard limit of 1024 fake file descriptors. when
  libusbx runs out of fake file descriptors then new transfers cannot be
  created anymore. workaround: brickd restart
