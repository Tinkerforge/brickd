#!/bin/bash -ex

version=1.0.24.11609
commit=1001cb5558cf6679af7bce3114bba1d3bb7b6f7f

rm -rf libusb-src
rm -f libusb-${commit}.zip
curl -LOJ https://github.com/libusb/libusb/archive/${commit}.zip
unzip libusb-${commit}.zip
mv libusb-${commit} libusb-src

pushd libusb-src

git init
git add .
git commit -m foobar --author "foobar <foobar@foobar.com>"
git apply ../libusb-brickd.patch

popd

echo done
