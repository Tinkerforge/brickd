#!/bin/bash -ex

version=1.0.23

rm -rf libusb-${version}
rm -rf libusb-${version}.tar.bz2
rm -rf libusb.h
rm -rf libusb-1.0-brickd-static.a

wget https://github.com/libusb/libusb/releases/download/v${version}/libusb-${version}.tar.bz2

tar -xf libusb-${version}.tar.bz2

pushd libusb-${version}

./configure --disable-shared --disable-udev
make

popd

cp libusb-${version}/libusb/libusb.h .
cp libusb-${version}/libusb/.libs/libusb-1.0.a libusb-1.0-brickd-static.a

echo done
