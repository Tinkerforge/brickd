This is a special version of libusb for brickd build on Mac OS X 10.11.6.

Based on libusb's github.com commit b14d0a49e8014c0043673160ace2910e5742a1d2
(libusb version 1.0.21 plus some fixes) with the libusb-brickd.patch
applied to it.

The libusb-1.0.dylib was build using the compile.sh script.

Changes:
- Add libusb_set_log_function function to intercept all log output.
- Don't open Apple devices before requesting the device descriptor. This avoids
  breaking the functionality of the Apple USB Ethernet Adapter.
