/*
 * brickd
 * Copyright (C) 2012 Matthias Bolte <matthias@tinkerforge.com>
 *
 * usb.c: USB specific functions
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

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "usb.h"

#include "brick.h"
#include "event.h"
#include "log.h"
#include "network.h"
#include "transfer.h"
#include "utils.h"

#define LOG_CATEGORY LOG_CATEGORY_USB

static libusb_context *_context = NULL;
static Array _bricks = ARRAY_INITIALIZER;

typedef int (*USBEnumerateFunction)(libusb_device *device);


static int usb_enumerate(USBEnumerateFunction function) {
	int rc;
	libusb_device **devices;
	libusb_device *device;
	int i = 0;
	struct libusb_device_descriptor descriptor;

	// get all devices
	rc = libusb_get_device_list(_context, &devices);

	if (rc < 0) {
		log_error("Could not get USB device list: %s (%d)",
		          get_libusb_error_name(rc), rc);

		return -1;
	}

	// check for Bricks
	for (device = devices[0]; device != NULL; device = devices[i++]) {
		rc = libusb_get_device_descriptor(device, &descriptor);

		if (rc < 0) {
			log_info("Could not get USB device descriptor, ignoring this device: %s (%d)",
			         get_libusb_error_name(rc), rc);

			continue;
		}

		if (descriptor.idVendor != USB_VENDOR_ID ||
		    descriptor.idProduct != USB_PRODUCT_ID) {
			continue;
		}

		rc = function(device);

		if (rc < 0) {
			goto cleanup;
		}
	}

	rc = 0;

cleanup:
	libusb_free_device_list(devices, 1);

	return rc;
}

static int usb_handle_device(libusb_device *device) {
	int i;
	Brick *brick;
	uint8_t bus_number = libusb_get_bus_number(device);
	uint8_t device_address = libusb_get_device_address(device);

	// check all known Bricks
	for (i = 0; i < _bricks.count; ++i) {
		brick = array_get(&_bricks, i);

		if (brick->bus_number == bus_number &&
		    brick->device_address == device_address) {
			// mark known Brick as connected
			brick->connected = 1;

			return 0;
		}
	}

	// create new Brick object
	log_debug("Found new USB device (bus: %u, device: %u)",
	          bus_number, device_address);

	brick = array_append(&_bricks);

	if (brick == NULL) {
		log_error("Could not append to Bricks array: %s (%d)",
		          get_errno_name(errno), errno);

		return -1;
	}

	if (brick_create(brick, bus_number, device_address) < 0) {
		array_remove(&_bricks, _bricks.count - 1, NULL);

		log_info("Ignoring USB device (bus: %u, device: %u) due to an error",
		         bus_number, device_address);

		return 0;
	}

	// mark new Brick as connected
	brick->connected = 1;

	log_info("Added USB device (bus: %d, device: %d) at index %d: %s [%s]",
	         brick->bus_number, brick->device_address, _bricks.count - 1,
	         brick->product, brick->serial_number);

	return 0;
}

static void usb_handle_events(void *opaque) {
	int rc;
	libusb_context *context = opaque;
	struct timeval tv;

	tv.tv_sec = 0;
	tv.tv_usec = 0;

	rc = libusb_handle_events_timeout(context, &tv);

	if (rc < 0) {
		log_error("Could not handle USB events: %s (%d)",
		          get_libusb_error_name(rc), rc);
	}
}

static void LIBUSB_CALL usb_add_pollfd(int fd, short events, void *opaque) {
	libusb_context *context = opaque;

	log_debug("Got told to add libusb pollfd (handle: %d, events: %d)", fd, events);

	// FIXME: need to handle libusb timeouts
	event_add_source(fd, EVENT_SOURCE_TYPE_USB, events, usb_handle_events, context); // FIXME: handle error?
}

static void LIBUSB_CALL usb_remove_pollfd(int fd, void *opaque) {
	(void)opaque;

	log_debug("Got told to remove libusb pollfd (handle: %d)", fd);

	event_remove_source(fd, EVENT_SOURCE_TYPE_USB); // FIXME: handle error?
}

int usb_init(void) {
	int phase = 0;

	log_debug("Initializing USB subsystem");

	// initialize main libusb context
	if (usb_create_context(&_context)) {
		goto cleanup;
	}

	phase = 1;

	if (!libusb_pollfds_handle_timeouts(_context)) {
		log_warn("libusb requires special timeout handling"); // FIXME
	} else {
		log_debug("libusb can handle timeouts on its own");
	}

	// create Bricks array
	if (array_create(&_bricks, 32, sizeof(Brick), 0) < 0) {
		log_error("Could not create Brick array: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 2;

	// find all Bricks
	if (usb_update() < 0) {
		goto cleanup;
	}

	phase = 3;

cleanup:
	switch (phase) { // no breaks, all cases fall through intentionally
	case 2:
		array_destroy(&_bricks, (FreeFunction)brick_destroy);

	case 1:
		usb_destroy_context(_context);

	default:
		break;
	}

	return phase == 3 ? 0 : -1;
}

void usb_exit(void) {
	log_debug("Shutting down USB subsystem");

	array_destroy(&_bricks, (FreeFunction)brick_destroy);

	usb_destroy_context(_context);
}

int usb_update(void) {
	int i;
	Brick *brick;
	int k;
	uint32_t uid;
	EnumerateCallback enumerate_callback;

	// mark all known Bricks as potentially removed
	for (i = 0; i < _bricks.count; ++i) {
		brick = array_get(&_bricks, i);

		brick->connected = 0;
	}

	// enumerate all USB devices and mark all Bricks that are still connected
	if (usb_enumerate(usb_handle_device) < 0) {
		return -1;
	}

	// remove all Bricks that are not marked as connected
	for (i = _bricks.count - 1; i >= 0; --i) {
		brick = array_get(&_bricks, i);

		if (brick->connected) {
			continue;
		}

		log_info("Removing USB device (bus: %d, device: %d) at index %d: %s [%s]",
		         brick->bus_number, brick->device_address, i,
		         brick->product, brick->serial_number);

		for (k = 0; k < brick->uids.count; ++k) {
			uid = *(uint32_t *)array_get(&brick->uids, k);

			memset(&enumerate_callback, 0, sizeof(EnumerateCallback));

			enumerate_callback.header.uid = uid;
			enumerate_callback.header.length = sizeof(EnumerateCallback);
			enumerate_callback.header.function_id = CALLBACK_ENUMERATE;
			enumerate_callback.header.sequence_number = 0; // FIXME

			base58_encode(enumerate_callback.uid, uid);
			enumerate_callback.enumeration_type = ENUMERATION_DISCONNECTED;

			log_debug("Sending enumeration-disconnected callback for [%s]",
			          enumerate_callback.uid);

			network_dispatch_packet((Packet *)&enumerate_callback);
		}

		array_remove(&_bricks, i, (FreeFunction)brick_destroy);
	}

	return 0;
}

void usb_dispatch_packet(Packet *packet) {
	int i;
	Brick *brick;
	int rc;
	int dispatched = 0;

	if (_bricks.count == 0) {
		log_debug("No Bricks connected, dropping request (U: %u, L: %u, F: %u, S: %u, R: %u)",
		          packet->header.uid, packet->header.length,
		          packet->header.function_id, packet->header.sequence_number,
		          packet->header.response_expected);

		return;
	}

	if (packet->header.uid == 0) {
		log_debug("Broadcasting request (U: %u, L: %u, F: %u, S: %u, R: %u) to %d Brick(s)",
		          packet->header.uid, packet->header.length,
		          packet->header.function_id, packet->header.sequence_number,
		          packet->header.response_expected, _bricks.count);

		for (i = 0; i < _bricks.count; ++i) {
			brick = array_get(&_bricks, i);

			brick_dispatch_packet(brick, packet, 1);
		}
	} else {
		log_debug("Dispatching request (U: %u, L: %u, F: %u, S: %u, R: %u) to %d Brick(s)",
		          packet->header.uid, packet->header.length,
		          packet->header.function_id, packet->header.sequence_number,
		          packet->header.response_expected, _bricks.count);

		for (i = 0; i < _bricks.count; ++i) {
			brick = array_get(&_bricks, i);

			rc = brick_dispatch_packet(brick, packet, 0);

			if (rc < 0) {
				continue;
			} else if (rc > 0) {
				dispatched = 1;
			}
		}

		if (dispatched) {
			return;
		}

		log_debug("Broadcasting request because no Brick knows the UID");

		for (i = 0; i < _bricks.count; ++i) {
			brick = array_get(&_bricks, i);

			brick_dispatch_packet(brick, packet, 1);
		}
	}
}

int usb_create_context(libusb_context **context) {
	int phase = 0;
	int rc;
	struct libusb_pollfd **pollfds = NULL;
	struct libusb_pollfd **pollfd;
	struct libusb_pollfd **last_added_pollfd = NULL;

	rc = libusb_init(context);

	if (rc < 0) {
		log_error("Could not initialize libusb context: %s (%d)",
		          get_libusb_error_name(rc), rc);

		goto cleanup;
	}

#if 0
	libusb_set_debug(*context, 5);
	libusb_set_log_stream(log_get_stream());
#endif

	phase = 1;

	// get pollfds from main libusb context
	pollfds = (struct libusb_pollfd **)libusb_get_pollfds(*context);

	if (pollfds == NULL) {
		log_error("Could not get pollfds from libusb context");

		goto cleanup;
	}

	for (pollfd = pollfds; *pollfd != NULL; ++pollfd) {
		if (event_add_source((*pollfd)->fd, EVENT_SOURCE_TYPE_USB,
		                     (*pollfd)->events, usb_handle_events,
		                     *context) < 0) {
			goto cleanup;
		}

		last_added_pollfd = pollfd;
		phase = 2;
	}

	// register pollfd notifiers
	libusb_set_pollfd_notifiers(*context, usb_add_pollfd, usb_remove_pollfd,
	                            *context);

	phase = 3;

cleanup:
	switch (phase) { // no breaks, all cases fall through intentionally
	case 2:
		for (pollfd = pollfds; pollfd != last_added_pollfd; ++pollfd) {
			event_remove_source((*pollfd)->fd, EVENT_SOURCE_TYPE_USB);
		}

	case 1:
		libusb_exit(*context);

	default:
		break;
	}

	free(pollfds);

	return phase == 3 ? 0 : -1;
}

void usb_destroy_context(libusb_context *context) {
	struct libusb_pollfd **pollfds = NULL;
	struct libusb_pollfd **pollfd;

	libusb_set_pollfd_notifiers(context, NULL, NULL, NULL);

	pollfds = (struct libusb_pollfd **)libusb_get_pollfds(context);

	if (pollfds == NULL) {
		log_error("Could not get pollfds from main libusb context");
	} else {
		for (pollfd = pollfds; *pollfd != NULL; ++pollfd) {
			event_remove_source((*pollfd)->fd, EVENT_SOURCE_TYPE_USB);
		}

		free(pollfds);
	}

	libusb_exit(context);
}
