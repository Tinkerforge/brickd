/*
 * brickd
 * Copyright (C) 2013 Matthias Bolte <matthias@tinkerforge.com>
 *
 * usb_winapi.c: WinAPI based USB specific functions
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

/*
 * brickd comes with its own libusbx fork on Windows. therefore, it is not
 * affected by the hotplug race between brickd and libusb 1.0.16. see the long
 * comment in usb_posix.c for details.
 *
 * once libusb gains hotplug support for Windows and the libusbx fork bundled
 * with brickd gets updated to include it brickd will also have to used the
 * hotplug handling in libusb on Windows. there is a similar race in event
 * handling to expect as on Linux and Mac OS X.
 */

#include "usb.h"

int usb_init_platform(void) {
	return 0;
}

void usb_exit_platform(void) {
}

int usb_init_hotplug(libusb_context *context) {
	(void)context;

	return 0;
}

void usb_exit_hotplug(libusb_context *context) {
	(void)context;
}

int usb_has_hotplug(void) {
	return 0;
}
