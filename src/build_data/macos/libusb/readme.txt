This is a special version of libusb for brickd build on macOS 10.11.6.

Based on libusb's github.com commit f1e385390213aab96d2a40e4858ff0d019a1b0b7
(libusb version 1.0.23-rc1 and a few commits) with the libusb-brickd.patch
applied to it.

The libusb-1.0-brickd.dylib was build using the compile.sh script.

Changes:
- Add libusb_set_log_callback function to intercept all log output.
- Don't open Apple devices before requesting the device descriptor. This avoids
  breaking the functionality of the Apple USB Ethernet Adapter.
- Rename libusb-1.0.dylib to libusb-1.0-brickd.dylib to avoid a potential name
  collisions with a system-wide installed libusb-1.0.dylib (e.g. homebrew).
