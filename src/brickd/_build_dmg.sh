#!/bin/sh
dot_version=`grep BRICKD_VERSION config.py | sed -e 's/BRICKD_VERSION = "\(.*\)"/\1/'`
underscore_version=`printf %s $dot_version | sed -e 's/\./_/g'`
dmg=brickd_macos_$underscore_version.dmg
rm $dmg
hdiutil create -fs HFS+ -volname "Brickd $dot_version" -srcfolder macos_build $dmg
