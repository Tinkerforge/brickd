#!/bin/bash -ex

# FIXME: use brickd version number here with docker image subversion, e.g. 2.4.3-1
version=1.0.0

pushd ../../..

src_zip=src/build_data/docker/src.zip

rm -f ${src_zip}
zip ${src_zip} src/brickd/*
zip ${src_zip} src/daemonlib/*
zip ${src_zip} src/build_data/linux/libusb/*

popd

docker build --no-cache -t tinkerforge/brickd-static:${version} .

rm -f src.zip
