/*
 * brickd
 * Copyright (C) 2012-2014, 2016-2019 Matthias Bolte <matthias@tinkerforge.com>
 *
 * usb_transfer.h: libusb transfer specific functions
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

#ifndef BRICKD_USB_TRANSFER_H
#define BRICKD_USB_TRANSFER_H

#include <libusb.h>
#include <stdbool.h>

#include <daemonlib/packet.h>

#include "usb_stack.h"

typedef enum {
	USB_TRANSFER_TYPE_READ = 0,
	USB_TRANSFER_TYPE_WRITE
} USBTransferType;

typedef struct _USBTransfer USBTransfer;

typedef void (*USBTransferFunction)(USBTransfer *usb_transfer);

struct _USBTransfer {
	USBStack *usb_stack;
	USBTransferType type;
	bool submitted;
	bool cancelled;
	USBTransferFunction function;
	struct libusb_transfer *handle;
	union {
		uint8_t packet_buffer[1024];
		Packet packet;
	};
	uint32_t submission;
};

int usb_transfer_create(USBTransfer *usb_transfer, USBStack *usb_stack,
                        USBTransferType type, USBTransferFunction function);
void usb_transfer_destroy(USBTransfer *usb_transfer);

int usb_transfer_submit(USBTransfer *usb_transfer);

#endif // BRICKD_USB_TRANSFER_H
