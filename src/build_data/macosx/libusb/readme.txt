This is a special version of libusb for brickd build on Mac OS X 10.6.

Based on libusb's github.com commit 25e82765978bef1189cb810d454165f76451a4bf
(libusb version 1.0.18 plus some fixes) with the libusb-brickd.patch
applied to it.

The libusb-1.0.dylib was build using the compile.sh script.

Changes:
- Don't open Apple devices before requesting the device descriptor. This avoids
  breaking the functionality of the Apple USB Ethernet Adapter.
