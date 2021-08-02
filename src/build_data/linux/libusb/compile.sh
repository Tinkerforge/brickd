#!/bin/bash -ex

rm -f libusb.h
rm -f libusb-1.0-brickd.a

pushd libusb-src

./autogen.sh --disable-shared --disable-udev --disable-examples-build --disable-tests-build
make clean
#make CFLAGS="-Og -g -ggdb"
make

popd

cp libusb-src/libusb/libusb.h libusb.h
cp libusb-src/libusb/.libs/libusb-1.0-brickd.a libusb-1.0-brickd-static.a

echo done
