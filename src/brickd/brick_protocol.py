# -*- coding: utf-8 -*-  
"""
brickd (Brick Daemon)
Copyright (C) 2011 Olaf Lüke <olaf@tinkerforge.com>

brick_protocol.py: Protocol implementation for bricks

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

from twisted.internet.protocol import Factory, Protocol

import logging
import struct
import sys

device_dict = {}

brick_protocol_list = []

def exit_brickd(signl, frme, reactor):
    logging.info("Received SIGINT or SIGTERM, shutting down.")
    for device in device_dict.values():
        device[0].delete()
        
    reactor.callFromThread(reactor.stop)
    sys.exit() 

def get_stack_id_from_data(data):
    return ord(data[0])

def get_length_from_data(data):
    return struct.unpack('<H', data[2:4])[0]

def get_type_from_data(data):
    return ord(data[1])

def get_callback_key_from_data(data):
    return data[0:2]

def get_usb_device_from_data(data):
    key = get_stack_id_from_data(data)
    if key in device_dict:
        return device_dict[key][0]
    
    return None

BASE58 = '123456789abcdefghijkmnopqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ'
def base58encode(value):
    encoded = ''
    while value >= 58:
        div, mod = divmod(value, 58)
        encoded = BASE58[mod] + encoded
        value = div
    encoded = BASE58[value] + encoded
    return encoded

class BrickProtocol(Protocol):
    def __init__(self):
        self.pending_data = ''

    def connectionMade(self):
        brick_protocol_list.append(self)
        logging.info("New socket connection: " + str(self))

    def connectionLost(self, reason):
        self.remove_connections()
        logging.info("Connection lost: " + str(self))

    def callback(self, data):
        logging.info("Callback: " + str(get_type_from_data(data)))
        self.transport.write(data)
        
    def add_connection(self, uid):
        for item in device_dict.items():
            if uid == item[1][1]:
                logging.info("Adding device " + 
                             base58encode(struct.unpack('<Q', uid)[0]))
                device_dict[item[0]][3].add(self.callback)
            
    def remove_connections(self):
        for item in device_dict.items():
            item[1][0].remove_read_callback(self.callback)
            try:
                item[1][3].remove(self.callback)
            except KeyError:
                pass
            
        brick_protocol_list.remove(self)
        
    def sanity_check(self, data):
        length = get_length_from_data(data)
        return length == len(data)
    
    def special_data_handling(self, data):
        stack_id = get_stack_id_from_data(data)
        type = get_type_from_data(data)
        if type == 255 and stack_id == 0:
            self.add_connection(data[4:12])
        
    def handle_broadcast(self, data):
        self.special_data_handling(data)
        usb_devices = set()
        for item in device_dict.items():
            usb_device = item[1][0]
            if usb_device not in usb_devices:
                usb_device.write_data_queue.put(data)
                usb_devices.add(usb_device)
        
    def dataReceived(self, data):
        self.pending_data += data

        while True:
            if len(self.pending_data) < 4:
                # wait for complete header
                return

            length = get_length_from_data(self.pending_data)

            if len(self.pending_data) < length:
                # wait for complete packet
                return

            packet = self.pending_data[0:length]
            self.pending_data = self.pending_data[length:]

            stack_id = get_stack_id_from_data(packet)
            if stack_id == 0:
                self.handle_broadcast(packet)
                continue

            usb_device = get_usb_device_from_data(packet)
            if usb_device == None:
                continue

            key = get_callback_key_from_data(packet)
            usb_device.add_read_callback(key, self.callback)

            usb_device.write_data_queue.put(packet)

class BrickProtocolFactory(Factory):
    protocol = BrickProtocol
