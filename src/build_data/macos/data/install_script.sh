#!/bin/bash
# plist nach /System/Library/LaunchDaemons
# programm nach /usr/libexec/brickd

cd $1
ls > /tmp/lala
cp com.tinkerforge.brickd.plist /System/Library/LaunchDaemons/
cp -r brickd.app /usr/libexec/
launchctl load /System/Library/LaunchDaemons/com.tinkerforge.brickd.plist
