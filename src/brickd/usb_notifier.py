# -*- coding: utf-8 -*-  
"""
brickd (Brick Daemon)
Copyright (C) 2009-2011 Olaf Lüke <olaf@tinkerforge.com>

usbnotifier.py: Notifies if new usb device is added or removed

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

import logging
from libusb import usb1

from usb_device import USBDevice
import brick_protocol

class USBNotifier():
    USB_VENDOR_ID = 0x16D0
    USB_PRODUCT_ID = 0x063D

    def __init__(self):
        self.context = usb1.USBContext()
        self.active_devices = self.find_all_devices()
        
        for address in self.active_devices:
            USBDevice(address)
            
    def get_device_address(self, device):
        return (device.getBusNumber(), device.getDeviceAddress())

    def notify_added(self):
        for address in self.find_all_devices():
            if address not in self.active_devices:
                self.active_devices.append(address)
                USBDevice(address)
             
    def notify_removed(self):
        all_devices = self.find_all_devices()
        removed_devices = []

        for address in self.active_devices:
            if address not in all_devices:
                removed_devices.append(address)

        for address in removed_devices:
            self.active_devices.remove(address)
            for item in brick_protocol.device_dict.items():
                if item[1][0].address == address:
                    item[1][0].delete()
                    brick_protocol.device_dict.pop(item[0])

    def find_all_devices(self):
        """ 
        Finds and returns all supported USB devices as addresses.
        """

        devices = []
        try:
            for device in self.context.getDeviceList():
                if device.getVendorID() == USBNotifier.USB_VENDOR_ID and \
                   device.getProductID() == USBNotifier.USB_PRODUCT_ID:
                    devices.append(self.get_device_address(device))
        except:
            logging.exception("Could not enumerate USB devices")

        return devices
