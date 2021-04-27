This is a special version of libusb for brickd.

Based on libusb's github.com commit 1001cb5558cf6679af7bce3114bba1d3bb7b6f7f
(libusb version 1.0.24.11609) with the libusb-brickd.patch applied to it.

Changes:
- Add libusb_set_log_callback function to intercept all log output.
- Rename libusb-1.0.a to libusb-1.0-brickd.a to avoid a potential name
  collisions with a system-wide installed libusb-1.0.a.
