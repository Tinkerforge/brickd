This is a special version of libusb for brickd build on macOS 10.15.7.

Based on libusb's github.com commit 1001cb5558cf6679af7bce3114bba1d3bb7b6f7f
(libusb version 1.0.24.11609) with the libusb-brickd.patch applied to it.

The libusb-1.0-brickd-static.a was build using the prepare.sh and compile.sh
scripts.

Changes:
- Add libusb_set_log_callback function to intercept all log output.
- Don't open Apple devices before requesting the device descriptor. This avoids
  breaking the functionality of the Apple USB Ethernet Adapter.
- Rename libusb-1.0.a to libusb-1.0-brickd.a to avoid a potential name
  collisions with a system-wide installed libusb-1.0.a (e.g. homebrew).
