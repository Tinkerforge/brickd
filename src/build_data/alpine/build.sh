#!/bin/sh

rm -r packages
mkdir packages

docker image rm tinkerforge/build_brickd_alpine
docker build -t tinkerforge/build_brickd_alpine .

docker run --rm -v ${PWD}/brickd:/brickd -v ${PWD}/packages:/packages \
  tinkerforge/build_brickd_alpine /bin/sh -c \
  'chown user:user /brickd /packages; su user -c "cd /brickd; abuild -r; cp -r /home/user/packages/* /packages"'

docker image rm tinkerforge/build_brickd_alpine
