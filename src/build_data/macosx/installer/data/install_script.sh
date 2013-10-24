#!/bin/bash

# uninstall old
if test -d /usr/libexec/brickd.app
then
	launchctl unload /System/Library/LaunchDaemons/com.tinkerforge.brickd.plist
	rm /System/Library/LaunchDaemons/com.tinkerforge.brickd.plist
	rm -rf /usr/libexec/brickd.app
	rm -f /etc/brickd.conf
fi

# install new
cd $1
cp com.tinkerforge.brickd.plist /System/Library/LaunchDaemons/
cp -r brickd.app /usr/libexec/
cp brickd.conf /etc/
launchctl load /System/Library/LaunchDaemons/com.tinkerforge.brickd.plist
launchctl start com.tinkerforge.brickd
