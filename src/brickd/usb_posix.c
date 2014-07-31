/*
 * brickd
 * Copyright (C) 2013-2014 Matthias Bolte <matthias@tinkerforge.com>
 *
 * usb_posix.c: POSIX based USB specific functions
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
 * libusb provides hotplug support on Linux and Mac OS X since 1.0.16. with
 * earlier versions the application (brickd) had to do its own hotplug handling.
 * brickd uses libudev to listen for uevents on Linux and used IOKit to listen
 * for notifications on Mac OS X. since brickd is shipped with a custom libusb
 * version (hotplug capable) on Mac OS X, IOKit is not used directly anymore.
 * on each add/remove uevent brickd calls libusb_get_device_list and compares
 * the result to the result of the previous call. the difference between the
 * two results allows to detect added and removed USB devices.
 *
 * the following text explains the problem with this approach in terms of
 * uevents on Linux, but the problem was exactly the same with notifications
 * on Mac OS X.
 *
 * the hotplug handling in brickd only works well until libusb 1.0.16, because
 * this release changed the way libusb_get_device_list works internally on Linux
 * and Mac OS X. before 1.0.16 each call to libusb_get_device_list triggered a
 * full enumeration of all connected USB devices, nothing was cached inside of
 * libusb. but since 1.0.16 libusb keeps a cache of connected USB devices that
 * libusb_get_device_list returns. this cache is updated by the new hotplug
 * mechanism of libusb 1.0.16.
 *
 * libusb can use libudev or netlink to listen for uevents on Linux and uses
 * IOKit to listen for notifications on Mac OS X. here is the problem. because
 * brickd and libusb listen for the same uevents there is a race between the
 * two. if libusb receives a uevent first then it will update its cache first.
 * by the time brickd receives the same uevent the list of USB devices returned
 * by libusb_get_device_list is up-to-date and brickd can check for changes.
 * but if brickd receives an uevent first then it calls libusb_get_device_list
 * and gets a list that is not up-to-date, because libusb did not receive the
 * same uevent yet. then the difference between the result of this call and
 * the last call is empty and brickd assumes that no USB device was added or
 * removed. now libusb receives the uevent and updates its cache. the next time
 * brickd receives an uevent before libusb it will see the difference in the
 * results that was created by the previous uevent. overall this race results
 * in brickd seeing USB device additions and removals off by one uevent.
 *
 * to avoid this race brickd does not listen for uevents on its own if libusb
 * supports hotplug. instead brickd uses the libusb hotplug mechanism and only
 * falls back to libudev for older libusb versions without hotplug support. now
 * there is only either libusb or brickd listening for uevents and no race is
 * possible.
 *
 * because the new hotplug functions are not available in all libusb versions
 * that brickd supports (1.0.6 and newer) they have to be resolved at runtime.
 * this allows to compile one binary that supports multiple libusb versions.
 */

#include <dlfcn.h>
#include <errno.h>
#include <libusb.h>
#include <stdlib.h>

#include <daemonlib/log.h>
#include <daemonlib/macros.h>

#include "usb.h"

#define LOG_CATEGORY LOG_CATEGORY_USB

// if compiled with a hotplug capable libusb then don't use dlopen to probe
#if (defined(LIBUSB_API_VERSION)  && (LIBUSB_API_VERSION  >= 0x01000102)) || \
    (defined(LIBUSBX_API_VERSION) && (LIBUSBX_API_VERSION >= 0x01000102))
	#define DLOPEN_HOTPLUG_FUNCTIONS 0
#else
	#define DLOPEN_HOTPLUG_FUNCTIONS 1
#endif

#if DLOPEN_HOTPLUG_FUNCTIONS

#define LIBUSBZ_HOTPLUG_MATCH_ANY -1

typedef enum {
	LIBUSBZ_CAP_HAS_CAPABILITY = 0x0000,
	LIBUSBZ_CAP_HAS_HOTPLUG    = 0x0001
} libusbz_capability;

typedef enum {
	LIBUSBZ_HOTPLUG_ENUMERATE = 1,
} libusbz_hotplug_flag;

