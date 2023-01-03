#!/bin/bash -ex

version=1.0.26.11755
commit=1c6e76a483238ba9f0511c77b02ea7607c30d897

rm -rf libusb-src
rm -f libusb-${commit}.zip
curl -LOJ https://github.com/libusb/libusb/archive/${commit}.zip
unzip libusb-${commit}.zip
mv libusb-${commit} libusb-src

pushd libusb-src

git init
git add .
git commit -m foobar --author "foobar <foobar@foobar.com>"
git apply --verbose ../libusb-brickd.patch

popd

echo done
