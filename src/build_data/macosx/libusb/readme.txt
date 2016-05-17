This is a special version of libusb for brickd build on Mac OS X 10.11.

Based on libusb's github.com commit 578942b5a90cd36b47b11e0992c2e92a05b70d91
(libusb version 1.0.20 plus some fixes) with the libusb-brickd.patch
applied to it.

The libusb-1.0.dylib was build using the compile.sh script.

Changes:
- Add libusb_set_log_function function to intercept all log output.
- Don't open Apple devices before requesting the device descriptor. This avoids
  breaking the functionality of the Apple USB Ethernet Adapter.
