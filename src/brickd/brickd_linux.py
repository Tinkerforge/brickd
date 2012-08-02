#!/usr/bin/env python
# -*- coding: utf-8 -*-  
"""
brickd (Brick Daemon)
Copyright (C) 2008-2011 Olaf Lüke <olaf@tinkerforge.com>

brickd_linux.py: Brick Daemon starting point for linux

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
import time

from usb_notifier import USBNotifier
from brick_protocol import BrickProtocolFactory, exit_brickd
import config

# logfile
LOG_FILENAME = '/var/log/brickd.log'
TF_DATA_DIR = os.getenv('TF_DATA_DIR')

if TF_DATA_DIR is not None and len(TF_DATA_DIR) > 0:
    LOG_FILENAME = os.path.join(TF_DATA_DIR, 'brickd.log')
elif os.getuid() != 0:
    XDG_DATA_DIR = os.getenv('XDG_DATA_DIR')

    if XDG_DATA_DIR is not None and len(XDG_DATA_DIR) > 0:
        LOG_FILENAME = os.path.join(XDG_DATA_DIR, 'brickd.log')
    else:
        LOG_FILENAME = os.path.expanduser('~/.brickd/brickd.log')

LOG_DIRNAME = os.path.dirname(LOG_FILENAME)

# pidfile
PID_FILENAME = '/var/run/brickd.pid'
TF_RUNTIME_DIR = os.getenv('TF_RUNTIME_DIR')

if TF_RUNTIME_DIR is not None and len(TF_RUNTIME_DIR) > 0:
    PID_FILENAME = os.path.join(TF_RUNTIME_DIR, 'brickd.pid')
elif os.getuid() != 0:
    XDG_RUNTIME_DIR = os.getenv('XDG_RUNTIME_DIR')

    if XDG_RUNTIME_DIR is not None and len(XDG_RUNTIME_DIR) > 0:
        PID_FILENAME = os.path.join(XDG_RUNTIME_DIR, 'brickd.pid')
    else:
        PID_FILENAME = os.path.expanduser('~/.brickd/brickd.pid')

PID_DIRNAME = os.path.dirname(PID_FILENAME)

# logging
logging.basicConfig(
    level = config.LOGGING_LEVEL, 
    format = config.LOGGING_FORMAT,
    datefmt = config.LOGGING_DATEFMT
)

# glib2reactor and gudev needed for USB hotplug
try:
    import gudev
    gudev_imported = True
except ImportError:
    logging.warn("Could not import gudev. Disabling USB hotplug")
    gudev_imported = False

try:
    from twisted.internet import glib2reactor
    glib2reactor.install()
    glib2reactor_installed = True
except ImportError:
    logging.warn("Could not install glib2reactor. Disabling USB hotplug.")
    glib2reactor_installed = False

from twisted.internet import reactor

class BrickdLinux:
    def __init__(self, stdin='/dev/null', stdout=LOG_FILENAME, stderr=LOG_FILENAME):
        if os.getuid() != 0:
            if not os.path.exists(LOG_DIRNAME):
                os.makedirs(LOG_DIRNAME)

            if not os.path.exists(PID_DIRNAME):
                os.makedirs(PID_DIRNAME)

        self.stdin = stdin
        self.stdout = stdout
        self.stderr = stderr
        
        if glib2reactor_installed:
            r = glib2reactor
        else:
            r = reactor
        
        signal.signal(signal.SIGINT, lambda s, f: exit_brickd(s, f, r)) 
        signal.signal(signal.SIGTERM, lambda s, f: exit_brickd(s, f, r)) 
        
    def start(self):
        logging.info("brickd {0} started".format(config.BRICKD_VERSION))
        
        self.usb_notifier = USBNotifier()
        
        if gudev_imported:
            self.gudev_client = gudev.Client(["usb"])
            self.gudev_client.connect("uevent", self.notify_udev)
            
        reactor.listenTCP(config.PORT, BrickProtocolFactory())
        try:
            reactor.run(installSignalHandlers = True)
        except KeyboardInterrupt:
            reactor.stop()

        logging.info("brickd {0} stopped".format(config.BRICKD_VERSION))
    
    def notify_udev(self, client, action, device):
        if action == "add":
            time.sleep(0.1) # Wait for changes to settle down in the system
            logging.info("New USB device")
            self.usb_notifier.notify_added()
        elif action == "remove":
            time.sleep(0.1) # Wait for changes to settle down in the system
            logging.info("Removed USB device")
            self.usb_notifier.notify_removed()

    # based on http://www.jejik.com/articles/2007/02/a_simple_unix_linux_daemon_in_python/
    def daemonize(self):   
        """
        do the UNIX double-fork magic, see Stevens' "Advanced 
        Programming in the UNIX Environment" for details (ISBN 0201563177)
        http://www.erlenstar.demon.co.uk/unix/faq_2.html#SEC16
        """
        try: 
            pid = os.fork() 
            if pid > 0:
                # exit first parent
                sys.exit(0) 
        except OSError, e: 
            sys.stderr.write("fork #1 failed: %d (%s)\n" % (e.errno, e.strerror))
            sys.exit(1)
    
        # decouple from parent environment
        os.chdir("/") 
        os.setsid() 
        os.umask(0) 

        # do second fork
        try:
            pid = os.fork() 
            if pid > 0:
                try:
                    open(PID_FILENAME, 'w').write("%d" % pid)
                except IOError, e:
                    sys.stderr.write("Could not write to pidfile %s: %s\n" % (PID_FILENAME, str(e)))
                    sys.exit(1)
                # exit from second parent
                sys.exit(0)
        except OSError, e:
            sys.stderr.write("fork #2 failed: %d (%s)\n" % (e.errno, e.strerror))
            sys.exit(1)

        try:
            open(LOG_FILENAME, 'a+').close()
        except IOError, e:
            sys.stderr.write("Could not open logfile %s: %s\n" % (LOG_FILENAME, str(e)))
            sys.exit(1)

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
    else:
        brickd = BrickdLinux()
        if "nodaemon" in sys.argv or "--no-daemon" in sys.argv:
            brickd.start()
        else:
            brickd.daemonize()
