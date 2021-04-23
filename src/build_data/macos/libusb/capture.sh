#!/bin/sh -ex

pushd libusb-src

git diff > ../libusb-brickd.patch

popd

echo done
