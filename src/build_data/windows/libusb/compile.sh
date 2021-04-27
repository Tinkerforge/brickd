#!/bin/bash -ex

# apt install gcc-mingw-w64-i686 g++-mingw-w64-i686 binutils-mingw-w64-i686

rm -f libusb.h
rm -f libusb-1.0-brickd.dll
rm -f libusb-1.0-brickd.dll.a
rm -f libusb-1.0-brickd.a

pushd libusb-src

./autogen.sh --host i686-w64-mingw32 --disable-examples-build --disable-tests-build
make clean
#make CFLAGS="-Og -g -ggdb"
make

popd

cp libusb-src/libusb/libusb.h libusb.h
cp libusb-src/libusb/libusb-1.0.def libusb-1.0-brickd.def
cp libusb-src/libusb/.libs/libusb-1.0-brickd.dll libusb-1.0-brickd.dll
cp libusb-src/libusb/.libs/libusb-1.0-brickd.dll.a libusb-1.0-brickd.dll.a
cp libusb-src/libusb/.libs/libusb-1.0-brickd.a libusb-1.0-brickd-static.a

echo done
