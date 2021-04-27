/*
 * brickd
 * Copyright (C) 2017, 2019-2021 Matthias Bolte <matthias@tinkerforge.com>
 *
 * libusb.c: dlopen wrapper for libusb API
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

#include <dlfcn.h>
#include <errno.h>
#include <stdlib.h>

#include <daemonlib/log.h>
#include <daemonlib/macros.h>

#include "libusb.h"

static LogSource _log_source = LOG_SOURCE_INITIALIZER;

static const char *_libusb = "libusb-1.0.so";
static void *_libusb_handle = NULL;

libusb_init_t libusb_init;
libusb_exit_t libusb_exit;
libusb_set_debug_t libusb_set_debug;
libusb_set_log_cb_t libusb_set_log_cb; // 1.0.23
libusb_has_capability_t libusb_has_capability;

libusb_get_device_list_t libusb_get_device_list;
libusb_free_device_list_t libusb_free_device_list;
libusb_ref_device_t libusb_ref_device;
libusb_unref_device_t libusb_unref_device;

libusb_get_device_descriptor_t libusb_get_device_descriptor;
libusb_get_config_descriptor_t libusb_get_config_descriptor;
libusb_free_config_descriptor_t libusb_free_config_descriptor;

libusb_get_bus_number_t libusb_get_bus_number;
libusb_get_device_address_t libusb_get_device_address;

libusb_open_t libusb_open;
libusb_close_t libusb_close;
libusb_get_device_t libusb_get_device;

libusb_claim_interface_t libusb_claim_interface;
libusb_release_interface_t libusb_release_interface;

libusb_clear_halt_t libusb_clear_halt;

libusb_alloc_transfer_t libusb_alloc_transfer;
libusb_submit_transfer_t libusb_submit_transfer;
libusb_cancel_transfer_t libusb_cancel_transfer;
libusb_free_transfer_t libusb_free_transfer;

libusb_get_string_descriptor_ascii_t libusb_get_string_descriptor_ascii;

libusb_handle_events_timeout_t libusb_handle_events_timeout;

libusb_get_pollfds_t libusb_get_pollfds;
libusb_free_pollfds_t libusb_free_pollfds;
libusb_set_pollfd_notifiers_t libusb_set_pollfd_notifiers;

libusb_hotplug_register_callback_t libusb_hotplug_register_callback;
libusb_hotplug_deregister_callback_t libusb_hotplug_deregister_callback;

#if defined(__clang__) || !defined(__GNUC__) || __GNUC_PREREQ(4, 6)

// according to dlopen manpage casting from "void *" to a function pointer
// is undefined in C99. the manpage suggests this workaround defined in the
// Technical Corrigendum 1 of POSIX.1-2003:
//
//  double (*cosine)(double);
//  *(void **)(&cosine) = dlsym(handle, "cos");
#define LIBUSB_DLSYM(name) do { *(void **)&name = dlsym(_libusb_handle, #name); } while (0)

#else

// older GCC versions complain about the workaround suggested by POSIX:
//
//  warning: dereferencing type-punned pointer will break strict-aliasing rules
//
// use a union to workaround this
#define LIBUSB_DLSYM(name) do { union { name##_t function; void *data; } alias; \
                                alias.data = dlsym(_libusb_handle, #name); \
                                name = alias.function; } while (0)

#endif

int libusb_init_dlopen(void) {
	_libusb_handle = dlopen(_libusb, RTLD_LAZY);

	if (_libusb_handle == NULL) {
		log_error("Could not load %s: %s", _libusb, dlerror());

		return -1;
	}

	log_debug("Successfully loaded %s", _libusb);

	LIBUSB_DLSYM(libusb_init);
	LIBUSB_DLSYM(libusb_exit);
	LIBUSB_DLSYM(libusb_set_debug);
	LIBUSB_DLSYM(libusb_set_log_cb); // 1.0.23
	LIBUSB_DLSYM(libusb_has_capability);

	LIBUSB_DLSYM(libusb_get_device_list);
	LIBUSB_DLSYM(libusb_free_device_list);
	LIBUSB_DLSYM(libusb_ref_device);
	LIBUSB_DLSYM(libusb_unref_device);

	LIBUSB_DLSYM(libusb_get_device_descriptor);
	LIBUSB_DLSYM(libusb_get_config_descriptor);
	LIBUSB_DLSYM(libusb_free_config_descriptor);

	LIBUSB_DLSYM(libusb_get_bus_number);
	LIBUSB_DLSYM(libusb_get_device_address);

	LIBUSB_DLSYM(libusb_open);
	LIBUSB_DLSYM(libusb_close);
	LIBUSB_DLSYM(libusb_get_device);

	LIBUSB_DLSYM(libusb_claim_interface);
	LIBUSB_DLSYM(libusb_release_interface);

	LIBUSB_DLSYM(libusb_clear_halt);

	LIBUSB_DLSYM(libusb_alloc_transfer);
	LIBUSB_DLSYM(libusb_submit_transfer);
	LIBUSB_DLSYM(libusb_cancel_transfer);
	LIBUSB_DLSYM(libusb_free_transfer);

	LIBUSB_DLSYM(libusb_get_string_descriptor_ascii);

	LIBUSB_DLSYM(libusb_handle_events_timeout);

	LIBUSB_DLSYM(libusb_get_pollfds);
	LIBUSB_DLSYM(libusb_free_pollfds);
	LIBUSB_DLSYM(libusb_set_pollfd_notifiers);

	LIBUSB_DLSYM(libusb_hotplug_register_callback);
	LIBUSB_DLSYM(libusb_hotplug_deregister_callback);

	return 0;
}

void libusb_exit_dlopen(void) {
	log_debug("Unloading %s", _libusb);

	dlclose(_libusb_handle);
}
