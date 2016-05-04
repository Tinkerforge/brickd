This is a special version of libusb for brickd build with WDK 7 for x86.

Based on libusb's github.com commit 578942b5a90cd36b47b11e0992c2e92a05b70d91
(libusb version 1.0.20 plus several patches) with the libusb-brickd.patch
applied to it.

The MinGW import lib libusb-1.0.dll.a was created from libusb-1.0.def using:
dlltool -k -d libusb-1.0.def -l libusb-1.0.dll.a

Changes:
- Add libusb_set_log_function function to intercept all log output.
- Make libusb_get_pollfds work on Windows.
- Expose internal functions for fake file descriptors, to allow integration
  into the brickd event loop.
- Make event handling ignore leaked transfer handles.
- Workaround possible issues with the RED Brick composite device.
- Check for invalid port number reported by Renesas/NEC USB controller with
  outdated driver.
- Add support for Intel Alpine Ridge USB 3.1 controller.

Known issues:
- Windows XP: Submitted transfers are not correctly aborted on USB device
  disconnect. This results in leaking the underlying fake file descriptor.
  Currently libsubx has a hard limit of 4096 fake file descriptors. When
  libusb runs out of fake file descriptors then new transfers cannot be
  created anymore. Workaround: brickd restart.
