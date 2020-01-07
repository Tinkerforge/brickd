/*
 * brickd
 * Copyright (C) 2017 Matthias Bolte <matthias@tinkerforge.com>
 *
 * usb_windows.c: General USB code used for all flavors of Windows
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

#include <daemonlib/log.h>

#include "usb_windows.h"

static LogSource _log_source = LOG_SOURCE_INITIALIZER;

// general USB device interface GUID, applies to all Bricks. for the RED Brick
// this only applies to the composite device itself, but not to its functions
const GUID GUID_DEVINTERFACE_USB_DEVICE =
{ 0xA5DCBF10L, 0x6530, 0x11D2, { 0x90, 0x1F, 0x00, 0xC0, 0x4F, 0xB9, 0x51, 0xED } };

// Brick device interface GUID (does not apply to the RED Brick). set by the
// brick.inf driver and reported by the Brick itself if used driverless since
// Windows 8, but not by all firmware versions. For example, Master Brick since
// 2.4.0, therefore it cannot be used as the only way to detect Bricks
const GUID GUID_DEVINTERFACE_BRICK_DEVICE =
{ 0x870013DDL, 0xFB1D, 0x4BD7, { 0xA9, 0x6C, 0x1F, 0x0B, 0x7D, 0x31, 0xAF, 0x41 } };

// RED Brick device interface GUID (only applies to the Brick function). set by
// the red_brick.inf driver and reported by the RED Brick itself if used driverless
// since Windows 8. therefore it can be used as the sole way to detect RED Bricks
const GUID GUID_DEVINTERFACE_RED_BRICK_DEVICE =
{ 0x9536B3B1L, 0x6077, 0x4A3B, { 0x9B, 0xAC, 0x7C, 0x2C, 0xFA, 0x8A, 0x2B, 0xF3 } };

bool usb_check_hotplug_event(USBHotplugType type, GUID *guid, const char *name) {
	bool possibly_brick = false;
	bool definitely_brick = false;
	bool definitely_red_brick = false;
	const char *name_prefix1 = "\\\\?\\USB\\";
	const char *name_prefix2 = "VID_16D0&PID_063D";
	char guid_str[64] = "<unknown>";

	// check GUID
	if (memcmp(guid, &GUID_DEVINTERFACE_USB_DEVICE, sizeof(GUID)) == 0) {
		possibly_brick = true;
	} else if (memcmp(guid, &GUID_DEVINTERFACE_BRICK_DEVICE, sizeof(GUID)) == 0) {
		definitely_brick = true;
	} else if (memcmp(guid, &GUID_DEVINTERFACE_RED_BRICK_DEVICE, sizeof(GUID)) == 0) {
		definitely_red_brick = true;
	} else {
		return false;
	}

	// check name for Brick vendor/product ID
	if (possibly_brick &&
	    strlen(name) > strlen(name_prefix1) &&
	    strncasecmp(name + strlen(name_prefix1), name_prefix2, strlen(name_prefix2)) == 0) {
		definitely_brick = true;
	}

	if (!definitely_brick && !definitely_red_brick) {
		return false;
	}

	snprintf(guid_str, sizeof(guid_str),
	         "{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
	         (uint32_t)guid->Data1, guid->Data2, guid->Data3,
	         guid->Data4[0], guid->Data4[1], guid->Data4[2], guid->Data4[3],
	         guid->Data4[4], guid->Data4[5], guid->Data4[6], guid->Data4[7]);

	log_debug("Received device-interface notification (type: %s, guid: %s, name: %s)",
	          type == USB_HOTPLUG_TYPE_ARRIVAL ? "arrival" : "removal", guid_str, name);

	return true;
}
