/*
 * brickd
 * Copyright (C) 2012 Matthias Bolte <matthias@tinkerforge.com>
 *
 * udev.c: udev specific functions
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

#include <poll.h>
#include <libudev.h>
#include <string.h>

#include "udev.h"

#include "event.h"
#include "log.h"
#include "usb.h"

#define LOG_CATEGORY LOG_CATEGORY_HOTPLUG

static struct udev *_udev_context = NULL;
static struct udev_monitor *_udev_monitor = NULL;
static int _udev_monitor_fd = -1;

static void udev_handle_event(void *opaque) {
	struct udev_device* device;
	const char *action;
	const char *dev_node;
	const char *sys_name;

	(void)opaque;

	device = udev_monitor_receive_device(_udev_monitor);

	if (device == NULL) {
		log_error("Could not read data from udev monitor socket");

		return;
	}

	action = udev_device_get_action(device);

	if (action == NULL) {
		goto cleanup;
	}

	dev_node = udev_device_get_devnode(device);

	if (dev_node == NULL) {
		goto cleanup;
	}

	sys_name = udev_device_get_sysname(device);

	if (sys_name == NULL) {
		goto cleanup;
	}

	if (strncmp(action, "add", 3) == 0 || strncmp(action, "remove", 6) == 0) {
		log_debug("Received udev event (action: %s, dev node: %s, sys name: %s)",
		          action, dev_node, sys_name);

		usb_update();
	} else {
		log_debug("Ignoring udev event (action: %s, dev node: %s, sys name: %s)",
		          action, dev_node, sys_name);
	}

cleanup:
	udev_device_unref(device);
}

int udev_init(void) {
	int phase = 0;
	int rc;

	log_debug("Initializing udev subsystem");

	// create udev context
	_udev_context = udev_new();

	if (_udev_context == NULL) {
		log_error("Could not create udev context");

		goto cleanup;
	}

	phase = 1;

	// create udev monitor
	_udev_monitor = udev_monitor_new_from_netlink(_udev_context, "udev");

	if (_udev_monitor == NULL) {
		log_error("Could not initialize udev monitor");

		goto cleanup;
	}

	phase = 2;

	// create filter for USB
	rc = udev_monitor_filter_add_match_subsystem_devtype(_udev_monitor, "usb", 0);

	if (rc != 0) {
		log_error("Could not initialize udev monitor filter for 'usb' subsystem: %d", rc);

		goto cleanup;
	}

	rc = udev_monitor_enable_receiving(_udev_monitor);

	if (rc != 0) {
		log_error("Could not enable the udev monitor: %d", rc);

		goto cleanup;
	}

	// add event source
	_udev_monitor_fd = udev_monitor_get_fd(_udev_monitor);

	if (event_add_source(_udev_monitor_fd, EVENT_SOURCE_TYPE_GENERIC,
	                     EVENT_READ, udev_handle_event, NULL) < 0) {
		goto cleanup;
	}

	phase = 3;

cleanup:
	switch (phase) { // no breaks, all cases fall through intentionally
	case 2:
		udev_monitor_unref(_udev_monitor);

	case 1:
		udev_unref(_udev_context);

	default:
		break;
	}

	return phase == 3 ? 0 : -1;
}

void udev_exit(void) {
	log_debug("Shutting down udev subsystem");

	event_remove_source(_udev_monitor_fd, EVENT_SOURCE_TYPE_GENERIC); // FIXME: handle error?

	udev_monitor_unref(_udev_monitor);
	udev_unref(_udev_context);
}
