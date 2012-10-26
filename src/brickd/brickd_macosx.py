#!/usr/bin/env python
# -*- coding: utf-8 -*-  
"""
brickd (Brick Daemon)
Copyright (C) 2011-2012 Olaf Lüke <olaf@tinkerforge.com>
Copyright (C) 2012 Bastian Nordmeyer <bastian@tinkerforge.com>

brickd_macosx.py: Brick Daemon starting point for mac osx

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License 
as published by the Free Software Foundation; either version 2 
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public
License along with this program; if not, write to the
Free Software Foundation, Inc., 59 Temple Place - Suite 330,
Boston, MA 02111-1307, USA.
"""

import os
import sys
import signal
import logging

from usb_notifier import USBNotifier
from brick_protocol import BrickProtocolFactory, shutdown
import config
from threading import Thread

from ctypes import *
from ctypes.util import find_library

PIDFILE = '/var/run/brickd.pid'
LOGFILE = '/var/log/brickd.log'

logging.basicConfig(
    level = config.LOGGING_LEVEL, 
    format = config.LOGGING_FORMAT,
    datefmt = config.LOGGING_DATEFMT
)

def osx_notifier(brickd):
    notify_added = brickd.usb_notifier.notify_added
    notify_removed = brickd.usb_notifier.notify_removed

    # Find libs
    iokit = CDLL(find_library('IOKit'))
    foundation = CDLL(find_library('foundation'))

    # Define constants
    kIOUSBDeviceClassName = 'IOUSBDevice'
    kIOPublishNotification = 'IOServicePublish'
    kIOTerminatedNotification = 'IOServiceTerminate'
    kIOMasterPortDefault = c_void_p.in_dll(iokit, "kIOMasterPortDefault")
    kCFRunLoopDefaultMode = c_void_p.in_dll(iokit, "kCFRunLoopDefaultMode")

    # Define functions
    IOServiceMatching = iokit.IOServiceMatching
    IOServiceMatching.restype = c_void_p
    IOServiceMatching.argtypes = (c_char_p,)

    IONotificationPortCreate = iokit.IONotificationPortCreate
    IONotificationPortCreate.restype = c_void_p
    IONotificationPortCreate.argtype = (c_void_p,)

    IONotificationPortGetRunLoopSource = iokit.IONotificationPortGetRunLoopSource
    IONotificationPortGetRunLoopSource.restype = c_void_p
    IONotificationPortGetRunLoopSource.argtype = (c_void_p,)

    CFRunLoopGetCurrent = foundation.CFRunLoopGetCurrent
    CFRunLoopGetCurrent.restype = c_void_p
    CFRunLoopGetCurrent.argtype = None
    
    CFRunLoopAddSource = foundation.CFRunLoopAddSource
    CFRunLoopAddSource.restype = None
    CFRunLoopAddSource.argtype = (c_void_p, c_void_p, c_void_p)

    CFRetain = foundation.CFRetain
    CFRetain.restype = c_void_p
    CFRetain.argtype = (c_void_p,)

    IOServiceMatchingCallback = CFUNCTYPE(None, c_void_p, c_void_p)
    IOServiceAddMatchingNotification = iokit.IOServiceAddMatchingNotification
    IOServiceAddMatchingNotification.restype = c_int
    IOServiceAddMatchingNotification.argtype = (c_void_p, c_char_p, c_void_p, IOServiceMatchingCallback, c_void_p, c_void_p) 

    CFRunLoopRun = foundation.CFRunLoopRun
    CFRunLoopRun.restype = None
    CFRunLoopRun.argtype = None

    IOIteratorNext = iokit.IOIteratorNext
    IOIteratorNext.restype = c_void_p
    IOIteratorNext.argtype = (c_void_p,)

    IOObjectRelease = iokit.IOObjectRelease
    IOObjectRelease.restype = None
    IOObjectRelease.argtype = (c_void_p,)

    io_iterator_t = c_void_p

    # Make run loop
    matchingDict = c_void_p(IOServiceMatching(kIOUSBDeviceClassName))
    gNotifyPort = c_void_p(IONotificationPortCreate(kIOMasterPortDefault))
    runLoopSource = c_void_p(IONotificationPortGetRunLoopSource(gNotifyPort))
    CFRunLoopAddSource(c_void_p(CFRunLoopGetCurrent()), 
                       runLoopSource, 
                       kCFRunLoopDefaultMode)
    matchingdict = c_void_p(CFRetain(matchingDict))
    matchingdict = c_void_p(CFRetain(matchingDict))

    # removed/added callback
    def callback(refcon, iterator, func):
        func()
        usbDevice = c_void_p(IOIteratorNext(iterator))
        while usbDevice:
            IOObjectRelease(usbDevice)
            usbDevice = c_void_p(IOIteratorNext(iterator))


    af = IOServiceMatchingCallback(lambda a, b: callback(a, b, notify_added))
    rf = IOServiceMatchingCallback(lambda a, b: callback(a, b, notify_removed))

    # Add notifier
    iterator_add = io_iterator_t()
    iterator_remove = io_iterator_t()

    IOServiceAddMatchingNotification(gNotifyPort, 
                                     kIOPublishNotification, 
                                     matchingDict, 
                                     af, 
                                     None, 
                                     byref(iterator_add))
    callback(None, iterator_add, lambda: None)

    IOServiceAddMatchingNotification(gNotifyPort, 
                                     kIOTerminatedNotification, 
                                     matchingDict, 
                                     rf, None, 
                                     byref(iterator_remove))
    callback(None, iterator_remove, lambda: None)

    logging.info('OSX Notifier initialized')

    # Go
    CFRunLoopRun()

