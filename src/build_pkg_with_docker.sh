#!/bin/bash -ex

rm -rf dist

daemonlib_symlink=$(readlink daemonlib || true)

if [ -n "${daemonlib_symlink}" ]; then
    daemonlib_path=$(realpath "${daemonlib_symlink}")
    daemonlib_volume="-v ${daemonlib_path}:${daemonlib_path}"
else
    daemonlib_volume=
fi

changelog_date=$(date -R)

for architecture in amd64 i386 arm32v7 arm64v8; do
    docker run --rm -it -v $(pwd)/..:/brickd ${daemonlib_volume} tinkerforge/builder-brickd-debian-${architecture}:1.0.0 bash -c "cd /brickd/src; python3 -u build_pkg.py '${changelog_date}'"
done
