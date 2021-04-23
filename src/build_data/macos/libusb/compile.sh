#!/bin/sh -ex

rm -rf libusb.h
rm -rf libusb-1.0-brickd.a

pushd libusb-src

make clean
make

popd

cp libusb-src/libusb/libusb.h libusb.h
cp libusb-src/libusb/.libs/libusb-1.0-brickd.a libusb-1.0-brickd-static.a

echo done
