/*
 * brickd
 * Copyright (C) 2012-2013 Matthias Bolte <matthias@tinkerforge.com>
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

#include "array.h"
#include "stack.h"
#include "event.h"
#include "log.h"
#include "network.h"
#include "usb_transfer.h"
#include "utils.h"

#define LOG_CATEGORY LOG_CATEGORY_USB

static libusb_context *_context = NULL;
static Array _stacks = ARRAY_INITIALIZER;

typedef int (*USBEnumerateFunction)(libusb_device *device);

static int usb_enumerate(USBEnumerateFunction function) {
	int rc;
	libusb_device **devices;
	libusb_device *device;
	int i = 0;
	struct libusb_device_descriptor descriptor;
	uint8_t bus_number;
	uint8_t device_address;

	// get all devices
	rc = libusb_get_device_list(_context, &devices);

	if (rc < 0) {
		log_error("Could not get USB device list: %s (%d)",
		          usb_get_error_name(rc), rc);

		return -1;
	}

	// check for stacks
	for (device = devices[0]; device != NULL; device = devices[++i]) {
		bus_number = libusb_get_bus_number(device);
		device_address = libusb_get_device_address(device);

		rc = libusb_get_device_descriptor(device, &descriptor);

		if (rc < 0) {
			log_warn("Could not get descriptor for USB device (bus: %u, device: %u), ignoring it: %s (%d)",
			         bus_number, device_address, usb_get_error_name(rc), rc);

			continue;
		}

		if (descriptor.idVendor != USB_VENDOR_ID ||
		    descriptor.idProduct != USB_PRODUCT_ID) {
			continue;
		}

		if (descriptor.bcdDevice < USB_DEVICE_RELEASE) {
			log_warn("USB device (bus: %u, device: %u) has protocol 1.0 firmware, ignoring it",
			         bus_number, device_address);

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
	USBStack *stack;
	uint8_t bus_number = libusb_get_bus_number(device);
	uint8_t device_address = libusb_get_device_address(device);

	// check all known stacks
	for (i = 0; i < _stacks.count; ++i) {
		stack = array_get(&_stacks, i);

		if (stack->bus_number == bus_number &&
		    stack->device_address == device_address) {
			// mark known stack as connected
			stack->connected = 1;

			return 0;
		}
	}

	// create new stack object
	log_debug("Found new USB device (bus: %u, device: %u)",
	          bus_number, device_address);

	stack = array_append(&_stacks);

	if (stack == NULL) {
		log_error("Could not append to stacks array: %s (%d)",
		          get_errno_name(errno), errno);

		return -1;
	}

	if (usb_stack_create(stack, bus_number, device_address) < 0) {
		array_remove(&_stacks, _stacks.count - 1, NULL);

		log_warn("Ignoring USB device (bus: %u, device: %u) due to an error",
		         bus_number, device_address);

		return 0;
	}

	// mark new stack as connected
	stack->connected = 1;

	log_info("Added USB device (bus: %u, device: %u) at index %d: %s",
	         stack->bus_number, stack->device_address, _stacks.count - 1,
	         stack->base.name);

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
		          usb_get_error_name(rc), rc);
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
		log_debug("libusb requires special timeout handling"); // FIXME
	} else {
		log_debug("libusb can handle timeouts on its own");
	}

	// create USB stacks array, the USBStack struct is not relocatable, because
	// its transfers keep a pointer to it
	if (array_create(&_stacks, 32, sizeof(USBStack), 0) < 0) {
		log_error("Could not create stack array: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 2;

	// find all stacks
	if (usb_update() < 0) {
		goto cleanup;
	}

	phase = 3;

cleanup:
	switch (phase) { // no breaks, all cases fall through intentionally
	case 2:
		array_destroy(&_stacks, (FreeFunction)usb_stack_destroy);

	case 1:
		usb_destroy_context(_context);

	default:
		break;
	}

	return phase == 3 ? 0 : -1;
}

void usb_exit(void) {
	log_debug("Shutting down USB subsystem");

	array_destroy(&_stacks, (FreeFunction)usb_stack_destroy);

	usb_destroy_context(_context);
}

int usb_update(void) {
	int i;
	USBStack *stack;
	int k;
	uint32_t uid; // always little endian
	EnumerateCallback enumerate_callback;

	// mark all known stacks as potentially removed
	for (i = 0; i < _stacks.count; ++i) {
		stack = array_get(&_stacks, i);

		stack->connected = 0;
	}

	// enumerate all USB devices and mark all stacks that are still connected
	if (usb_enumerate(usb_handle_device) < 0) {
		return -1;
	}

	// remove all stacks that are not marked as connected
	for (i = _stacks.count - 1; i >= 0; --i) {
		stack = array_get(&_stacks, i);

		if (stack->connected) {
			continue;
		}

		log_info("Removing USB device (bus: %u, device: %u) at index %d: %s ",
		         stack->bus_number, stack->device_address, i, stack->base.name);

		for (k = 0; k < stack->base.uids.count; ++k) {
			uid = *(uint32_t *)array_get(&stack->base.uids, k);

			memset(&enumerate_callback, 0, sizeof(enumerate_callback));

			enumerate_callback.header.uid = uid;
			enumerate_callback.header.length = sizeof(enumerate_callback);
			enumerate_callback.header.function_id = CALLBACK_ENUMERATE;
			packet_header_set_sequence_number(&enumerate_callback.header, 0);

			base58_encode(enumerate_callback.uid, uint32_from_le(uid));
			enumerate_callback.enumeration_type = ENUMERATION_TYPE_DISCONNECTED;

			log_debug("Sending enumerate-disconnected callback (uid: %s)",
			          enumerate_callback.uid);

			network_dispatch_packet((Packet *)&enumerate_callback);
		}

		array_remove(&_stacks, i, (FreeFunction)usb_stack_destroy);
	}

	return 0;
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
		          usb_get_error_name(rc), rc);

		goto cleanup;
	}

#if 0
	libusb_set_debug(*context, 5);
	libusb_set_log_file(log_get_file());
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

#ifdef LIBUSBX_EXPORTS_FREE_FUNCTION
	libusb_free(pollfds); // avoid possible heap-mismatch on Windows
#else
	free(pollfds);
#endif

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

#ifdef LIBUSBX_EXPORTS_FREE_FUNCTION
		libusb_free(pollfds); // avoid possible heap-mismatch on Windows
#else
		free(pollfds);
#endif
	}

	libusb_exit(context);
}

int usb_get_device_name(libusb_device_handle *device_handle, char *name, int length) {
	int rc;
	libusb_device *device = libusb_get_device(device_handle);
	uint8_t bus_number = libusb_get_bus_number(device);
	uint8_t device_address = libusb_get_device_address(device);
	struct libusb_device_descriptor device_descriptor;
	char product[64];
	char serial_number[64];

	// get device descriptor
	rc = libusb_get_device_descriptor(device, &device_descriptor);

	if (rc < 0) {
		log_error("Could not get device descriptor for USB device (bus: %u, device: %u): %s (%d)",
		          bus_number, device_address, usb_get_error_name(rc), rc);

		return -1;
	}

	// get product string descriptor
	rc = libusb_get_string_descriptor_ascii(device_handle,
	                                        device_descriptor.iProduct,
	                                        (unsigned char *)product,
	                                        sizeof(product));

	if (rc < 0) {
		log_error("Could not get product string descriptor for USB device (bus: %u, device: %u): %s (%d)",
		          bus_number, device_address, usb_get_error_name(rc), rc);

		return -1;
	}

	// get serial number string descriptor
	rc = libusb_get_string_descriptor_ascii(device_handle,
	                                        device_descriptor.iSerialNumber,
	                                        (unsigned char *)serial_number,
	                                        sizeof(serial_number));

	if (rc < 0) {
		log_error("Could not get serial number string descriptor for USB device (bus: %u, device: %u): %s (%d)",
		          bus_number, device_address, usb_get_error_name(rc), rc);

		return -1;
	}

	// format name
	snprintf(name, length - 1, "%s [%s]", product, serial_number);
	name[length - 1] = '\0';

	return 0;
}

const char *usb_get_error_name(int error_code) {
	#define LIBUSB_ERROR_NAME(code) case code: return #code

	switch (error_code) {
	LIBUSB_ERROR_NAME(LIBUSB_SUCCESS);
	LIBUSB_ERROR_NAME(LIBUSB_ERROR_IO);
	LIBUSB_ERROR_NAME(LIBUSB_ERROR_INVALID_PARAM);
	LIBUSB_ERROR_NAME(LIBUSB_ERROR_ACCESS);
	LIBUSB_ERROR_NAME(LIBUSB_ERROR_NO_DEVICE);
	LIBUSB_ERROR_NAME(LIBUSB_ERROR_NOT_FOUND);
	LIBUSB_ERROR_NAME(LIBUSB_ERROR_BUSY);
	LIBUSB_ERROR_NAME(LIBUSB_ERROR_TIMEOUT);
	LIBUSB_ERROR_NAME(LIBUSB_ERROR_OVERFLOW);
	LIBUSB_ERROR_NAME(LIBUSB_ERROR_PIPE);
	LIBUSB_ERROR_NAME(LIBUSB_ERROR_INTERRUPTED);
	LIBUSB_ERROR_NAME(LIBUSB_ERROR_NO_MEM);
	LIBUSB_ERROR_NAME(LIBUSB_ERROR_NOT_SUPPORTED);
	LIBUSB_ERROR_NAME(LIBUSB_ERROR_OTHER);

	default: return "<unknown>";
	}

	#undef LIBUSB_ERROR_NAME
}

const char *usb_get_speed_name(int speed) {
	#define LIBUSB_SPEED_NAME(speed) case speed: return #speed

	switch (speed) {
	LIBUSB_SPEED_NAME(LIBUSB_SPEED_UNKNOWN);
	LIBUSB_SPEED_NAME(LIBUSB_SPEED_LOW);
	LIBUSB_SPEED_NAME(LIBUSB_SPEED_FULL);
	LIBUSB_SPEED_NAME(LIBUSB_SPEED_HIGH);
	LIBUSB_SPEED_NAME(LIBUSB_SPEED_SUPER);

	default: return "<unknown>";
	}

	#undef LIBUSB_SPEED_NAME
}
