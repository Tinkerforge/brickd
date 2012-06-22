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
        self.context = usb1.LibUSBContext()

        self.active_devices = self.find_all_devices()
        
        for device in self.active_devices:
            USBDevice(device, self.context)
            
    def get_addr(self, device):
        return (device.getBusNumber(), device.getDeviceAddress())

    def notify_added(self):
        new = self.find_all_devices()
        addr = {}
        for device in new:
            addr[self.get_addr(device)] = device
        
        for device in self.active_devices:
            if self.get_addr(device) in addr:
                addr.pop(self.get_addr(device))
                
        for device in addr.values():
            if device not in self.active_devices:
                self.active_devices.append(device)
                USBDevice(device, self.context)
             
    def notify_removed(self):
        new = self.find_all_devices()
        addr = self.device_addresses()

        for device in new:
            if self.get_addr(device) in addr:
                addr.remove(self.get_addr(device))
                
        for device in self.device_by_addr(addr):
            self.active_devices.remove(device)
            for item in brick_protocol.device_dict.items():
                if item[1][0].usb_device is device:
                    item[1][0].delete()
                    brick_protocol.device_dict.pop(item[0])

    def find_all_devices(self):
        """ 
        Finds and returns all supported usb devices.
        """

        tries = 2
        while tries > 0:
            devices = []
            try:
                for device in self.context.getDeviceList():
                    if device.getVendorID() == USBNotifier.USB_VENDOR_ID and \
                       device.getProductID() == USBNotifier.USB_PRODUCT_ID:
                        devices.append(device)
                        
                tries = 0
            except:
                logging.warn("USB context broken, trying to fix it")
                self.context = usb1.LibUSBContext()
                devices = []
                tries -= 1
            
        return devices
    
    def device_addresses(self):
        """ 
        Returns addresses from all devices.
        """
        
        return [self.get_addr(dev) for dev in self.active_devices]
    
    def device_by_addr(self, addr):
        """ 
        Finds and returns device by address.
        """
        
        if not addr.__class__ == list:
            addr = [addr]
            
        devices = []
        for device in self.active_devices:
            if self.get_addr(device) in addr:
                devices.append(device)
                
        return devices
    
