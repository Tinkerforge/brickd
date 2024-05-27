#!/bin/bash -ex

# distinguish between arm32v7 and arm32v6. historically brickd was build on
# target. the RED Brick is arm32v7 and the Raspberry Pi Zero is arm32v6. build
# the general armhf release as arm32v6 to be compatible with all Raspberry Pi
# models

version=1.2.1

rm -rf dist

daemonlib_symlink=$(readlink "$(pwd)/daemonlib" || true)

if [ -n "${daemonlib_symlink}" ]; then
    daemonlib_path=$(realpath "${daemonlib_symlink}")
    daemonlib_volume="-v ${daemonlib_path}:${daemonlib_path}"
else
    daemonlib_volume=
fi

changelog_date=$(date -R)

brickd_path=$(realpath "$(pwd)/..")
brickd_volume="-v ${brickd_path}:${brickd_path}"

for config in amd64,linux/amd64 i386,linux/386 arm32v6,linux/arm arm64v8,linux/arm64; do
    IFS=',' read architecture platform <<< "${config}"
    docker run --rm -it --platform ${platform} ${brickd_volume} ${daemonlib_volume} -u $(id -u):$(id -g) tinkerforge/builder-brickd-debian-${architecture}:${version} bash -c "cd ${brickd_path}/src; python3 -u build_pkg.py --changelog-date '${changelog_date}' $1"
done

docker run --rm -it --platform linux/arm ${brickd_volume} ${daemonlib_volume} -u $(id -u):$(id -g) tinkerforge/builder-brickd-debian-arm32v7:${version} bash -c "cd ${brickd_path}/src; python3 -u build_pkg.py --changelog-date '${changelog_date}' --with-red-brick $1"
