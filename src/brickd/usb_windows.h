/*
 * brickd
 * Copyright (C) 2017, 2021 Matthias Bolte <matthias@tinkerforge.com>
 *
 * usb_windows.h: General USB code used for all flavors of Windows
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

#ifndef BRICKD_USB_WINDOWS_H
#define BRICKD_USB_WINDOWS_H

#include <windows.h>
#include <stdbool.h>

typedef enum {
	USB_HOTPLUG_TYPE_ARRIVAL = 0,
	USB_HOTPLUG_TYPE_REMOVAL
} USBHotplugType;

bool usb_check_hotplug_event(USBHotplugType type, GUID *guid, const char *name);

#endif // BRICKD_USB_WINDOWS_H
