#!/bin/bash

ROOT_DIR=`/bin/pwd`
DAEMONLIB_DIR=$(realpath $ROOT_DIR/../daemonlib)

if [ ! -d "$ROOT_DIR/../daemonlib" ]; then \
	echo "Could not find daemonlib. Please symlink daemonlib into src/brickd/ folder."; \
	exit 1; \
fi

if command -v docker >/dev/null 2>&1 ; then
	if [ $(/usr/bin/docker images -q build_environment_c) ]; then
		echo "Using docker image to build.";
		docker run \
		-v $ROOT_DIR/../:/$ROOT_DIR/../ -u $(id -u):$(id -g) \
		-v $DAEMONLIB_DIR/:$DAEMONLIB_DIR/: -u $(id -u):$(id -g) \
		build_environment_c /bin/bash \
		-c "cd $ROOT_DIR ; make "$@""; \
	else
		echo "No docker image found.";
	fi
else
	echo "Docker not found";
fi
