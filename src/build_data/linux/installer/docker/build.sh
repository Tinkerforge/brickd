#!/bin/bash -ex

# prepare host system to run docker with ARM containers
#
# sudo apt-get install qemu binfmt-support qemu-user-static
# docker run --rm --privileged multiarch/qemu-user-static --reset -p yes

# distinguish between arm32v7 and arm32v6. historically brickd was build on
# target. the RED Brick is arm32v7 and the Raspberry Pi Zero is arm32v6. build
# the general armhf release as arm32v6 to be compatible with all Raspberry Pi
# models

version=1.1.0

for architecture in amd64 i386 arm32v7 arm64v8; do
    docker build --no-cache --build-arg ARCHITECTURE=${architecture} -t tinkerforge/builder-brickd-debian-${architecture}:${version} .
done

# update raspbian.gpg to use Raspbian APT repository in Debian based container
#
# rm raspbian.gpg
# wget -q http://raspbian.raspberrypi.org/raspbian.public.key
# wget -q http://archive.raspberrypi.org/debian/raspberrypi.gpg.key
# sudo apt-key --keyring ./raspbian.gpg add raspbian.public.key
# sudo apt-key --keyring ./raspbian.gpg add raspberrypi.gpg.key

docker build --no-cache -f Dockerfile.arm32v6 -t tinkerforge/builder-brickd-debian-arm32v6:${version} .
