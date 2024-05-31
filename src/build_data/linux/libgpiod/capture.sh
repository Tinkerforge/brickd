#!/bin/bash -ex

pushd libgpiod-src

git diff > ../libgpiod-brickd.patch

popd

echo done
