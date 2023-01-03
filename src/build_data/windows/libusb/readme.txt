This is a special version of libusb for brickd build with MinGW-w64.

Based on libusb's github.com commit 1c6e76a483238ba9f0511c77b02ea7607c30d897
(libusb version 1.0.26.11755) with the libusb-brickd.patch applied to it.

The libusb-1.0-brickd-static.a and libusb-1.0-brickd.dll[.a] were build using
the prepare.sh and compile.sh scripts.

The libusb-1.0-brickd.lib was build using the finish.bat scripts.

Changes:
- Add libusb_set_log_callback function to intercept all log output.
- Check for invalid port number reported by Renesas/NEC USB controller with
  outdated driver.
- Enable USB enumerate debug logging
- Ignoring orphaned USB transfer completion to avoid crashing
