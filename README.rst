Brick Daemon
============

This repository contains the source code of the Brick Daemon.

Compiling the Source Code
-------------------------

Brick Daemon uses the common Tinkerforge daemonlib::

 http://github.com/Tinkerforge/daemonlib

It has to be cloned or symlinked to the ``src/daemonlib`` directory before
the source code can be compiled.

Also the following libraries are required:

* libusb-1.0
* libudev (optional for USB hotplug, Linux only)
* pm-utils (optional for suspend/resume handling, Linux only)

On Debian based Linux distributions try::

 sudo apt-get install build-essential pkg-config libusb-1.0-0-dev libudev-dev pm-utils

On Fedora Linux try::

 sudo yum groupinstall "Development Tools"
 sudo yum install libusb1-devel libudev-devel pm-utils-devel

For Windows and Mac OS X a suitable pre-compiled libusb binary is part of this
repository.

Linux
^^^^^

A Makefile is provided to compile the source code using GCC and install the
result::

 cd src/brickd
 make
 sudo make install

Run the following commands to register brickd for autostart on Debian based
Linux distribution and start it::

 sudo update-rc.d brickd defaults
 sudo /etc/init.d/brickd start

Windows
^^^^^^^

A batch file ``compile.bat`` is provided to compile the source code using
the Visual Studio (MSVC) or Windows Driver Kit (WDK) compiler. Open a MSVC or
WDK command prompt::

 cd src\brickd
 compile.bat

The ``brickd.exe`` binary is created in ``src\brickd\dist``.

There is also a Makefile to compile the source code using MinGW::

 cd src\brickd
 mingw32-make

The ``brickd.exe`` binary is created in ``src\brickd\dist``.

Mac OS X
^^^^^^^^

A Makefile is provided to compile the source code using GCC::

 cd src/brickd
 make

The ``brickd`` binary is created in ``src/brickd``.

Building Packages
-----------------

Packages can be build for multiple platforms.

Linux, Windows and Mac OS X
^^^^^^^^^^^^^^^^^^^^^^^^^^^

The Python script ``src/brickd/build_pkg.py`` can build a Debian package for
Linux, a NSIS based ``setup.exe`` for Windows and a Disk Image for Mac OS X.
Run::

 python build_pkg.py

On Linux this has to be executed as ``root`` and on Windows this has to be
executed from a MSVC or WDK command prompt because it invokes the platform
specific commands to compile the source code.

The installer/package is created in ``src/brickd``.

OpenWrt
^^^^^^^

There is also a Makefile to build an OpenWrt package. To include the package
into your OpenWrt build simply link or copy the ``src/build_data/openwrt``
folder to the package directory of your OpenWrt build tree, select the
``brickd2`` package in the menuconfig and build.

Commandline Options
-------------------

Common:

* ``--help`` shows help and exits
* ``--version`` shows version number and exits
* ``--check-config`` checks config file for errors
* ``--debug`` sets all log levels to debug

Windows only:

* ``--install`` registers Brick Daemon as service and starts it
* ``--uninstall`` stops service and unregisters it
* ``--console`` forces start as console application
* ``--log-to-file`` writes log messages to file

USB Hotplug Detection
---------------------

Brick Daemon can autodetect USB hotplug. Different systems are used for this:

* libusb's own hotplug callbacks (if available)
* device notifications on Windows
* libudev on Linux
* IOKit notifications on Mac OS X

On Linux brickd will also check for added or removed Bricks if the SIGUSR1
signal is received. This is used on OpenWrt where the hotplug2 daemon is told
to tell brickd about USB hotplug this way, instead of using libudev on OpenWrt.
