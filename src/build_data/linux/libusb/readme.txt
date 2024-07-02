This is a special version of libusb for brickd for static linking.

Based on libusb's github.com commit 1c6e76a483238ba9f0511c77b02ea7607c30d897
(libusb version 1.0.26.11755) with the libusb-brickd.patch applied to it.

Changes:
- Add libusb_set_log_callback function to intercept all log output.
- Rename libusb-1.0.a to libusb-1.0-brickd-static.a to avoid a potential name
  collisions with a system-wide installed libusb-1.0.a.

To compile libusb-1.0-brickd-static.a run ./prepare.sh and ./compile.sh.

To modify the libusb-brickd.patch run ./prepare.sh then modify the source in
the libusb-src directory and run ./capture.sh to update libusb-brickd.patch.