class BrickdMacOSX:
    def __init__(self, stdin='/dev/null', stdout=LOGFILE, stderr=LOGFILE):
        self.stdin = stdin
        self.stdout = stdout
        self.stderr = stderr

    def exit_brickd(self):
        logging.info("Received SIGINT or SIGTERM, shutting down.")
        shutdown(self.reactor)
        sys.exit()
        
    def start(self):
        logging.info("brickd {0} started".format(config.BRICKD_VERSION))

        from twisted.internet import reactor

        self.reactor = reactor
        signal.signal(signal.SIGINT, lambda s, f: self.exit_brickd)
        signal.signal(signal.SIGTERM, lambda s, f: self.exit_brickd)

        self.usb_notifier = USBNotifier()
        self.osx_notifier_thread = Thread(target=osx_notifier, args=(self,))
        self.osx_notifier_thread.daemon = True
        self.osx_notifier_thread.start()

        reactor.listenTCP(config.PORT, BrickProtocolFactory())
        try:
            reactor.run(installSignalHandlers = True)
        except KeyboardInterrupt:
            reactor.stop()

        logging.info("brickd {0} stopped".format(config.BRICKD_VERSION))
    
    def daemonize(self):
        # double fork doesn't work on Mac OS X, launching with launchctl after
        # the py2app building results in a crash...
        # we just use brickd as it is with logging from the daemon code

        # redirect standard file descriptors
        sys.stdout.flush()
        sys.stderr.flush()
        si = file(self.stdin, 'r')
        so = file(self.stdout, 'a+')
        se = file(self.stderr, 'a+', 0)
        os.dup2(si.fileno(), sys.stdin.fileno())
        os.dup2(so.fileno(), sys.stdout.fileno())
        os.dup2(se.fileno(), sys.stderr.fileno())

        self.start()

if __name__ == "__main__":
    if "--version" in sys.argv:
        print config.BRICKD_VERSION
    elif os.geteuid() != 0:
        sys.stderr.write("brickd has to be started as root, exiting\n")
    else:
        brickd = BrickdMacOSX()
        if "nodaemon" in sys.argv or "--no-daemon" in sys.argv:
            brickd.start()
        else:
            brickd.daemonize()
