This is a special version of libusb for brickd build with WDK 7 for x86.

Based on libusb's github.com commit 8facad00ea66e0609d93ad8aa4e174a6e7be8b3c
(libusb version 1.0.18 plus some fixes) with the libusb-brickd.patch
applied to it.

The MinGW import lib libusb-1.0.dll.a was created from libusb-1.0.def using:
dlltool -k -d libusb-1.0.def -l libusb-1.0.dll.a

Changes:
- Add libusb_set_log_file function to redirect log output to a file.
- Add libusb_free function to avoid heap problems due to potential msvcrt.dll
  mismatch between libusb and brickd.
- Make libusb_get_pollfds work on Windows.
- Expose internal functions for fake file descriptors, to allow integration
  into the brickd event loop.
- Make event handling ignore leaked transfer handles.

Known issues:
- Windows XP: Submitted transfers are not correctly aborted on USB device
  disconnect. This results in leaking the underlying fake file descriptor.
  Currently libsubx has a hard limit of 4096 fake file descriptors. When
  libusb runs out of fake file descriptors then new transfers cannot be
  created anymore. Workaround: brickd restart.
