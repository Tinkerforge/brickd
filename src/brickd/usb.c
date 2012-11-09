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
	log_debug("Found new USB device (bus %u, device %u)",
	          bus_number, device_address);

	brick = array_append(&_bricks);

	if (brick == NULL) {
		log_error("Could not append to Bricks array: %s (%d)",
		          get_errno_name(errno), errno);

		return -1;
	}

	if (brick_create(brick, _context, bus_number, device_address) < 0) {
		array_remove(&_bricks, _bricks.count - 1, NULL);

		log_info("Ignoring USB device (bus %u, device %u) due to an error",
		         bus_number, device_address);

		return 0;
	}

	// mark new Brick as connected
	brick->connected = 1;

	log_info("Added USB device (bus %d, device %d) at index %d: %s [%s]",
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
	(void)opaque;

	log_debug("Adding libusb pollfd (handle: %d, events: %d)", fd, events);

	// FIXME: need to handle libusb timeouts
	event_add_source(fd, events, usb_handle_events, _context); // FIXME: handle error?
}

static void LIBUSB_CALL usb_remove_pollfd(int fd, void *opaque) {
	(void)opaque;

	log_debug("Removing libusb pollfd (handle: %d)", fd);

	event_remove_source(fd); // FIXME: handle error?
}

int usb_init(void) {
	int rc;
	struct libusb_pollfd **pollfds;
	struct libusb_pollfd **pollfd;

	log_debug("Initializing USB subsystem");

	if (array_create(&_bricks, 32, sizeof(Brick)) < 0) {
		log_error("Could not create Brick array: %s (%d)",
		          get_errno_name(errno), errno);

		return -1;
	}

	// initialize main libusb context
	rc = libusb_init(&_context);

	if (rc < 0) {
		array_destroy(&_bricks, (FreeFunction)brick_destroy);

		log_error("Could not initialize main libusb context: %s (%d)",
		          get_libusb_error_name(rc), rc);

		return -1;
	}

	if (!libusb_pollfds_handle_timeouts(_context)) {
		log_warn("libusb requires special timeout handling"); // FIXME
	} else {
		log_debug("libusb can handle timeouts on its own");
	}

	// get pollfds from main libusb context
	pollfds = (struct libusb_pollfd **)libusb_get_pollfds(_context);

	if (pollfds == NULL) {
		libusb_exit(_context);
		array_destroy(&_bricks, (FreeFunction)brick_destroy);

		log_error("Could not get pollfds from main libusb context");

		return -1;
	}

	for (pollfd = pollfds; *pollfd != NULL; ++pollfd) {
		// FIXME: need to handle libsub timeouts
		if (event_add_source((*pollfd)->fd, (*pollfd)->events,
		                     usb_handle_events, _context) < 0) {
			// FIXME: close context, remove already added pollfds, free array
			return -1;
		}
	}

	free(pollfds);

	// register pollfd notifiers for main libusb context
	libusb_set_pollfd_notifiers(_context, usb_add_pollfd, usb_remove_pollfd, NULL);

	// find all Bricks
	return usb_update();
}

void usb_exit(void) {
	log_debug("Shutting down USB subsystem");

	// FIXME: close main libusb context, and cleanup other stuff

	array_destroy(&_bricks, (FreeFunction)brick_destroy);

	libusb_exit(_context);
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
	for (i = 0; i < _bricks.count;) {
		brick = array_get(&_bricks, i);

		if (brick->connected) {
			++i;
			continue;
		}

		log_info("Removed USB device (bus: %d, device: %d) at index %d: %s [%s]",
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




static void write_transfer_callback(Transfer *transfer) {
	if (transfer->handle->status != LIBUSB_TRANSFER_COMPLETED) {
		log_warn("Write transfer returned with an error: %s (%d)",
		         get_libusb_transfer_status_name(transfer->handle->status),
		         transfer->handle->status);

		return;
	}
}


void usb_dispatch_packet(Packet *packet) {
	int j;
	Brick *brick;
	int submitted;
	int k;
	Transfer *transfer;

	log_debug("Dispatching request (U: %u, L: %u, F: %u, S: %u, R: %u) to %d Brick(s)",
	          packet->header.uid, packet->header.length,
	          packet->header.function_id,
	          packet->header.sequence_number,
	          packet->header.response_expected,
	          _bricks.count);

	for (j = 0; j < _bricks.count; ++j) {
		brick = array_get(&_bricks, j);

		submitted = 0;

		for (k = 0; k < brick->write_transfers.count; ++k) {
			transfer = array_get(&brick->write_transfers, k);

			if (transfer->submitted) {
				continue;
			}

			transfer->function = write_transfer_callback;

			memcpy(&transfer->packet, packet, packet->header.length);

			if (transfer_submit(transfer) < 0) {
				// FIXME: how to handle a failed submission, try to re-submit?

				continue;
			}

			submitted = 1;

			break;
		}

		if (!submitted) {
			log_error("Could not find a free write transfer for %s [%s]",
			          brick->product, brick->serial_number);

			/*queued_packet = array_append(&brick->write_transfer_packet_queue);

			if (queued_packet == NULL) {
				// FIXME: report error
				continue;
			}

			memcpy(queued_packet, packet, packet->header.length);*/
		}
	}
}