typedef enum {
	LIBUSBZ_HOTPLUG_EVENT_DEVICE_ARRIVED = 0x01,
	LIBUSBZ_HOTPLUG_EVENT_DEVICE_LEFT    = 0x02,
} libusbz_hotplug_event;

typedef int libusbz_hotplug_callback_handle;
typedef int LIBUSB_CALL (*libusbz_hotplug_callback_fn)(libusb_context *ctx, libusb_device *device, libusbz_hotplug_event event, void *user_data);

typedef int LIBUSB_CALL (*libusbz_has_capability_t)(uint32_t capability);
typedef int LIBUSB_CALL (*libusbz_hotplug_register_callback_t)(libusb_context *ctx, libusbz_hotplug_event events, libusbz_hotplug_flag flags, int vendor_id, int product_id, int dev_class, libusbz_hotplug_callback_fn cb_fn, void *user_data, libusbz_hotplug_callback_handle *handle);
typedef void LIBUSB_CALL (*libusbz_hotplug_deregister_callback_t)(libusb_context *ctx, libusbz_hotplug_callback_handle handle);

static libusbz_has_capability_t libusbz_has_capability = NULL;
static libusbz_hotplug_register_callback_t libusbz_hotplug_register_callback = NULL;
static libusbz_hotplug_deregister_callback_t libusbz_hotplug_deregister_callback = NULL;

static void *_libusb_handle = NULL;

#if defined(__clang__) || !defined(__GNUC__) || __GNUC_PREREQ(4, 6)

// according to dlopen manpage casting from "void *" to a function pointer
// is undefined in C99. the manpage suggests this workaround defined in the
// Technical Corrigendum 1 of POSIX.1-2003:
//
//  double (*cosine)(double);
//  *(void **)(&cosine) = dlsym(handle, "cos");
#define USB_DLSYM(name, variable) do { *(void **)&variable = dlsym(_libusb_handle, #name); } while (0)

#else

// older GCC versions complain about the workaround suggested by POSIX:
//
//  warning: dereferencing type-punned pointer will break strict-aliasing rules
//
// use a union to workaround this
#define USB_DLSYM(name, variable) do { union { variable##_t function; void *data; } alias; \
                                       alias.data = dlsym(_libusb_handle, #name); \
                                       variable = alias.function; } while (0)

#endif

static int usb_dlopen(void) {
	_libusb_handle = dlopen(NULL, RTLD_LAZY);

	if (_libusb_handle == NULL) {
		log_error("Could not load brickd (for libusb symbols): %s", dlerror());

		return -1;
	}

	log_debug("Successfully loaded brickd (for libusb symbols)");

	USB_DLSYM(libusb_has_capability, libusbz_has_capability);
	USB_DLSYM(libusb_hotplug_register_callback, libusbz_hotplug_register_callback);
	USB_DLSYM(libusb_hotplug_deregister_callback, libusbz_hotplug_deregister_callback);

	return 0;
}

static void usb_dlclose(void) {
	log_debug("Unloading brickd (for libusb symbols)");

	dlclose(_libusb_handle);
}

#else

#define LIBUSBZ_HOTPLUG_MATCH_ANY LIBUSB_HOTPLUG_MATCH_ANY

#define LIBUSBZ_CAP_HAS_CAPABILITY LIBUSB_CAP_HAS_CAPABILITY
#define LIBUSBZ_CAP_HAS_HOTPLUG LIBUSB_CAP_HAS_HOTPLUG

#define LIBUSBZ_HOTPLUG_EVENT_DEVICE_ARRIVED LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED
#define LIBUSBZ_HOTPLUG_EVENT_DEVICE_LEFT LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT

#define libusbz_hotplug_event libusb_hotplug_event
#define libusbz_hotplug_callback_handle libusb_hotplug_callback_handle

#define libusbz_has_capability libusb_has_capability
#define libusbz_hotplug_register_callback libusb_hotplug_register_callback
#define libusbz_hotplug_deregister_callback libusb_hotplug_deregister_callback

#endif

static libusbz_hotplug_callback_handle _brick_hotplug_handle;
static libusbz_hotplug_callback_handle _red_brick_hotplug_handle;

