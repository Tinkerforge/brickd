#!/bin/sh -e
echo "creating disk image"
./brickd --version > /dev/null
dot_version=`./brickd --version | sed -e 's/ /-/g'`
underscore_version=`./brickd --version | sed -e 's/[\. ]/_/g'`
dmg=brickd_macos_$underscore_version.dmg
if [ -f $dmg ]
then
    rm $dmg
fi
hdiutil create -fs HFS+ -volname "Brickd-$dot_version" -srcfolder dist $dmg
