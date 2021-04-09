#!/bin/bash -ex

# sudo apt get install gcc-mingw-w64-i686 g++-mingw-w64-i686 binutils-mingw-w64-i686

version=f1e385390213aab96d2a40e4858ff0d019a1b0b7

rm -rf libusb-${version}
rm -rf ${version}.zip
rm -rf libusb.h
rm -rf libusb-1.0-brickd-static.a

wget https://github.com/libusb/libusb/archive/${version}.zip

unzip ${version}.zip

pushd libusb-${version}

patch -p1 < ../../libusb/libusb-brickd.patch
./autogen.sh --host i686-w64-mingw32 --disable-shared
#make V=1 CFLAGS="-Og -g -ggdb"
make

popd

cp libusb-${version}/libusb/libusb.h .
cp libusb-${version}/libusb/.libs/libusb-1.0.a libusb-1.0-brickd-static.a

echo done
