Brick Daemon
============

This repository contains the source code of the Brick Daemon. Prebuild
installers/packages for various platforms are provided here::

 https://www.tinkerforge.com/en/doc/Downloads.html

Supported Platforms
-------------------

* Linux with libusb 1.0.20 or newer
* Windows Vista or newer
* macOS 10.9 (Mavericks) or newer

Using the Tinkerforge APT Repository
------------------------------------

We provide prebuild `.deb` packages for Debian based Linux distributions.

Follow the steps in the readme for your distribution to set up the repository:

* Debian::

   https://download.tinkerforge.com/apt/debian/readme.txt

* Ubuntu::

   https://download.tinkerforge.com/apt/ubuntu/readme.txt

* Raspberry Pi OS (Raspbian)::

   https://download.tinkerforge.com/apt/raspbian/readme.txt

Install Brick Daemon package::

 sudo apt install brickd

Compiling the Source Code (instead of using prebuild installer/package)
-----------------------------------------------------------------------

Instead of using the prebuild installer/package, Brick Daemon can also be
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
* pm-utils (optional for suspend/resume handling, Linux without systemd only)

On Debian based Linux distributions try::

 sudo apt-get install build-essential pkg-config libusb-1.0-0-dev pm-utils

On Fedora Linux try::

 sudo yum groupinstall "Development Tools"
 sudo yum install libusb1-devel pm-utils-devel

For Windows and macOS a suitable pre-compiled libusb-1.0 binary is part of this
repository.

Linux
^^^^^

A Makefile is provided to compile the source code using GCC and install the
result. The Makefile will autodetect the availability of pm-utils::

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
the Visual Studio (MSVC) compiler. Open a MSVC command prompt::

 cd src\brickd
 compile.bat

The ``brickd.exe`` binary is created in ``src\dist\``.

For the MinGW compiler there is a Makefile to compile the source code::

 cd src\brickd
 mingw32-make

The ``brickd.exe`` binary is created in ``src\dist\``.

Alternatively, there is a Visual Studio 2019 project file to compile the
source code::

 src\build_data\windows\msvc\brickd.sln

Windows 10 IoT Core (Universal Windows Platform)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

There is a Visual Studio 2019 project file to compile the source code::

 src\build_data\windows\msvc_uwp\brickd_uwp.sln

The "Windows IoT Core Project Templates" extension has to be installed first.
The project file builds a UWP app that has been successfully tested on a
Raspberry Pi running Windows 10 IoT Core with Bricks connected to USB. But
there is a currently unsolved problem with USB device detection::

 https://www.tinkerforge.com/en/blog/2016/7/12/brick-daemon-beta-fuer-windows-10-iot-core-teil-1-2/

TL;DR: There seems to be a bug in Windows 10 IoT Core that stops Bricks from
being properly detected as USB devices. Because of this bug Brick Daemon cannot
access them out-of-the-box.

You have to run the following command on your Raspberry Pi, while replacing the
placeholder ``<UID>`` in the command with the UID of the Brick you want to
connect::

 reg add "HKLM\System\CurrentControlSet\Enum\USB\VID_16D0&PID_063D\<UID>\Device Parameters" /v DeviceInterfaceGUIDs /t REG_MULTI_SZ /d "{870013DD-FB1D-4BD7-A96C-1F0B7D31AF41}"

This has to be done for every Brick that you want to connect to the Raspberry Pi.

There is also experimental support for the HAT (Zero) Brick for the Raspberry Pi.
It's experimental because Windows 10 IoT Core doesn't provide HAT detection for
the Raspberry Pi and it also doesn't allow to access the I2C interface used for
HAT detection on Raspbian to solve this in Brick Daemon itself.

This means that Brick Daemon cannot detect if a HAT is connected or which kind
of HAT is connected. Therefore, HAT (Zero) Brick support cannot be enabled by
default and one of the preprocessor define ``BRICKD_WITH_UWP_HAT_BRICK`` or
``BRICKD_WITH_UWP_HAT_ZERO_BRICK`` has to be defined in the Visual Studio 2019
project file to enable HAT (Zero) Brick support.

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

On Windows this has to be executed from a MSVC or WDK command prompt because it
invokes the platform specific commands to compile the source code.

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

* libusb hotplug callback on non-Windows systems
* Win32 device notification on Windows systems

On Linux brickd will also check for added or removed Bricks if the SIGUSR1
signal is received. This is used on OpenWrt where the hotplug2 daemon is told
to tell brickd about USB hotplug this way, instead of using libudev on OpenWrt.
