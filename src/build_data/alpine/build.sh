#!/bin/sh -ex

if [ -z "$1" ]; then
    docker_host=""
else
    docker_host="-H tcp://$1"
fi

docker ${docker_host} image rm -f tinkerforge/build_brickd_alpine || true
docker ${docker_host} build -t tinkerforge/build_brickd_alpine .

container=$(docker ${docker_host} create tinkerforge/build_brickd_alpine)
docker ${docker_host} cp ${container}:/home/user/packages .
docker ${docker_host} cp ${container}:/brickd/APKBUILD brickd/APKBUILD.updated

docker ${docker_host} rm -v $container
docker ${docker_host} image rm -f tinkerforge/build_brickd_alpine

mkdir -p output

for architecture in $(ls packages); do
    for apk in $(ls packages/${architecture}/*.apk); do
        cp ${apk} output/$(echo $(basename ${apk}) | sed -e s/\.apk$/-${architecture}\.apk/g)
    done
done
