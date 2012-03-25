#!/bin/sh
rm Brickd.dmg
hdiutil create -fs HFS+ -volname Brickd -srcfolder macos_build Brickd.dmg

