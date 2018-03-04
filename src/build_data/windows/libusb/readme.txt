This is a special version of libusb for brickd build with WDK 7 for x86.

Based on libusb's github.com commit b14d0a49e8014c0043673160ace2910e5742a1d2
(libusb version 1.0.21 plus several patches) with the libusb-brickd.patch
applied to it.

The MinGW import lib libusb-1.0.dll.a was created from libusb-1.0.def using:
dlltool -k -d libusb-1.0.def -l libusb-1.0.dll.a

Changes:
- Add libusb_set_log_callback function to intercept all log output.
- Make libusb_get_pollfds work on Windows.
- Expose internal functions for fake file descriptors, to allow integration
  into the brickd event loop.
- Make event handling ignore leaked transfer handles.
- Workaround possible issues with the RED Brick composite device.
- Check for invalid port number reported by Renesas/NEC USB controller with
  outdated driver.
- Make usbi_poll work with more than 64 fake file descriptors in all cases.

Known issues:
- Windows XP: Submitted transfers are not correctly aborted on USB device
  disconnect. This results in leaking the underlying fake file descriptor.
  Currently libsub has a hard limit of 4096 fake file descriptors. When
  libusb runs out of fake file descriptors then new transfers cannot be
  created anymore. Workaround: brickd restart.
