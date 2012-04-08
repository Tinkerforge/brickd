#!/bin/bash
# plist nach /System/Library/LaunchDaemons
# programm nach /usr/libexec/brickd

cd $1
cp com.tinkerforge.brickd.plist /System/Library/LaunchDaemons/
cp -r brickd.app /usr/libexec/
launchctl load /System/Library/LaunchDaemons/com.tinkerforge.brickd.plist
