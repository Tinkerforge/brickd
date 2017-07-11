/*
 * brickd
 * Copyright (C) 2013-2014, 2016 Matthias Bolte <matthias@tinkerforge.com>
 *
 * usb_stack.h: USB stack specific functions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef BRICKD_USB_STACK_H
#define BRICKD_USB_STACK_H

#include <libusb.h>
#include <stdbool.h>

#include <daemonlib/array.h>
#include <daemonlib/queue.h>
#include <daemonlib/timer.h>

#include "stack.h"

typedef struct {
	Stack base;

	uint8_t bus_number;
	uint8_t device_address;
	libusb_context *context;
	libusb_device_handle *device_handle;
	int interface_number;
	uint8_t endpoint_in;
	uint8_t endpoint_out;
	Timer stall_timer;
	Array read_transfers;
	Array write_transfers;
	Queue write_queue;
	uint32_t dropped_requests;
	bool connected;
	bool expecting_short_A1_response;
	bool expecting_read_stall_before_removal;
	bool expecting_disconnect;
} USBStack;

int usb_stack_create(USBStack *usb_stack, uint8_t bus_number, uint8_t device_address);
void usb_stack_destroy(USBStack *usb_stack);

void usb_stack_start_stall_timer(USBStack *usb_stack);

#endif // BRICKD_USB_STACK_H
