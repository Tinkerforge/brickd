#!/bin/bash -ex

# sudo apt-get install qemu binfmt-support qemu-user-static
# docker run --rm --privileged multiarch/qemu-user-static --reset -p yes

for architecture in amd64 i386 arm32v7 arm64v8; do
    docker build --no-cache --build-arg ARCHITECTURE=${architecture} -t tinkerforge/builder-brickd-debian-${architecture}:1.0.0 .
done
