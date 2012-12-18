#!/bin/sh
dot_version=`./brickd --version | sed -e 's/ /-/g'`
underscore_version=`./brickd --version | sed -e 's/[\. ]/_/g'`
dmg=brickd_macos_$underscore_version.dmg
rm $dmg
hdiutil create -fs HFS+ -volname "Brickd-$dot_version" -srcfolder dist $dmg
