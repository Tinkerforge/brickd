#!/bin/sh -ex

version=$(head -n 1 version.txt)
commit=$(tail -n +2 version.txt | head -n 1)

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
