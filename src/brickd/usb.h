/*
 * brickd
 * Copyright (C) 2012-2014, 2016-2017 Matthias Bolte <matthias@tinkerforge.com>
 *
 * usb.h: USB specific functions
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

#ifndef BRICKD_USB_H
#define BRICKD_USB_H

#include <stdbool.h>
#include <libusb.h>

#include "usb_stack.h"

// newer libusb defines LIBUSB_CALL but older libusb doesn't
#ifndef LIBUSB_CALL
	#define LIBUSB_CALL
#endif

#define USB_BRICK_VENDOR_ID 0x16D0
#define USB_BRICK_PRODUCT_ID 0x063D
#define USB_BRICK_DEVICE_RELEASE ((1 << 8) | (1 << 4) | (0 << 0)) /* 1.10 */
#define USB_BRICK_INTERFACE 0

#define USB_RED_BRICK_VENDOR_ID 0x16D0
#define USB_RED_BRICK_PRODUCT_ID 0x09E5
#define USB_RED_BRICK_DEVICE_RELEASE ((1 << 8) | (1 << 4) | (0 << 0)) /* 1.10 */
#define USB_RED_BRICK_INTERFACE 0

int usb_init(void);
void usb_exit(void);

bool usb_has_hotplug(void);

int usb_rescan(void);
int usb_reopen(USBStack *usb_stack);

int usb_create_context(libusb_context **context);
void usb_destroy_context(libusb_context *context);

int usb_get_interface_endpoints(libusb_device_handle *device_handle, int interface_number,
                                uint8_t *endpoint_in, uint8_t *endpoint_out);

int usb_get_device_name(libusb_device_handle *device_handle, char *name, int length);

const char *usb_get_error_name(int error_code);

#endif // BRICKD_USB_H
