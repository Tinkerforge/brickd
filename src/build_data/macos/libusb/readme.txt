This is a special version of libusb for brickd build on macOS 10.15.7.

Based on libusb's github.com commit 1c6e76a483238ba9f0511c77b02ea7607c30d897
(libusb version 1.0.26.11755) with the libusb-brickd.patch applied to it.

The libusb-1.0-brickd-static.a was build using the prepare.sh and compile.sh
scripts.

Changes:
- Add libusb_set_log_callback function to intercept all log output.
- Don't open Apple devices before requesting the device descriptor. This avoids
  breaking the functionality of the Apple USB Ethernet Adapter.
- Rename libusb-1.0.a to libusb-1.0-brickd.a to avoid a potential name
  collisions with a system-wide installed libusb-1.0.a (e.g. homebrew).

To compile libusb-1.0-brickd-static.a run ./prepare.sh and ./compile.sh.

To modify the libusb-brickd.patch run ./prepare.sh then modify the source in
the libusb-src directory and run ./capture.sh to update libusb-brickd.patch.
