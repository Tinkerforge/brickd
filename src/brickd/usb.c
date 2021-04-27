/*
 * brickd
 * Copyright (C) 2012-2021 Matthias Bolte <matthias@tinkerforge.com>
 * Copyright (C) 2014 Olaf Lüke <olaf@tinkerforge.com>
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
#include <daemonlib/pipe.h>
#include <daemonlib/utils.h>

#include "usb.h"

#include "stack.h"
#include "network.h"
#include "usb_transfer.h"

static LogSource _log_source = LOG_SOURCE_INITIALIZER;
static LogSource _libusb_log_source = LOG_SOURCE_INITIALIZER;

static libusb_context *_context = NULL;
static Pipe _hotplug_pipe;
static Array _usb_stacks;

extern int usb_init_platform(libusb_context *context);
extern void usb_exit_platform(libusb_context *context);
extern void usb_handle_events_platform(libusb_context *context);

#ifdef LIBUSB_BRICKD_PATCH

static void LIBUSB_CALL usb_forward_message(libusb_context *ctx,
                                            enum libusb_log_level level_,
                                            const char *function,
                                            const char *format,
                                            va_list arguments) {
	LogLevel level;
	LogDebugGroup debug_group;
	uint32_t inclusion;
	char buffer[1024] = "<unknown>";

	(void)ctx;

	switch (level_) {
	case LIBUSB_LOG_LEVEL_ERROR:   level = LOG_LEVEL_ERROR; break;
	case LIBUSB_LOG_LEVEL_WARNING: level = LOG_LEVEL_WARN;  break;
	case LIBUSB_LOG_LEVEL_INFO:    level = LOG_LEVEL_INFO;  break;
	case LIBUSB_LOG_LEVEL_DEBUG:   level = LOG_LEVEL_DEBUG; break;
	default:                                                return;
	}

	if (level == LOG_LEVEL_DEBUG) {
		debug_group = LOG_DEBUG_GROUP_LIBUSB;
	} else {
		debug_group = LOG_DEBUG_GROUP_NONE;
	}

	inclusion = log_check_inclusion(level, &_libusb_log_source, debug_group, -1);

	if (inclusion != LOG_INCLUSION_NONE) {
		vsnprintf(buffer, sizeof(buffer), format, arguments);

		log_message(level, &_libusb_log_source, debug_group, inclusion, function,
		            -1, "%s", buffer);
	}
}

#elif defined BRICKD_WITH_LIBUSB_DLOPEN || (!defined BRICKD_WITH_UNKNOWN_LIBUSB_API_VERSION && LIBUSB_API_VERSION >= 0x01000107) // libusb 1.0.23

static void LIBUSB_CALL usb_forward_message(libusb_context *ctx,
                                            enum libusb_log_level level_,
                                            const char *message) {
	LogLevel level;
	LogDebugGroup debug_group;
	uint32_t inclusion;
	char buffer[1024] = "<unknown>";

	(void)ctx;

	switch (level_) {
	case LIBUSB_LOG_LEVEL_ERROR:   level = LOG_LEVEL_ERROR; break;
	case LIBUSB_LOG_LEVEL_WARNING: level = LOG_LEVEL_WARN;  break;
	case LIBUSB_LOG_LEVEL_INFO:    level = LOG_LEVEL_INFO;  break;
	case LIBUSB_LOG_LEVEL_DEBUG:   level = LOG_LEVEL_DEBUG; break;
	default:                                                return;
	}

	if (level == LOG_LEVEL_DEBUG) {
		debug_group = LOG_DEBUG_GROUP_LIBUSB;
	} else {
		debug_group = LOG_DEBUG_GROUP_NONE;
	}

	inclusion = log_check_inclusion(level, &_libusb_log_source, debug_group, -1);

	if (inclusion != LOG_INCLUSION_NONE) {
		string_copy(buffer, sizeof(buffer), message, strcspn(message, "\r\n"));

		log_message(level, &_libusb_log_source, debug_group, inclusion, NULL,
		            -1, "%s", buffer);
	}
}

#endif

static void usb_forward_hotplug(void *opaque) {
	uint8_t byte;
	int repeat;

	(void)opaque;

	if (pipe_read(&_hotplug_pipe, &byte, sizeof(byte)) < 0) {
		log_error("Could not read from USB hotplug pipe: %s (%d)",
		          get_errno_name(errno), errno);

		return;
	}

	for (repeat = 0; repeat < 100; ++repeat) {
		// consume all accumulated hotplug events in one go, but atmost
		// 100 to avoid getting stuck here forever
		if (pipe_read(&_hotplug_pipe, &byte, sizeof(byte)) < 0) {
			if (errno_would_block()) {
				break;
			}

			log_error("Could not read from USB hotplug pipe: %s (%d)",
			          get_errno_name(errno), errno);

			return;
		}
	}

	log_debug("Starting USB device scan, triggered by hotplug (repeat: %d)", repeat);

	usb_rescan();
}

static int usb_enumerate(void) {
	int result = -1;
	libusb_device **devices;
	libusb_device *device;
	int rc;
	int i = 0;
	struct libusb_device_descriptor descriptor;
	uint8_t bus_number;
	uint8_t device_address;
	bool red_brick;
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

	log_debug("Found %d USB device(s)", rc);

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
				log_warn("USB device (bus: %u, device: %u) has unsupported protocol 1.0 firmware, please update firmware, ignoring USB device",
				         bus_number, device_address);

				continue;
			}

			red_brick = false;
		} else if (descriptor.idVendor == USB_RED_BRICK_VENDOR_ID &&
		           descriptor.idProduct == USB_RED_BRICK_PRODUCT_ID) {
			if (descriptor.bcdDevice < USB_RED_BRICK_DEVICE_RELEASE) {
				log_warn("USB device (bus: %u, device: %u) has unexpected release version, ignoring USB device",
				         bus_number, device_address);

				continue;
			}

			red_brick = true;
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

		if (usb_stack_create(usb_stack, _context, device, red_brick) < 0) {
			array_remove(&_usb_stacks, _usb_stacks.count - 1, NULL);

			log_warn("USB device (bus: %u, device: %u) could not be acquired correctly, ignoring USB device",
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

static void usb_set_debug(libusb_context *context, int level) {
#if !defined BRICKD_WITH_UNKNOWN_LIBUSB_API_VERSION && LIBUSB_API_VERSION >= 0x01000106 // libusb 1.0.22
	libusb_set_option(context, LIBUSB_OPTION_LOG_LEVEL, level); // avoid deprecation warning for libusb_set_debug
#else
	libusb_set_debug(context, level);
#endif
}

int usb_init(void) {
	int phase = 0;
	int rc;

	log_debug("Initializing USB subsystem");

	_libusb_log_source.file = "libusb";
	_libusb_log_source.name = "libusb";
	_libusb_log_source.libusb = true;

#ifdef LIBUSB_BRICKD_PATCH
	libusb_set_log_callback(usb_forward_message);
#elif defined BRICKD_WITH_LIBUSB_DLOPEN
	if (libusb_set_log_cb != NULL) {
		libusb_set_log_cb(NULL, usb_forward_message, LIBUSB_LOG_CB_GLOBAL);
	}
#elif !defined BRICKD_WITH_UNKNOWN_LIBUSB_API_VERSION && LIBUSB_API_VERSION >= 0x01000107 // libusb 1.0.23
	libusb_set_log_cb(NULL, usb_forward_message, LIBUSB_LOG_CB_GLOBAL);
#endif

	// create hotplug pipe
	if (pipe_create(&_hotplug_pipe, PIPE_FLAG_NON_BLOCKING_READ) < 0) {
		log_error("Could not create USB hotplug pipe: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 1;

	if (event_add_source(_hotplug_pipe.base.read_handle, EVENT_SOURCE_TYPE_GENERIC,
	                     "usb-hotplug", EVENT_READ, usb_forward_hotplug, NULL) < 0) {
		goto cleanup;
	}

	phase = 2;

	// configure logging
	switch (log_get_effective_level()) {
	case LOG_LEVEL_ERROR:
		putenv("LIBUSB_DEBUG=1");
		break;

	case LOG_LEVEL_WARN:
		putenv("LIBUSB_DEBUG=2");
		break;

	case LOG_LEVEL_INFO:
		putenv("LIBUSB_DEBUG=3");
		break;

	case LOG_LEVEL_DEBUG:
		if (log_check_inclusion(LOG_LEVEL_DEBUG, &_libusb_log_source,
		                        LOG_DEBUG_GROUP_LIBUSB, -1) != LOG_INCLUSION_NONE) {
			putenv("LIBUSB_DEBUG=4");
		} else {
			putenv("LIBUSB_DEBUG=3");
		}

		break;

	default:
		break;
	}

	// initialize libusb context
	rc = libusb_init(&_context);

	if (rc < 0) {
		log_error("Could not initialize libusb context: %s (%d)",
		          usb_get_error_name(rc), rc);

		goto cleanup;
	}

	phase = 3;

	switch (log_get_effective_level()) {
	case LOG_LEVEL_ERROR:
		usb_set_debug(_context, LIBUSB_LOG_LEVEL_ERROR);
		break;

	case LOG_LEVEL_WARN:
		usb_set_debug(_context, LIBUSB_LOG_LEVEL_WARNING);
		break;

	case LOG_LEVEL_INFO:
		usb_set_debug(_context, LIBUSB_LOG_LEVEL_INFO);
		break;

	case LOG_LEVEL_DEBUG:
		if (log_check_inclusion(LOG_LEVEL_DEBUG, &_libusb_log_source,
		                        LOG_DEBUG_GROUP_LIBUSB, -1) != LOG_INCLUSION_NONE) {
			usb_set_debug(_context, LIBUSB_LOG_LEVEL_DEBUG);
		} else {
			usb_set_debug(_context, LIBUSB_LOG_LEVEL_INFO);
		}

		break;

	default:
		break;
	}

	if (usb_init_platform(_context)) {
		goto cleanup;
	}

	phase = 4;

	// create USB stack array. the USBStack struct is not relocatable, because
	// its USB transfers keep a pointer to it
	if (array_create(&_usb_stacks, 32, sizeof(USBStack), false) < 0) {
		log_error("Could not create USB stack array: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 5;

	log_debug("Starting initial USB device scan");

	if (usb_rescan() < 0) {
		goto cleanup;
	}

	phase = 6;

cleanup:
	switch (phase) { // no breaks, all cases fall through intentionally
	case 5:
		array_destroy(&_usb_stacks, (ItemDestroyFunction)usb_stack_destroy);
		// fall through

	case 4:
		usb_exit_platform(_context);
		// fall through

	case 3:
		libusb_exit(_context);
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

	return phase == 6 ? 0 : -1;
}

void usb_exit(void) {
	log_debug("Shutting down USB subsystem");

	array_destroy(&_usb_stacks, (ItemDestroyFunction)usb_stack_destroy);

	usb_exit_platform(_context);

	libusb_exit(_context);

	event_remove_source(_hotplug_pipe.base.read_handle, EVENT_SOURCE_TYPE_GENERIC);
	pipe_destroy(&_hotplug_pipe);

#ifdef LIBUSB_BRICKD_PATCH
	libusb_set_log_callback(NULL);
#elif defined BRICKD_WITH_LIBUSB_DLOPEN
	if (libusb_set_log_cb != NULL) {
		libusb_set_log_cb(NULL, NULL, LIBUSB_LOG_CB_GLOBAL);
	}
#elif !defined BRICKD_WITH_UNKNOWN_LIBUSB_API_VERSION && LIBUSB_API_VERSION >= 0x01000107 // libusb 1.0.23
	libusb_set_log_cb(NULL, NULL, LIBUSB_LOG_CB_GLOBAL);
#endif
}

void usb_handle_events(void) {
	usb_handle_events_platform(_context);
}

void usb_handle_hotplug(void) {
	uint8_t byte = 0;

	if (pipe_write(&_hotplug_pipe, &byte, sizeof(byte)) < 0) {
		log_error("Could not write to USB hotplug pipe: %s (%d)",
		          get_errno_name(errno), errno);
	}
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

		log_info("Removing USB device (bus: %u, device: %u) at index %d: %s",
		         usb_stack->bus_number, usb_stack->device_address, i,
		         usb_stack->base.name);

		stack_announce_disconnect(&usb_stack->base);

		array_remove(&_usb_stacks, i, (ItemDestroyFunction)usb_stack_destroy);
	}

	return 0;
}

int usb_reopen(USBStack *usb_stack) {
	Array recipients;
	int i;
	USBStack *candidate;
	uint8_t bus_number;
	uint8_t device_address;
	libusb_device *device;
	bool red_brick;
	bool found = false;

	if (usb_stack == NULL) {
		log_info("Reopening all USB devices");
	}

	if (array_create(&recipients, 1, sizeof(Recipient), true) < 0) {
		log_error("Could not create temporary recipient array: %s (%d)",
		          get_errno_name(errno), errno);

		return -1;
	}

	// iterate backwards for simpler index handling and to avoid memmove in
	// array_remove call
	for (i = _usb_stacks.count - 1; i >= 0; --i) {
		candidate = array_get(&_usb_stacks, i);

		if (usb_stack != NULL && candidate != usb_stack) {
			continue;
		}

		log_info("Reopening USB device (bus: %u, device: %u) at index %d: %s",
		         candidate->bus_number, candidate->device_address, i,
		         candidate->base.name);

		bus_number = candidate->bus_number;
		device_address = candidate->device_address;
		device = libusb_ref_device(candidate->device);
		red_brick = candidate->red_brick;

		array_swap(&candidate->base.recipients, &recipients);

		usb_stack_destroy(candidate);

		if (usb_stack_create(candidate, _context, device, red_brick) < 0) {
			array_remove(&_usb_stacks, i, NULL);

			log_warn("Could not reopen USB device (bus: %u, device: %u) due to an error",
			         bus_number, device_address);

			recipients_announce_disconnect(&recipients);
		} else {
			array_swap(&recipients, &candidate->base.recipients);
		}

		libusb_unref_device(device);

		if (usb_stack != NULL && candidate == usb_stack) {
			found = true;

			break;
		}
	}

	if (usb_stack != NULL && !found) {
		log_error("Could not find USB device (bus: %u, device: %u) to reopen: %s",
		          usb_stack->bus_number, usb_stack->device_address, usb_stack->base.name);
	}

	array_destroy(&recipients, NULL);

	return usb_rescan();
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

	if (config_descriptor->bNumInterfaces == 0) {
		log_error("Config descriptor for USB device (bus: %u, device: %u) contains no interfaces",
		          bus_number, device_address);

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
