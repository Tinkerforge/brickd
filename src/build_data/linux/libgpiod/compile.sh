#!/bin/bash -ex

rm -f gpiod.h
rm -f libgpiod-brickd-static.a

pushd libgpiod-src

./autogen.sh --disable-shared
make clean
#make CFLAGS="-Og -g -ggdb"
make

popd

cp libgpiod-src/include/gpiod.h gpiod.h
cp libgpiod-src/lib/.libs/libgpiod-brickd.a libgpiod-brickd-static.a

echo done
