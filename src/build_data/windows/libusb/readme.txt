This is a special version of libusb for brickd build with MinGW-w64.

Based on libusb's github.com commit 1c6e76a483238ba9f0511c77b02ea7607c30d897
(libusb version 1.0.26.11755) with the libusb-brickd.patch applied to it.

Changes:
- Add libusb_set_log_callback function to intercept all log output.
- Check for invalid port number reported by Renesas/NEC USB controller with
  outdated driver.
- Enable USB enumerate debug logging
- Ignoring orphaned USB transfer completion to avoid crashing

To compile libusb-1.0-brickd-static.a and libusb-1.0-brickd.dll[.a] run
./prepare.sh and ./compile.sh. To create libusb-1.0-brickd.lib run finish.bat.

To modify the libusb-brickd.patch run ./prepare.sh then modify the source in
the libusb-src directory and run ./capture.sh to update libusb-brickd.patch.