static int LIBUSB_CALL usb_handle_hotplug(libusb_context *context, libusb_device *device,
                                          libusbz_hotplug_event event, void *user_data) {
	uint8_t bus_number = libusb_get_bus_number(device);
	uint8_t device_address = libusb_get_device_address(device);

	(void)context;
	(void)user_data;

	switch (event) {
	case LIBUSBZ_HOTPLUG_EVENT_DEVICE_ARRIVED:
		log_debug("Received libusb hotplug event (event: arrived, bus: %u, device: %u)",
		          bus_number, device_address);

		usb_rescan();

		break;

	case LIBUSBZ_HOTPLUG_EVENT_DEVICE_LEFT:
		log_debug("Received libusb hotplug event (event: left, bus: %u, device: %u)",
		          bus_number, device_address);

		usb_rescan();

		break;

	default:
		log_debug("Ignoring libusb hotplug event (event: %d, bus: %u, device: %u)",
		          event, bus_number, device_address);

		break;
	}

	return 0;
}

int usb_init_platform(void) {
#if DLOPEN_HOTPLUG_FUNCTIONS
	if (usb_dlopen() < 0) {
		return -1;
	}
#endif

	return 0;
}

void usb_exit_platform(void) {
#if DLOPEN_HOTPLUG_FUNCTIONS
	usb_dlclose();
#endif
}

int usb_init_hotplug(libusb_context *context) {
	int rc;

	// cannot use LIBUSBZ_HOTPLUG_ENUMERATE here for initial enumeration,
	// because it is broken in libusb 1.0.16 as in calling libusb functions
	// from the hotplug callback might deadlock.
	rc = libusbz_hotplug_register_callback(context,
	                                       LIBUSBZ_HOTPLUG_EVENT_DEVICE_ARRIVED |
	                                       LIBUSBZ_HOTPLUG_EVENT_DEVICE_LEFT, 0,
	                                       USB_BRICK_VENDOR_ID, USB_BRICK_PRODUCT_ID,
	                                       LIBUSBZ_HOTPLUG_MATCH_ANY,
	                                       usb_handle_hotplug, NULL,
	                                       &_brick_hotplug_handle);

	if (rc < 0) {
		log_error("Could not register libusb hotplug callback: %s (%d)",
		          usb_get_error_name(rc), rc);

		return -1;
	}

	rc = libusbz_hotplug_register_callback(context,
	                                       LIBUSBZ_HOTPLUG_EVENT_DEVICE_ARRIVED |
	                                       LIBUSBZ_HOTPLUG_EVENT_DEVICE_LEFT, 0,
	                                       USB_RED_BRICK_VENDOR_ID, USB_RED_BRICK_PRODUCT_ID,
	                                       LIBUSBZ_HOTPLUG_MATCH_ANY,
	                                       usb_handle_hotplug, NULL,
	                                       &_brick_hotplug_handle);

	if (rc < 0) {
		libusbz_hotplug_deregister_callback(context, _brick_hotplug_handle);

		log_error("Could not register libusb hotplug callback: %s (%d)",
		          usb_get_error_name(rc), rc);

		return -1;
	}

	return 0;
}

void usb_exit_hotplug(libusb_context *context) {
	libusbz_hotplug_deregister_callback(context, _brick_hotplug_handle);
	libusbz_hotplug_deregister_callback(context, _red_brick_hotplug_handle);
}

bool usb_has_hotplug(void) {
#if DLOPEN_HOTPLUG_FUNCTIONS
	return libusbz_has_capability != NULL &&
	       libusbz_hotplug_register_callback != NULL &&
	       libusbz_hotplug_deregister_callback != NULL &&
	       libusbz_has_capability(LIBUSBZ_CAP_HAS_CAPABILITY) &&
	       libusbz_has_capability(LIBUSBZ_CAP_HAS_HOTPLUG);
#else
	return libusbz_has_capability(LIBUSBZ_CAP_HAS_CAPABILITY) &&
	       libusbz_has_capability(LIBUSBZ_CAP_HAS_HOTPLUG);
#endif
}
