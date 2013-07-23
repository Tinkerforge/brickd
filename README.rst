Brick Daemon
============

This repository contains the source code of the Brick Daemon.

Compiling the Source
--------------------

The following libraries are required:

* libusb-1.0
* libudev (Linux only)

On Debian based Linux distributions try::

 sudo apt-get install build-essential libusb-1.0-0-dev libudev-dev

On Fedora Linux try::

 sudo yum groupinstall "Development Tools"
 sudo yum install libusb1-devel libudev-devel

For Windows a suitable pre-compiled libusb binary is part of this repository.
For Mac OS X a suitable libusb version can be obtained via MacPorts or Homebrew.

Linux
^^^^^

A Makefile is provided to compile the source code using GCC and install the
result::

 cd src/brickd
 make
 sudo make install

Run the following commands to register brickd for autostart on Debian based
Linux distribution and start it::

 update-rc.d brickd defaults
 /etc/init.d/brickd start

Windows
^^^^^^^

A batch file ``build_exe.bat`` is provided to compile the source code using
the Visual Studio (MSVC) or Windows Driver Kit (WDK) compiler. Open a MSVC or
WDK command prompt::

 cd src\brickd
 build_exe.bat

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

The Python script ``src/brickd/build_pkg.py`` can build a Debian package for
Linux, a NSIS based ``setup.exe`` for Windows and a Disk Image for Mac OS X.
Run::

 python build_pkg.py

On Linux this has to be executed as ``root`` and on Windows this has to be
executed from a MSVC or WDK command prompt because it invokes the platform
specific commands to compile the source code.

The installer/package is created in ``src/brickd``.

Commandline Options
-------------------

Common:

* ``--help`` shows help and exits
* ``--version`` shows version number and exits
* ``--check-config`` checks config file for errors
* ``--debug`` sets all log levels to debug

Windows only:

* ``--install`` registers Brick Daemon as service and start it
* ``--uninstall`` stops service and unregister it
* ``--console`` forces start as console application
* ``--log-to-file`` writes log messages to file
