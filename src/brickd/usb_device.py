# -*- coding: utf-8 -*-  
"""
brickd (Brick Daemon)
Copyright (C) 2009-2010 Olaf Lüke <olaf@tinkerforge.com>

usb_device.py: Implementation of USB communication

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
import struct
from libusb import libusb1
from collections import deque
from threading import Thread

# Queue for python 2, queue for python 3
try:
    from Queue import Queue
except ImportError:
    from queue import Queue

import brick_protocol

import twisted
import time

class USBDevice:
    USB_ENDPOINT_SIZE = 64
    USB_BUFFER_SIZE = 4096
    
    USB_CONFIGURATION = 1
    USB_INTERFACE = 0
    USB_ENDPOINT_IN = 4
    USB_ENDPOINT_OUT = 5
    
    NUM_READ_TRANSFER = 5
    NUM_WRITE_TRANSFER = 5
    
    TYPE_GET_STACK_ID = 255
    TYPE_ENUMERATE = 254
    TYPE_ENUMERATE_CALLBACK = 253
    TYPE_STACK_ENUMERATE = 252
    TYPE_ADC_CALIBRATE = 251
    TYPE_GET_ADC_CALIBRATION = 250
    
    MAX_CONCURRENT_REQUESTS = 25
    
    def __init__(self, usb_device, context):
        self.usb_device = usb_device
        self.context = context

        try:
            # open the device for communication
            self.usb_handle = self.usb_device.open()
            self.usb_handle.resetDevice()
            
            # claim configuration and interface
            self.usb_handle.setConfiguration(USBDevice.USB_CONFIGURATION)
            self.usb_handle.claimInterface(USBDevice.USB_INTERFACE)

            self.data_callback = {}
            
            self.routing_table_in = []
            self.routing_table_out = []
            for i in range(256):
                self.routing_table_in.append(chr(i))
                self.routing_table_out.append(chr(i))

            self.read_transfers = []
            self.write_transfers = []

            self.write_data_queue = Queue()
            self.write_transfer_queue = Queue(USBDevice.NUM_WRITE_TRANSFER)
            
            for i in xrange(USBDevice.NUM_READ_TRANSFER):
                self.add_read_transfer()
            for i in xrange(USBDevice.NUM_WRITE_TRANSFER):
                self.add_write_transfer()
                
            self.alive = True
            self.deleted = False
            self.write_loop_thread = Thread(target=self.write_loop)
            self.write_loop_thread.daemon = True
            self.write_loop_thread.start()
            
            self.event_loop_thread = Thread(target=self.event_loop)
            self.event_loop_thread.daemon = True
            self.event_loop_thread.start()
            
            self.t = time.time()
            self.t_sum = 0
            self.i = 0
            
            self.get_devices()
        except:
            self.alive = False
            logging.warning("Could not create USB Device")
        
    def add_read_transfer(self):
        logging.info("Adding read transfer")
        transfer = self.usb_handle.getTransfer()
        transfer.setBulk(USBDevice.USB_ENDPOINT_IN + 0x80, 
                         4096, 
                         self.read_callback)
        transfer.submit()
        self.read_transfers.append(transfer)
        
    def add_write_transfer(self):
        logging.info("Adding write transfer")
        transfer = self.usb_handle.getTransfer()
        self.write_transfer_queue.put(transfer)
        self.write_transfers.append(transfer)
        
    def delete(self):
        if not self.deleted:
            logging.info("Deleting USB device")
            for item in brick_protocol.device_dict.items():
                if item[1][0] != self:
                    continue
                
                data =  chr(0)                   # Stack ID (broadcast)
                data += chr(253)                 # Enumerate Type
                data += struct.pack('<H', 54)    # Length
                data += item[1][1]               # UID 
                data += struct.pack('<40s', item[1][2]) # Name
                data += chr(item[0])             # Device Stack ID
                data += struct.pack('<?', False) # Denumerate
                
                for bp in brick_protocol.brick_protocol_list:
                    twisted.internet.reactor.callFromThread(bp.callback, data)
                    
        self.alive = False
        self.deleted = True

        try:
            self.write_data_queue.put(None, False)
            self.write_transfer_queue.put(None, False)
        except:
            pass

        self.write_loop_thread.join()
        self.event_loop_thread.join()

        # Cancel pending USBTransfers and close all USBTransfers
        for transfer in self.read_transfers + self.write_transfers:
            if transfer.isSubmitted():
                transfer.cancel()
            transfer.close()

        self.usb_handle.close()

    def add_read_callback(self, key, callback):
        if key in self.data_callback:
            logging.info("Add callback for message: " + 
                         str(brick_protocol.get_type_from_data(key)))
            # We actually don't know if a function does return something,
            # so we always wait for a return value. Thus we need a maximum
            # size for the callback queue, otherwise this would be a memory
            # leak. This could be prevented by adding another byte to the
            # protocol. On a PC a small queue for each setter does not matter.
            # But as soon as we have a WIFI Extension, this might actually
            # be a problem. If there is not enough flash on the Master Brick
            # to implement this we will have to change the protocol.
            # This will be an absolute pain in the ass :-).
            if len(self.data_callback[key]) < USBDevice.MAX_CONCURRENT_REQUESTS:
                self.data_callback[key].append(callback)
        else:
            logging.info("Add queue and callback for message: " + 
                         str(brick_protocol.get_type_from_data(key)))
            self.data_callback[key] = deque([callback])

    def remove_read_callback(self, callback):
        for item in self.data_callback.items():
            while True:
                try:
                    item[1].remove(callback)
                    logging.info("Remove callback for message: " +
                                 str(brick_protocol.get_type_from_data(item[0])))
                except ValueError:
                    break

    def write_callback(self, transfer):
        if not self.alive:
            return
        
        status = transfer.getStatus()
        if status == libusb1.LIBUSB_TRANSFER_COMPLETED:
            logging.info("Write callback length: " + 
                         str(transfer.getActualLength()))
        else:
            # TODO: Better error handling
            logging.warn("Write callback not successful (status " + 
                         str(status) + 
                         "): Probably disconnect")
            self.alive = False
        self.write_transfer_queue.put(transfer)
        
    def extend_routing_table(self, old):
        new = self.find_unused_stack_id()
        if new != -1:
            self.routing_table_in[old] = chr(new)
            self.routing_table_out[new] = chr(old)
        else:
            logging.error("You are trying to use more then 255 " + \
                          "devices, this is not possible.")   
            
    def apply_routing_table_out(self, data):
        return self.apply_routing_table(self.routing_table_out, data)
            
    def apply_routing_table_in(self, data):
        stack_id = brick_protocol.get_stack_id_from_data(data)
        type = brick_protocol.get_type_from_data(data)
        if stack_id == 0:
            if type == USBDevice.TYPE_ENUMERATE_CALLBACK:
                old_stack_id = ord(self.routing_table_in[ord(data[52])])
                if old_stack_id in brick_protocol.device_dict:
                    uid = data[4:12]
                    if brick_protocol.device_dict[old_stack_id][1] != uid:
                        self.extend_routing_table(old_stack_id)
                    
                return data[0:52] + \
                       self.routing_table_in[old_stack_id] + \
                       data[53:]
                       
            elif type == USBDevice.TYPE_GET_STACK_ID:
                old_stack_id = ord(data[55])
                return data[0:55] + self.routing_table_in[old_stack_id]
            else:
                return data
        else:
            return self.apply_routing_table(self.routing_table_in, data)
    
    def apply_routing_table(self, rt, data):
        return rt[ord(data[0])] + data[1:]
        
    def read_callback(self, transfer):
        if not self.alive:
            return
        
        status = transfer.getStatus()
        if status == libusb1.LIBUSB_TRANSFER_COMPLETED:
            data = transfer.getBuffer()
            
            data = data[0:brick_protocol.get_length_from_data(data)]
            
            # Apply routing table
            data = self.apply_routing_table_in(data)
            
            stack_id = brick_protocol.get_stack_id_from_data(data)
            type = brick_protocol.get_type_from_data(data)
            if stack_id == 0 and type == USBDevice.TYPE_ENUMERATE_CALLBACK:
                self.new_device(data)
                
            key = brick_protocol.get_callback_key_from_data(data);
            
            # Data is return value of function call
            if key in self.data_callback:
                try:
                    callback = self.data_callback[key].popleft()
                except:
                    logging.warn("No callback for data. Latency too high?")
                else:
                    twisted.internet.reactor.callFromThread(callback, data)
                    
            # Data is Signal or Broadcast Message
            else:
                # Stack ID = 0 -> Broadcast Message
                if stack_id == 0:
                    for bp in brick_protocol.brick_protocol_list:
                        callback = bp.callback
                        twisted.internet.reactor.callFromThread(callback, data)
                elif stack_id in brick_protocol.device_dict:
                    callbacks = brick_protocol.device_dict[stack_id][3]
                    for callback in callbacks:
                        twisted.internet.reactor.callFromThread(callback, data)
                else:
                    logging.warn("Read callback with unknown Stack ID: " + 
                                 str(stack_id))
            
            logging.info("Read callback: " + str(type))
        else:
            # TODO: Better error handling
            logging.warn("Read callback not successful (status " + 
                         str(status) + 
                         "): Probably disconnect")
            self.alive = False
            
        try:
            transfer.submit()
        except libusb1.USBError:
            logging.warn("Transfer exception: Probably disconnect")
            self.alive = False
    
    def write_loop(self):
        """
        Write data from queue to usb device
        """ 

        try:
            while self.alive:
                transfer = self.write_transfer_queue.get()
                if not transfer: 
                    if not self.alive:
                        return
                    continue

                data = self.write_data_queue.get()
                if not data:
                    if not self.alive:
                        return
                    continue

                # Apply routing table
                data = self.apply_routing_table_out(data)
                logging.info("Write to device: " + 
                             str(brick_protocol.get_type_from_data(data)))
                
                transfer.setBulk(USBDevice.USB_ENDPOINT_OUT, 
                                 data,
                                 self.write_callback)
                transfer.submit()
                self.write_data_queue.task_done()
                self.write_transfer_queue.task_done()
        except:
            self.alive = False
    
    def event_loop(self):
        """
        Triggers libusb events
        """
        
        while self.alive:
            self.context.handleEvents()

    def get_devices(self):
        logging.info("Calling get_devices on: " + str(self))
        data = ''.join(map(chr, [0, USBDevice.TYPE_ENUMERATE, 4, 0]))
        self.write_data_queue.put(data)
        
    def find_unused_stack_id(self):
        for stack_id in range(1, 255):
            if not stack_id in brick_protocol.device_dict:
                return stack_id
            
        return -1

    def new_device(self, data):
        stack_id = ord(data[52])
        if not stack_id in brick_protocol.device_dict:
            name = data[12:52].replace(chr(0), '')
            uid = data[4:12]
            brick_protocol.device_dict[stack_id] = (self, uid, name, set())
            logging.info("New device {0} with Stack ID {1} ".format(name, 
                                                                    stack_id))
            
