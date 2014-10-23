/*
 * brickd
 * Copyright (C) 2012-2014 Matthias Bolte <matthias@tinkerforge.com>
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
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <daemonlib/array.h>
#include <daemonlib/event.h>
#include <daemonlib/log.h>
#include <daemonlib/utils.h>

#include "usb.h"

#include "stack.h"
#include "network.h"
#include "usb_transfer.h"

#define LOG_CATEGORY LOG_CATEGORY_USB

static bool _libusb_debug = false;
static libusb_context *_context = NULL;
static Array _usb_stacks;
static bool _initialized_hotplug = false;

extern int usb_init_platform(void);
extern void usb_exit_platform(void);
extern int usb_init_hotplug(libusb_context *context);
extern void usb_exit_hotplug(libusb_context *context);

static int usb_enumerate(void) {
	int result = -1;
	libusb_device **devices;
	libusb_device *device;
	int rc;
	int i = 0;
	struct libusb_device_descriptor descriptor;
	uint8_t bus_number;
	uint8_t device_address;
	bool known;
	int k;
	USBStack *usb_stack;

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
			log_warn("Could not get device descriptor for USB device (bus: %u, device: %u), ignoring USB device: %s (%d)",
			         bus_number, device_address, usb_get_error_name(rc), rc);

			continue;
		}

		if (descriptor.idVendor == USB_BRICK_VENDOR_ID &&
		    descriptor.idProduct == USB_BRICK_PRODUCT_ID) {
			if (descriptor.bcdDevice < USB_BRICK_DEVICE_RELEASE) {
				log_warn("USB device (bus: %u, device: %u) has protocol 1.0 firmware, ignoring USB device",
				         bus_number, device_address);

				continue;
			}
		} else if (descriptor.idVendor == USB_RED_BRICK_VENDOR_ID &&
		           descriptor.idProduct == USB_RED_BRICK_PRODUCT_ID) {
			if (descriptor.bcdDevice < USB_RED_BRICK_DEVICE_RELEASE) {
				log_warn("USB device (bus: %u, device: %u) has unexpected release version, ignoring USB device",
				         bus_number, device_address);

				continue;
			}
		} else {
			continue;
		}

		// check all known stacks
		known = false;

		for (k = 0; k < _usb_stacks.count; ++k) {
			usb_stack = array_get(&_usb_stacks, k);

			if (usb_stack->bus_number == bus_number &&
			    usb_stack->device_address == device_address) {
				// mark known USBStack as connected
				usb_stack->connected = true;
				known = true;

				break;
			}
		}

		if (known) {
			continue;
		}

		// create new USBStack object
		log_debug("Found new USB device (bus: %u, device: %u)",
		          bus_number, device_address);

		usb_stack = array_append(&_usb_stacks);

		if (usb_stack == NULL) {
			log_error("Could not append to USB stacks array: %s (%d)",
			          get_errno_name(errno), errno);

			goto cleanup;
		}

		if (usb_stack_create(usb_stack, bus_number, device_address) < 0) {
			array_remove(&_usb_stacks, _usb_stacks.count - 1, NULL);

			log_warn("Ignoring USB device (bus: %u, device: %u) due to an error",
			         bus_number, device_address);

			continue;
		}

		// mark new stack as connected
		usb_stack->connected = true;

		log_info("Added USB device (bus: %u, device: %u) at index %d: %s",
		         usb_stack->bus_number, usb_stack->device_address,
		         _usb_stacks.count - 1, usb_stack->base.name);
	}

	result = 0;

cleanup:
	libusb_free_device_list(devices, 1);

	return result;
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

	event_remove_source(fd, EVENT_SOURCE_TYPE_USB);
}

int usb_init(bool libusb_debug) {
	int phase = 0;

	log_debug("Initializing USB subsystem");

	_libusb_debug = libusb_debug;

	if (_libusb_debug) {
		putenv("LIBUSB_DEBUG=5");

#ifdef _WIN32
		libusb_set_log_file(log_get_file());
#endif
	}

	if (usb_init_platform() < 0) {
		goto cleanup;
	}

	phase = 1;

	// initialize main libusb context
	if (usb_create_context(&_context)) {
		goto cleanup;
	}

	phase = 2;

	if (!libusb_pollfds_handle_timeouts(_context)) {
		log_debug("libusb requires special timeout handling"); // FIXME
	} else {
		log_debug("libusb can handle timeouts on its own");
	}

	// create USB stack array. the USBStack struct is not relocatable, because
	// its USB transfers keep a pointer to it
	if (array_create(&_usb_stacks, 32, sizeof(USBStack), false) < 0) {
		log_error("Could not create USB stack array: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 3;

	if (usb_has_hotplug()) {
		log_debug("libusb supports hotplug");

		if (usb_init_hotplug(_context) < 0) {
			goto cleanup;
		}

		_initialized_hotplug = true;
	} else {
		log_debug("libusb does not support hotplug");
	}

	if (usb_rescan() < 0) {
		goto cleanup;
	}

	phase = 4;

cleanup:
	switch (phase) { // no breaks, all cases fall through intentionally
	case 3:
		array_destroy(&_usb_stacks, (ItemDestroyFunction)usb_stack_destroy);

	case 2:
		usb_destroy_context(_context);

	case 1:
		usb_exit_platform();

	default:
		break;
	}

	return phase == 4 ? 0 : -1;
}

void usb_exit(void) {
	log_debug("Shutting down USB subsystem");

	if (_initialized_hotplug) {
		usb_exit_hotplug(_context);
	}

	array_destroy(&_usb_stacks, (ItemDestroyFunction)usb_stack_destroy);

	usb_destroy_context(_context);

	usb_exit_platform();
}

int usb_rescan(void) {
	int i;
	USBStack *usb_stack;

	log_debug("Looking for added/removed USB devices");

	// mark all known USB stacks as potentially removed
	for (i = 0; i < _usb_stacks.count; ++i) {
		usb_stack = array_get(&_usb_stacks, i);

		usb_stack->connected = false;
	}

	// enumerate all USB devices, mark all USB stacks that are still connected
	// and add USB stacks that are newly connected
	if (usb_enumerate() < 0) {
		return -1;
	}

	// remove all USB stacks that are not marked as connected. iterate backwards
	// so array_remove can be used without invalidating the current index
	for (i = _usb_stacks.count - 1; i >= 0; --i) {
		usb_stack = array_get(&_usb_stacks, i);

		if (usb_stack->connected) {
			continue;
		}

		log_info("Removing USB device (bus: %u, device: %u) at index %d: %s ",
		         usb_stack->bus_number, usb_stack->device_address, i,
		         usb_stack->base.name);

		stack_announce_disconnect(&usb_stack->base);

		array_remove(&_usb_stacks, i, (ItemDestroyFunction)usb_stack_destroy);
	}

	return 0;
}

int usb_reopen(void) {
	int i;
	USBStack *usb_stack;

	log_debug("Reopening all USB devices");

	// iterate backwards for simpler index handling and to avoid memmove
	// in array_remove call
	for (i = _usb_stacks.count - 1; i >= 0; --i) {
		usb_stack = array_get(&_usb_stacks, i);

		log_info("Temporarily removing USB device (bus: %u, device: %u) at index %d: %s ",
		         usb_stack->bus_number, usb_stack->device_address, i,
		         usb_stack->base.name);

		stack_announce_disconnect(&usb_stack->base);

		array_remove(&_usb_stacks, i, (ItemDestroyFunction)usb_stack_destroy);
	}

	return usb_rescan();
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

	if (_libusb_debug) {
		libusb_set_debug(*context, 5);
	}

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

	phase = 3;

	// register pollfd notifiers
	libusb_set_pollfd_notifiers(*context, usb_add_pollfd, usb_remove_pollfd,
	                            *context);

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

#ifdef _WIN32
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

#ifdef _WIN32
		libusb_free(pollfds); // avoid possible heap-mismatch on Windows
#else
		free(pollfds);
#endif
	}

	libusb_exit(context);
}

int usb_get_interface_endpoints(libusb_device_handle *device_handle, int interface_number,
                                uint8_t *endpoint_in, uint8_t *endpoint_out) {
	int rc;
	libusb_device *device = libusb_get_device(device_handle);
	uint8_t bus_number = libusb_get_bus_number(device);
	uint8_t device_address = libusb_get_device_address(device);
	int i;
	struct libusb_config_descriptor *config_descriptor;
	const struct libusb_interface_descriptor *interface_descriptor;
	int k;
	const struct libusb_endpoint_descriptor *endpoint_descriptor;

	rc = libusb_get_config_descriptor(device, 0, &config_descriptor);

	if (rc < 0) {
		log_error("Could not get config descriptor for USB device (bus: %u, device: %u): %s (%d)",
		          bus_number, device_address, usb_get_error_name(rc), rc);

		return -1;
	}

	for (i = 0; i < config_descriptor->bNumInterfaces; ++i) {
		if (config_descriptor->interface[i].num_altsetting < 1) {
			log_debug("Interface at index %d of USB device (bus: %u, device: %u) has no alt setting, ignoring it",
			          i, bus_number, device_address);

			continue;
		}

		interface_descriptor = &config_descriptor->interface[i].altsetting[0];

		if (interface_descriptor->bInterfaceNumber != interface_number) {
			continue;
		}

		if (interface_descriptor->bNumEndpoints != 2) {
			log_debug("Interface %d of USB device (bus: %u, device: %u) has %d endpoints, expecting 2, ignoring it",
			          interface_descriptor->bInterfaceNumber, bus_number, device_address,
			          interface_descriptor->bNumEndpoints);

			continue;
		}

		for (k = 0; k < interface_descriptor->bNumEndpoints; ++k) {
			endpoint_descriptor = &interface_descriptor->endpoint[k];

			if (endpoint_descriptor->bEndpointAddress & LIBUSB_ENDPOINT_IN) {
				*endpoint_in = endpoint_descriptor->bEndpointAddress;
			} else {
				*endpoint_out = endpoint_descriptor->bEndpointAddress;
			}
		}

		libusb_free_config_descriptor(config_descriptor);

		return 0;
	}

	libusb_free_config_descriptor(config_descriptor);

	return -1;
}

int usb_get_device_name(libusb_device_handle *device_handle, char *name, int length) {
	int rc;
	libusb_device *device = libusb_get_device(device_handle);
	uint8_t bus_number = libusb_get_bus_number(device);
	uint8_t device_address = libusb_get_device_address(device);
	struct libusb_device_descriptor descriptor;
	char product[64];
	char serial_number[64];

	// get device descriptor
	rc = libusb_get_device_descriptor(device, &descriptor);

	if (rc < 0) {
		log_error("Could not get device descriptor for USB device (bus: %u, device: %u): %s (%d)",
		          bus_number, device_address, usb_get_error_name(rc), rc);

		return -1;
	}

	// get product string descriptor
	rc = libusb_get_string_descriptor_ascii(device_handle,
	                                        descriptor.iProduct,
	                                        (unsigned char *)product,
	                                        sizeof(product));

	if (rc < 0) {
		log_error("Could not get product string descriptor for USB device (bus: %u, device: %u): %s (%d)",
		          bus_number, device_address, usb_get_error_name(rc), rc);

		return -1;
	}

	// get serial number string descriptor
	rc = libusb_get_string_descriptor_ascii(device_handle,
	                                        descriptor.iSerialNumber,
	                                        (unsigned char *)serial_number,
	                                        sizeof(serial_number));

	if (rc < 0) {
		log_error("Could not get serial number string descriptor for USB device (bus: %u, device: %u): %s (%d)",
		          bus_number, device_address, usb_get_error_name(rc), rc);

		return -1;
	}

	// format name
	snprintf(name, length, "%s [%s]", product, serial_number);

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
