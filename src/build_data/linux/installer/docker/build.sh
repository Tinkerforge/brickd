#!/bin/bash -ex

# prepare host system to run docker with ARM containers
#
# sudo apt-get install binfmt-support qemu-user-static
# docker run --rm --privileged tonistiigi/binfmt --install all

# distinguish between arm32v7 and arm32v6. historically brickd was build on
# target. the RED Brick is arm32v7 and the Raspberry Pi Zero is arm32v6. build
# the general armhf release as arm32v6 to be compatible with all Raspberry Pi
# models. but there is no ready made Raspberry Pi docker image available.
# therefore, we take an arm32v7 Debian image and install the toolchain from
# Raspbian to be able to target arm32v6

version=1.3.0

for config in amd64,linux/amd64 i386,linux/386 arm32v7,linux/arm/v7 arm64v8,linux/arm64; do
    echo ">>>>> building ${config}"
    IFS=',' read architecture platform <<< "${config}"
    docker build --platform=${platform} --no-cache --build-arg ARCHITECTURE=${architecture} -t tinkerforge/builder-brickd-debian-${architecture}:${version} .
done

# update raspbian.gpg to use Raspbian APT repository in Debian based container
#
# rm raspbian.gpg
# wget -q http://raspbian.raspberrypi.org/raspbian.public.key
# wget -q http://archive.raspberrypi.org/debian/raspberrypi.gpg.key
# sudo apt-key --keyring ./raspbian.gpg add raspbian.public.key
# sudo apt-key --keyring ./raspbian.gpg add raspberrypi.gpg.key

echo ">>>>> building arm32v6,linux/arm/v7"
docker build --platform=linux/arm/v7 --no-cache -f Dockerfile.arm32v6 -t tinkerforge/builder-brickd-debian-arm32v6:${version} .
