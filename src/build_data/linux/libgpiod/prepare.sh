#!/bin/bash -ex

version=$(head -n 1 version.txt)

rm -rf libgpiod-src
rm -f libgpiod-${version}.tar.gz
curl -LOJ https://git.kernel.org/pub/scm/libs/libgpiod/libgpiod.git/snapshot/libgpiod-${version}.tar.gz
tar -xvf libgpiod-${version}.tar.gz
mv libgpiod-${version} libgpiod-src

pushd libgpiod-src

git init
git add .
git commit -m foobar --author "foobar <foobar@foobar.com>"
git apply --verbose ../libgpiod-brickd.patch

popd

echo done
