Brick Daemon
============

This repository contains the source code of the Brick Daemon. Prebuild
installers/packages for various platforms are provided here::

 https://www.tinkerforge.com/en/doc/Downloads.html

Compiling the Source Code
-------------------------

Instead of using the prebuild installers/packages, Brick Daemon can also be
compiled from source code for various platforms.

Brick Daemon uses the Tinkerforge daemonlib::

 https://github.com/Tinkerforge/daemonlib

It has to be cloned or symlinked to the ``src/daemonlib/`` directory before
the source code can be compiled. Make sure that you're using matching versions
of the brickd and daemonlib source code. If you're using the current git
version of brickd then you also need the current git version of daemonlib. If
you're using a specific release of brickd (e.g. tagged v2.3.0) then you also
need the matching release of daemonlib (e.g. tagged brickd-2.3.0).

Brick Daemon also depends on the following libraries:

* libusb-1.0 (mandatory)
* libudev (optional for USB hotplug, Linux only)
* pm-utils (optional for suspend/resume handling, Linux without systemd only)

On Debian based Linux distributions try::

 sudo apt-get install build-essential pkg-config libusb-1.0-0-dev libudev-dev pm-utils

On Fedora Linux try::

 sudo yum groupinstall "Development Tools"
 sudo yum install libusb1-devel libudev-devel pm-utils-devel

For Windows and macOS a suitable pre-compiled libusb-1.0 binary is part of this
repository.

Linux
^^^^^

A Makefile is provided to compile the source code using GCC and install the
result. The Makefile will autodetect the availability of libudev and pm-utils::

 cd src/brickd
 make
 sudo make install

Run the following commands to register brickd for autostart on Debian based
Linux distribution and start it::

 sudo systemctl enable brickd
 sudo systemctl enable brickd-resume
 sudo systemctl start brickd

On Debian based Linux distribution without systemd run the following commands
instead::

 sudo update-rc.d brickd defaults
 sudo invoke-rc.d brickd start

Windows
^^^^^^^

A batch file ``compile.bat`` is provided to compile the source code using
the Visual Studio (MSVC) or Windows Driver Kit (WDK) compiler. Open a MSVC or
WDK command prompt::

 cd src\brickd
 compile.bat

The ``brickd.exe`` binary is created in ``src\dist\``.

For the MinGW compiler there is a Makefile to compile the source code::

 cd src\brickd
 mingw32-make

The ``brickd.exe`` binary is created in ``src\dist\``.

Alternatively, there is a Visual Studio 2017 project file to compile the
source code::

 src\build_data\windows\msvc\brickd.sln

Windows 10 IoT (Universal Windows Platform)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

A Visual Studio 2017 project file to compile the source code::

 src\build_data\windows\msvc_uwp\brickd_uwp.sln

There is a currently unsolved problem with USB hotplug detection::

 https://www.tinkerforge.com/en/blog/brick-daemon-beta-for-windows-10-iot-core-part-12/

macOS
^^^^^

A Makefile is provided to compile the source code using GCC::

 cd src/brickd
 make

The ``brickd`` binary is created in ``src/brickd/``.

Building Packages
-----------------

Packages can be build for multiple platforms.

Linux, Windows and macOS
^^^^^^^^^^^^^^^^^^^^^^^^

The Python script ``src/build_pkg.py`` can build a Debian package for
Linux, a NSIS based ``setup.exe`` for Windows and a Disk Image for macOS.
Run::

 python build_pkg.py

On Linux this has to be executed as ``root`` and on Windows this has to be
executed from a MSVC or WDK command prompt because it invokes the platform
specific commands to compile the source code.

The installer/package is created in ``src``.

OpenWrt
^^^^^^^

There is also a Makefile to build an OpenWrt package. To include the package
into your OpenWrt build simply link or copy the ``src/build_data/openwrt/``
folder to the package directory of your OpenWrt build tree, select the
``brickd2`` package in the menuconfig and build.

USB Hotplug Detection
---------------------

Brick Daemon can autodetect USB hotplug. Different systems are used for this:

* libusb's own hotplug callbacks (if available)
* device notifications on Windows
* libudev on Linux
* IOKit notifications on macOS

On Linux brickd will also check for added or removed Bricks if the SIGUSR1
signal is received. This is used on OpenWrt where the hotplug2 daemon is told
to tell brickd about USB hotplug this way, instead of using libudev on OpenWrt.
