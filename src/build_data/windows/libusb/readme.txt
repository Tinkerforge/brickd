This is a special version of libusb for brickd build with WDK 7 for x86.

Based on libusb's github.com commit 0034b2afdcdb1614e78edaa2a9e22d5936aeae5d
(libusb version 1.0.22) with the libusb-brickd.patch applied to it.

The MinGW import lib libusb-1.0.dll.a was created from libusb-1.0.def using:
dlltool -k -d libusb-1.0.def -l libusb-1.0.dll.a

Changes:
- Add libusb_set_log_callback function to intercept all log output.
- Make libusb_get_pollfds work on Windows.
- Expose internal functions for fake file descriptors, to allow integration
  into the brickd event loop.
- Make event handling ignore leaked transfer handles.
- Check for invalid port number reported by Renesas/NEC USB controller with
  outdated driver.
- Make usbi_poll work with more than 64 fake file descriptors in all cases.

Known issues:
- Windows XP: Submitted transfers are not correctly aborted on USB device
  disconnect. This results in leaking the underlying fake file descriptor.
  Currently libsub has a hard limit of 512 fake file descriptors. When
  libusb runs out of fake file descriptors then new transfers cannot be
  created anymore. Workaround: brickd restart.
