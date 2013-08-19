/*
 * brickd
 * Copyright (C) 2012-2013 Matthias Bolte <matthias@tinkerforge.com>
 *
 * stack.h: Stack specific functions
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

#ifndef BRICKD_STACK_H
#define BRICKD_STACK_H

#include <libusb.h>

#include "packet.h"
#include "utils.h"

#define USB_VENDOR_ID 0x16D0
#define USB_PRODUCT_ID 0x063D
#define USB_DEVICE_RELEASE ((1 << 8) | (1 << 4) | (0 << 0)) /* 1.10 */

#define USB_CONFIGURATION 1
#define USB_INTERFACE 0

#define USB_ENDPOINT_IN 4
#define USB_ENDPOINT_OUT 5

typedef struct {
	// USB device
	uint8_t bus_number;
	uint8_t device_address;
	libusb_context *context;
	libusb_device *device;
	struct libusb_device_descriptor device_descriptor;
	libusb_device_handle *device_handle;
	char product[64];
	char serial_number[64];
	Array read_transfers;
	Array write_transfers;

	// Stack
	Array uids; // always little endian
	Array write_queue;

	// used by usb_update
	int connected;
} Stack;

int stack_create(Stack *stack, uint8_t bus_number, uint8_t device_address);
void stack_destroy(Stack *stack);

int stack_add_uid(Stack *stack, uint32_t uid /* always little endian */);
int stack_knows_uid(Stack *stack, uint32_t uid /* always little endian */);

int stack_dispatch_packet(Stack *stack, Packet *packet, int force);

#endif // BRICKD_STACK_H
