/*
 * brickd
 * Copyright (C) 2012-2013 Matthias Bolte <matthias@tinkerforge.com>
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

#include <libusb.h>

// libusbx defines LIBUSB_CALL but libusb doesn't
#ifndef LIBUSB_CALL
	#define LIBUSB_CALL
#endif

#define USB_VENDOR_ID 0x16D0
#define USB_PRODUCT_ID 0x063D
#define USB_DEVICE_RELEASE ((1 << 8) | (1 << 4) | (0 << 0)) /* 1.10 */

#define USB_CONFIGURATION 1
#define USB_INTERFACE 0

#define USB_ENDPOINT_IN 4
#define USB_ENDPOINT_OUT 5

int usb_init(void);
void usb_exit(void);

int usb_has_hotplug(void);

int usb_update(void);

int usb_create_context(libusb_context **context);
void usb_destroy_context(libusb_context *context);

int usb_get_device_name(libusb_device_handle *device_handle, char *name, int length);

const char *usb_get_error_name(int error_code);

#endif // BRICKD_USB_H
