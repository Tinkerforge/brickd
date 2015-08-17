#!/bin/bash

# Mac OS X 10.11 added "System Integrity Protection" that basically makes
# /System, /bin, /sbin and /usr unwritable even for root. Recommended places
# to put files are now [~]/Library, /usr/local and /Applications

MINOR_VERSION=$(sw_vers -productVersion | awk -F '.' '{print $2}')

if [ ${MINOR_VERSION} -lt 11 ]
then
LIBEXEC_DIR=/usr/libexec
LIBRARY_DIR=/System/Library
PLIST_VERSION=v10
else
LIBEXEC_DIR=/usr/local/libexec
LIBRARY_DIR=/Library
PLIST_VERSION=v11
fi

# uninstall old
if [ -d ${LIBEXEC_DIR}/brickd.app ]
then
launchctl unload ${LIBRARY_DIR}/LaunchDaemons/com.tinkerforge.brickd.plist
rm ${LIBRARY_DIR}/LaunchDaemons/com.tinkerforge.brickd.plist
rm -rf ${LIBEXEC_DIR}/brickd.app
rm -f /etc/brickd.conf
fi

# install new
cd $1
cp com.tinkerforge.brickd.plist.${PLIST_VERSION} ${LIBRARY_DIR}/LaunchDaemons/com.tinkerforge.brickd.plist
[ ! -d ${LIBEXEC_DIR}/ ] && mkdir -m 0755 -p ${LIBEXEC_DIR}/
cp -r brickd.app ${LIBEXEC_DIR}/
cp brickd.conf /etc/
launchctl load ${LIBRARY_DIR}/LaunchDaemons/com.tinkerforge.brickd.plist
launchctl start com.tinkerforge.brickd
