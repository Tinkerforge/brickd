#!/bin/bash

version=1.2.0

exec docker run --rm -it tinkerforge/builder-brickd-debian-$1:${version} bash -c $2
