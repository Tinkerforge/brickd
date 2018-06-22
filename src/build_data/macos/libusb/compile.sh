#!/bin/sh
# run this in a clone of https://github.com/libusb/libusb
set -ex
git checkout -f
git apply ../brickd/src/build_data/macos/libusb/libusb-brickd.patch
mkdir -p installed
./autogen.sh --prefix=${PWD}/installed
make
make install
cp installed/lib/libusb-1.0.0.dylib ../brickd/src/build_data/macos/libusb/libusb-1.0.dylib
cp installed/include/libusb-1.0/libusb.h ../brickd/src/build_data/macos/libusb/
install_name_tool -id @executable_path/../build_data/macos/libusb/libusb-1.0.dylib ../brickd/src/build_data/macos/libusb/libusb-1.0.dylib
echo done
