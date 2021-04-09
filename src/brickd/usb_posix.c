/*
 * brickd
 * Copyright (C) 2013-2014, 2017-2021 Matthias Bolte <matthias@tinkerforge.com>
 *
 * usb_posix.c: POSIX specific USB functions
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
 * related to hotplug handling is the management of device files in /dev/bus/usb.
 * typically there is some service such as udevd that takes care of this, but in
 * minimal container systems brickd might be the only process running. libusb
 * can still receive uevents but there is no service to manage device files that
 * libusb expects to exist. to make libusb work in this case brickd can create
 * the necessary device files itself based on libusb hotplug events.
 */

#ifndef DAEMONLIB_WITH_STATIC
	#include <dlfcn.h>
#endif
#include <errno.h>
#include <libusb.h>
#include <stdlib.h>
#ifdef __linux__
	#include <sys/sysmacros.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <daemonlib/event.h>
#include <daemonlib/log.h>
#include <daemonlib/macros.h>
#include <daemonlib/pipe.h>

#include "usb.h"

static LogSource _log_source = LOG_SOURCE_INITIALIZER;

static Pipe _hotplug_pipe;
static libusb_hotplug_callback_handle _brick_hotplug_handle;
static libusb_hotplug_callback_handle _red_brick_hotplug_handle;

#ifdef BRICKD_WITH_LIBUSB_HOTPLUG_MKNOD
bool usb_hotplug_mknod = false;
#endif

static void usb_forward_hotplug(void *opaque) {
	uint8_t byte;

	(void)opaque;

	if (pipe_read(&_hotplug_pipe, &byte, sizeof(byte)) < 0) {
		log_error("Could not read from hotplug pipe: %s (%d)",
		          get_errno_name(errno), errno);

		return;
	}

	log_debug("Starting USB device scan, triggered by hotplug");

	usb_rescan();
}

static int LIBUSB_CALL usb_handle_hotplug(libusb_context *context, libusb_device *device,
                                          libusb_hotplug_event event, void *user_data) {
	uint8_t bus_number = libusb_get_bus_number(device);
	uint8_t device_address = libusb_get_device_address(device);
#ifdef BRICKD_WITH_LIBUSB_HOTPLUG_MKNOD
	char buffer[256];
#endif
	uint8_t byte = 0;

	(void)context;
	(void)user_data;

	switch (event) {
	case LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED:
		log_debug("Received libusb hotplug event (event: arrived, bus: %u, device: %u)",
		          bus_number, device_address);

#ifdef BRICKD_WITH_LIBUSB_HOTPLUG_MKNOD
		if (usb_hotplug_mknod) {
			// create bus directory
			snprintf(buffer, sizeof(buffer), "/dev/bus/usb/%03u/", bus_number);

			if (mkdir(buffer, 0755) < 0 && errno != EEXIST) {
				log_warn("Could not create bus directory %s: %s (%d)", buffer, get_errno_name(errno), errno);
			}

			// create device file, try even if creating the bus directory failed
			snprintf(buffer, sizeof(buffer), "/dev/bus/usb/%03u/%03u", bus_number, device_address);

			if (mknod(buffer, 0664 | S_IFCHR, makedev(189, ((bus_number - 1) << 7) | (device_address - 1))) < 0) {
				log_warn("Could not create device file %s: %s (%d)", buffer, get_errno_name(errno), errno);
			} else {
				log_debug("Successfully created device file %s", buffer);
			}
		}
#endif

		if (pipe_write(&_hotplug_pipe, &byte, sizeof(byte)) < 0) {
			log_error("Could not write to hotplug pipe: %s (%d)",
			          get_errno_name(errno), errno);
		}

		break;

	case LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT:
		log_debug("Received libusb hotplug event (event: left, bus: %u, device: %u)",
		          bus_number, device_address);

#ifdef BRICKD_WITH_LIBUSB_HOTPLUG_MKNOD
		if (usb_hotplug_mknod) {
			// remove device file
			snprintf(buffer, sizeof(buffer), "/dev/bus/usb/%03u/%03u", bus_number, device_address);

			if (remove(buffer) < 0 && errno != ENOENT) {
				log_warn("Could not remove device file %s: %s (%d)", buffer, get_errno_name(errno), errno);
			} else {
				log_debug("Successfully removed device file %s", buffer);
			}
		}
#endif

		if (pipe_write(&_hotplug_pipe, &byte, sizeof(byte)) < 0) {
			log_error("Could not write to hotplug pipe: %s (%d)",
			          get_errno_name(errno), errno);
		}

		break;

	default:
		log_debug("Ignoring libusb hotplug event (event: %d, bus: %u, device: %u)",
		          event, bus_number, device_address);

		break;
	}

	return 0;
}

