Brick Daemon
============

This repository contains the source code of the Brick Daemon.

Usage
-----

You should be able to start brickd from source with the 
"python brickd_linux.py", "python brickd_windows.py" or 
"python brickd_macos.py" scripts in the src/brickd/ directory.

The following libraries are required:
 * twisted
 * libusb 1.0
 * python-gudev (linux only)
 * pywin32 (windows only)

On Debian based linux distributions try::
 sudo apt-get install python-twisted python-gudev libusb-1.0-0

Building Packages
-----------------
The python script src/brickd/build_pkg.py can build a Debian package and a
setup.exe for Windows. Try::

 python build_pkg.py linux

or::

 python build_pkg.py windows
