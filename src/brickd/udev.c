/*
 * brickd
 * Copyright (C) 2012-2013 Matthias Bolte <matthias@tinkerforge.com>
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

#ifdef BRICKD_WITH_LIBUDEV_DLOPEN
	#include <dlfcn.h>
#endif
#include <errno.h>
#include <poll.h>
#ifndef BRICKD_WITH_LIBUDEV_DLOPEN
	#include <libudev.h>
#endif
#include <string.h>

#include "udev.h"

#include "event.h"
#include "log.h"
#include "usb.h"

#define LOG_CATEGORY LOG_CATEGORY_HOTPLUG

#ifdef BRICKD_WITH_LIBUDEV_DLOPEN

struct udev;
struct udev_monitor;
struct udev_device;

static struct udev_device *(*udev_monitor_receive_device)(struct udev_monitor *udev_monitor) = NULL;
static const char *(*udev_device_get_action)(struct udev_device *udev_device) = NULL;
static const char *(*udev_device_get_devnode)(struct udev_device *udev_device) = NULL;
static const char *(*udev_device_get_sysname)(struct udev_device *udev_device) = NULL;
static void (*udev_device_unref)(struct udev_device *udev_device) = NULL;
static struct udev *(*udev_new)(void) = NULL;
static struct udev_monitor *(*udev_monitor_new_from_netlink)(struct udev *udev, const char *name) = NULL;
static int (*udev_monitor_filter_add_match_subsystem_devtype)(struct udev_monitor *udev_monitor, const char *subsystem, const char *devtype) = NULL;
static int (*udev_monitor_enable_receiving)(struct udev_monitor *udev_monitor) = NULL;
static int (*udev_monitor_get_fd)(struct udev_monitor *udev_monitor) = NULL;
static void (*udev_monitor_unref)(struct udev_monitor *udev_monitor) = NULL;
static void (*udev_unref)(struct udev *udev) = NULL;

#endif

static struct udev *_udev_context = NULL;
static struct udev_monitor *_udev_monitor = NULL;
static int _udev_monitor_fd = -1;

#ifdef BRICKD_WITH_LIBUDEV_DLOPEN

static const char *_libudev0 = "libudev.so.0";
static const char *_libudev1 = "libudev.so.1";
static void *_libudev_handle = NULL;
static const char *_loaded_libudev = "<unknown>";
static int _dlsym_error = 0;

// according to dlopen manpage casting from "void *" to a function pointer
// is undefined in C99. the manpage suggests this workaround defined in the
// Technical Corrigendum 1 of POSIX.1-2003:
//
//  double (*cosine)(double);
//  *(void **)(&cosine) = dlsym(handle, "cos");
#define UDEV_DLSYM(name) do { *(void **)&name = udev_dlsym(#name); } while (0)

static void *udev_dlsym(const char *name) {
	void *pointer;
	char *error;

	if (_dlsym_error) {
		return NULL;
	}

	dlerror(); // clear any existing error

	pointer = dlsym(_libudev_handle, name);
	error = dlerror();

	if (error != NULL) {
		log_error("Could not resolve '%s': %s", name, error);

		_dlsym_error = 1;
	}

	return pointer;
}

// using dlopen for libudev allows to deal with both SONAMEs for libudev:
// libudev.so.0 and libudev.so.1
static int udev_dlopen(void) {
	log_debug("Trying to load %s", _libudev1);

	_libudev_handle = dlopen(_libudev1, RTLD_LAZY);

	if (_libudev_handle == NULL) {
		log_debug("Could not load %s: %s", _libudev1, dlerror());
		log_debug("Trying to load %s instead", _libudev0);

		_libudev_handle = dlopen(_libudev0, RTLD_LAZY);

		if (_libudev_handle == NULL) {
			log_debug("Could not load %s either: %s", _libudev0, dlerror());
			log_error("Could not load %s nor %s", _libudev1, _libudev0);

			return -1;
		} else {
			_loaded_libudev = _libudev0;
		}
	} else {
		_loaded_libudev = _libudev1;
	}

	log_debug("Successfully loaded %s", _loaded_libudev);

	UDEV_DLSYM(udev_monitor_receive_device);
	UDEV_DLSYM(udev_device_get_action);
	UDEV_DLSYM(udev_device_get_devnode);
	UDEV_DLSYM(udev_device_get_sysname);
	UDEV_DLSYM(udev_device_unref);
	UDEV_DLSYM(udev_new);
	UDEV_DLSYM(udev_monitor_new_from_netlink);
	UDEV_DLSYM(udev_monitor_filter_add_match_subsystem_devtype);
	UDEV_DLSYM(udev_monitor_enable_receiving);
	UDEV_DLSYM(udev_monitor_get_fd);
	UDEV_DLSYM(udev_monitor_unref);
	UDEV_DLSYM(udev_unref);

	if (_dlsym_error) {
		dlclose(_libudev_handle);

		return -1;
	}

	return 0;
}

static void udev_dlclose(void) {
	log_debug("Unloading %s", _loaded_libudev);

	dlclose(_libudev_handle);
}

#endif

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

#ifdef BRICKD_WITH_LIBUDEV_DLOPEN
	if (udev_dlopen() < 0) {
		goto cleanup;
	}

	phase = 1;
#endif

	// create udev context
	_udev_context = udev_new();

	if (_udev_context == NULL) {
		log_error("Could not create udev context");

		goto cleanup;
	}

	phase = 2;

	// create udev monitor
	_udev_monitor = udev_monitor_new_from_netlink(_udev_context, "udev");

	if (_udev_monitor == NULL) {
		log_error("Could not initialize udev monitor");

		goto cleanup;
	}

	phase = 3;

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

	phase = 4;

cleanup:
	switch (phase) { // no breaks, all cases fall through intentionally
	case 3:
		udev_monitor_unref(_udev_monitor);

	case 2:
		udev_unref(_udev_context);

#ifdef BRICKD_WITH_LIBUDEV_DLOPEN
	case 1:
		udev_dlclose();
#endif

	default:
		break;
	}

	return phase == 4 ? 0 : -1;
}

void udev_exit(void) {
	log_debug("Shutting down udev subsystem");

	event_remove_source(_udev_monitor_fd, EVENT_SOURCE_TYPE_GENERIC); // FIXME: handle error?

	udev_monitor_unref(_udev_monitor);
	udev_unref(_udev_context);

#ifdef BRICKD_WITH_LIBUDEV_DLOPEN
	udev_dlclose();
#endif
}