int usb_init_platform(void) {
	return 0;
}

void usb_exit_platform(void) {
}

int usb_init_hotplug(libusb_context *context) {
	int phase = 0;
	int rc;

	// create hotplug pipe
	if (pipe_create(&_hotplug_pipe, PIPE_FLAG_NON_BLOCKING_READ) < 0) {
		log_error("Could not create hotplug pipe: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 1;

	if (event_add_source(_hotplug_pipe.base.read_handle, EVENT_SOURCE_TYPE_GENERIC,
	                     "hotplug", EVENT_READ, usb_forward_hotplug, NULL) < 0) {
		goto cleanup;
	}

	phase = 2;

	// cannot use LIBUSB_HOTPLUG_ENUMERATE here for initial enumeration,
	// because it is broken in libusb 1.0.16. calling libusb functions from the
	// hotplug callback might deadlock.
	rc = libusb_hotplug_register_callback(context,
	                                      LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED |
	                                      LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT, 0,
	                                      USB_BRICK_VENDOR_ID, USB_BRICK_PRODUCT_ID,
	                                      LIBUSB_HOTPLUG_MATCH_ANY,
	                                      usb_handle_hotplug, NULL,
	                                      &_brick_hotplug_handle);

	if (rc < 0) {
		log_error("Could not register libusb hotplug callback: %s (%d)",
		          usb_get_error_name(rc), rc);

		goto cleanup;
	}

	phase = 3;

	rc = libusb_hotplug_register_callback(context,
	                                      LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED |
	                                      LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT, 0,
	                                      USB_RED_BRICK_VENDOR_ID, USB_RED_BRICK_PRODUCT_ID,
	                                      LIBUSB_HOTPLUG_MATCH_ANY,
	                                      usb_handle_hotplug, NULL,
	                                      &_brick_hotplug_handle);

	if (rc < 0) {

		log_error("Could not register libusb hotplug callback: %s (%d)",
		          usb_get_error_name(rc), rc);

		goto cleanup;
	}

	phase = 4;

cleanup:
	switch (phase) { // no breaks, all cases fall through intentionally
	case 3:
		libusb_hotplug_deregister_callback(context, _brick_hotplug_handle);
		// fall through

	case 2:
		event_remove_source(_hotplug_pipe.base.read_handle, EVENT_SOURCE_TYPE_GENERIC);
		// fall through

	case 1:
		pipe_destroy(&_hotplug_pipe);
		// fall through

	default:
		break;
	}

	return phase == 4 ? 0 : -1;
}

void usb_exit_hotplug(libusb_context *context) {
	libusb_hotplug_deregister_callback(context, _brick_hotplug_handle);
	libusb_hotplug_deregister_callback(context, _red_brick_hotplug_handle);

	event_remove_source(_hotplug_pipe.base.read_handle, EVENT_SOURCE_TYPE_GENERIC);
	pipe_destroy(&_hotplug_pipe);
}

bool usb_has_hotplug(void) {
	return libusb_has_capability(LIBUSB_CAP_HAS_CAPABILITY) !=0 &&
	       libusb_has_capability(LIBUSB_CAP_HAS_HOTPLUG) != 0;
}
