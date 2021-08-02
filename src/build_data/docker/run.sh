#!/bin/bash -ex

# FIXME: use brickd version number here with docker image subversion, e.g. 2.4.3-1
version=1.0.0

docker run --rm --privileged -p 4223:4223 tinkerforge/brickd-static:${version}
