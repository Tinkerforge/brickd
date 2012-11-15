/*
 * brickd
 * Copyright (C) 2012 Matthias Bolte <matthias@tinkerforge.com>
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

#include "packet.h"

// libusbx defines LIBUSB_CALL but libusb doesn't
#ifndef LIBUSB_CALL
	#define LIBUSB_CALL
#endif

int usb_init(void);
void usb_exit(void);

int usb_update(void);

void usb_dispatch_packet(Packet *packet);

int usb_create_context(libusb_context **context);
void usb_destroy_context(libusb_context *context);

#endif // BRICKD_USB_H
