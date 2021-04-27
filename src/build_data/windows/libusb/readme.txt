This is a special version of libusb for brickd build with MinGW-w64.

Based on libusb's github.com commit 1001cb5558cf6679af7bce3114bba1d3bb7b6f7f
(libusb version 1.0.24.11609) with the libusb-brickd.patch applied to it.

The libusb-1.0-brickd-static.a and libusb-1.0-brickd.dll[.a] were build using
the prepare.sh and compile.sh scripts.

Changes:
- Add libusb_set_log_callback function to intercept all log output.
- Check for invalid port number reported by Renesas/NEC USB controller with
  outdated driver.
- Enable USB enumerate debug logging
- Ignoring orphaned USB transfer completion to avoid crashing
