This is a special version of libusbx for brickd build on Mac OS X 10.6.

Based on libusbx github.org commit 0500232303fe706dbe538290a49869f1dadf90af
(libusbx version 1.0.17 plus some fixes) with the libusbx-brickd.patch
applied to it.

The libusb-1.0.dylib was build using the following commands:
cd <libusbx-clone>
mkdir installed
./autogen.sh --prefix=<libusbx-clone>/installed
make clean
make
make install
cp installed/lib/libusb-1.0.0.dylib <brickd-clone>/src/build_data/macosx/libusb/libusb-1.0.dylib
cp installed/include/libusb-1.0/libusb.h <brickd-clone>/src/build_data/macosx/libusb/
cd <brickd-clone>/src/build_data/macosx/libusb
install_name_tool -id @executable_path/../build_data/macosx/libusb/libusb-1.0.dylib libusb-1.0.dylib

Changes:
- Don't open Apple devices before requesting the device descriptor. This avoids
  breaking the functionality of the Apple USB Ethernet Adapter.
