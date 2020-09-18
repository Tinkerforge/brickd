#!/bin/bash -ex

for architecture in amd64 i386 arm32v7 arm64v8; do
    docker build --no-cache --build-arg ARCHITECTURE=${architecture} -t tinkerforge/builder-brickd-debian-${architecture}:1.0.0 .
done
