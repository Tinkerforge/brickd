Tinkerforge Brick Daemon
========================

The Brick Daemon (brickd.exe) acts as a proxy between the Bricks/Bricklets and
the API bindings for the different programming languages.

The installer registers and starts brickd.exe as a Windows Service. It runs
in the background and normally you should not see anything of it or have to
deal with it at all.

Drivers
-------

The drivers subfolder contains the bootloader driver and the Brick driver. The
installer will install both by default for Windows versions that need them. So
Windows should be able to use them automatically if you connect a Brick to USB.
Normally you should not have to install them manually.

The bootloader driver is needed for updating the firmware of a Brick on Windows
Vista and older Windows versions. In bootloader mode a Brick emulates a serial
port, the bootloader driver tells Windows how to handle this emulated port.
Windows 7 and newer versions don't need this driver. They automatically detect
the emulated serial port as a device called "GPS Camera Detect". This is okay.
Just select this serial port in Brick Viewer when updating a firmware.

The Brick driver if needed on Windows 7 and older Windows versions. Without
this driver Brick Daemon cannot find a Brick and it won't show up in Brick
Viewer. Windows 8 and newer versions doesn't need this driver. They are able
to automatically handle a Brick correctly on their own.

Commandline Options
-------------------

The Brick Daemon understands several commandline options, they are mostly for
debugging. You should not have to fiddle with them in the common case. Just let
the installer register and start brickd.exe as a Windows Service.

--help           shows help and exits
--version        shows version number and exits
--check-config   checks config file for errors
--debug          sets all log levels to debug
--install        registers Brick Daemon as service and starts it
--uninstall      stops service and unregister it
--console        forces start as console application
--log-to-file    writes log messages to a file

If brickd.exe is running as a service you can use the Services section in the
Computer Management to pass the commandline option --debug and --log-to-file
via the Start Parameters option. The other options are only valid if brickd.exe
is started from a command prompt.

To start brickd.exe from a command prompt you should stop it running as Windows
Service first, because there can only be one brickd.exe running at the same
time. You can also start brickd.exe by a double click from the Explorer. Brick
Daemon detects this and does the right thing automatically.

Debugging
---------

By default Brick Daemon writes error and warning messages to the Windows Event
Log. You can use eventlog.exe to browse them. It shows only the Brick Daemon
log entires from the Windows Event Log.

You can also use the --log-to-file option, then Brick Daemon will write a log
file named brickd.log in the folder where brickd.exe is located. In this mode
error, warning and information messages are written to the log by default. You
an use the --debug option to include debug messages as well.

If started from a command prompt and without the --log-to-file option log
messages are written to the command prompt window.

Config File
-----------

There is a config file named brickd.ini that allows to change the default
network bind address and port and control in more detail what messages are
written to the log output. See brickd.ini for more details.
