Brick Daemon
============

This repository contains the source code of the Brick Daemon.

Usage
-----

You should be able to start brickd from source with the
``python brickd_linux.py``, ``python brickd_windows.py`` or
``python brickd_macosx.py`` scripts in the ``src/brickd/`` directory.

The following libraries are required:

* libusb-1.0
* python-twisted
* python-gudev (Linux only)
* pywin32 (Windows only)

On Debian based Linux distributions try::

 sudo apt-get install python-twisted python-gudev libusb-1.0-0

Building Packages
-----------------

The Python script ``src/brickd/build_pkg.py`` can build a Debian package for
Linux, a ``setup.exe`` for Windows and a Disk Image for Mac OS X. Try::

 python build_pkg.py linux

or::

 python build_pkg.py windows

or::

 python build_pkg.py macosx
