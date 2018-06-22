This is a special version of libusb for brickd build on macOS 10.11.6.

Based on libusb's github.com commit 0034b2afdcdb1614e78edaa2a9e22d5936aeae5d
(libusb version 1.0.22) with the libusb-brickd.patch applied to it.

The libusb-1.0.dylib was build using the compile.sh script.

Changes:
- Add libusb_set_log_callback function to intercept all log output.
- Don't open Apple devices before requesting the device descriptor. This avoids
  breaking the functionality of the Apple USB Ethernet Adapter.
