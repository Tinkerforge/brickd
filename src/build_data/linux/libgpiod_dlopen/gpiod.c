/*
 * brickd
 * Copyright (C) 2025 Matthias Bolte <matthias@tinkerforge.com>
 *
 * gpiod.h: dlopen wrapper for libgpiod2 API
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

#include "gpiod.h"

static LogSource _log_source = LOG_SOURCE_INITIALIZER;

static const char *_libgpiod2 = "libgpiod.so.2";
static void *_libgpiod2_handle = NULL;

static const char *_libgpiod3 = "libgpiod.so.3";
static void *_libgpiod3_handle = NULL;

int libgpiod_abi = 0;

// ABI 2 and 3
gpiod_chip_close_t gpiod_chip_close = NULL;

// ABI 2
gpiod_ctxless_find_line_t gpiod_ctxless_find_line = NULL;
gpiod_chip_open_by_name_t gpiod_chip_open_by_name = NULL;
gpiod_chip_get_line_t gpiod_chip_get_line = NULL;
gpiod_line_request_output_t gpiod_line_request_output = NULL;
gpiod_line_set_value_t gpiod_line_set_value = NULL;
gpiod_line_release_t gpiod_line_release = NULL;

// ABI 3
gpiod_is_gpiochip_device_t gpiod_is_gpiochip_device = NULL;
gpiod_chip_open_t gpiod_chip_open = NULL;
gpiod_chip_get_info_t gpiod_chip_get_info = NULL;
gpiod_chip_get_line_offset_from_name_t gpiod_chip_get_line_offset_from_name = NULL;
gpiod_chip_request_lines_t gpiod_chip_request_lines = NULL;
gpiod_chip_info_free_t gpiod_chip_info_free = NULL;
gpiod_chip_info_get_name_t gpiod_chip_info_get_name = NULL;
gpiod_line_settings_new_t gpiod_line_settings_new = NULL;
gpiod_line_settings_free_t gpiod_line_settings_free = NULL;
gpiod_line_settings_set_direction_t gpiod_line_settings_set_direction = NULL;
gpiod_line_settings_set_output_value_t gpiod_line_settings_set_output_value = NULL;
gpiod_line_config_new_t gpiod_line_config_new = NULL;
gpiod_line_config_free_t gpiod_line_config_free = NULL;
gpiod_line_config_add_line_settings_t gpiod_line_config_add_line_settings = NULL;
gpiod_line_request_release_t gpiod_line_request_release = NULL;
gpiod_line_request_set_value_t gpiod_line_request_set_value = NULL;
gpiod_request_config_new_t gpiod_request_config_new = NULL;
gpiod_request_config_free_t gpiod_request_config_free = NULL;
gpiod_request_config_set_consumer_t gpiod_request_config_set_consumer = NULL;

#if defined(__clang__) || !defined(__GNUC__) || __GNUC_PREREQ(4, 6)

// according to dlopen manpage casting from "void *" to a function pointer
// is undefined in C99. the manpage suggests this workaround defined in the
// Technical Corrigendum 1 of POSIX.1-2003:
//
//  double (*cosine)(double);
//  *(void **)(&cosine) = dlsym(handle, "cos");
#define LIBGPIOD_DLSYM(handle, name) do { *(void **)&name = dlsym(handle, #name); } while (0)

#else

// older GCC versions complain about the workaround suggested by POSIX:
//
//  warning: dereferencing type-punned pointer will break strict-aliasing rules
//
// use a union to workaround this
#define LIBGPIOD_DLSYM(handle, name) do { union { name##_t function; void *data; } alias; \
                                          alias.data = dlsym(handle, #name); \
                                          name = alias.function; } while (0)

#endif

int libgpiod_dlopen(void) {
	_libgpiod2_handle = dlopen(_libgpiod2, RTLD_LAZY);

	if (_libgpiod2_handle != NULL) {
		log_debug("Successfully loaded %s", _libgpiod2);

		libgpiod_abi = 2;

		LIBGPIOD_DLSYM(_libgpiod2_handle, gpiod_ctxless_find_line);
		LIBGPIOD_DLSYM(_libgpiod2_handle, gpiod_chip_open_by_name);
		LIBGPIOD_DLSYM(_libgpiod2_handle, gpiod_chip_close);
		LIBGPIOD_DLSYM(_libgpiod2_handle, gpiod_chip_get_line);
		LIBGPIOD_DLSYM(_libgpiod2_handle, gpiod_line_request_output);
		LIBGPIOD_DLSYM(_libgpiod2_handle, gpiod_line_set_value);
		LIBGPIOD_DLSYM(_libgpiod2_handle, gpiod_line_release);

		return 0;
	}

	log_debug("Could not load %s: %s", _libgpiod2, dlerror());

	_libgpiod3_handle = dlopen(_libgpiod3, RTLD_LAZY);

	if (_libgpiod3_handle != NULL) {
		log_debug("Successfully loaded %s", _libgpiod3);

		libgpiod_abi = 3;

		LIBGPIOD_DLSYM(_libgpiod3_handle, gpiod_is_gpiochip_device);
		LIBGPIOD_DLSYM(_libgpiod3_handle, gpiod_chip_open);
		LIBGPIOD_DLSYM(_libgpiod3_handle, gpiod_chip_close);
		LIBGPIOD_DLSYM(_libgpiod3_handle, gpiod_chip_get_info);
		LIBGPIOD_DLSYM(_libgpiod3_handle, gpiod_chip_get_line_offset_from_name);
		LIBGPIOD_DLSYM(_libgpiod3_handle, gpiod_chip_request_lines);
		LIBGPIOD_DLSYM(_libgpiod3_handle, gpiod_chip_info_free);
		LIBGPIOD_DLSYM(_libgpiod3_handle, gpiod_chip_info_get_name);
		LIBGPIOD_DLSYM(_libgpiod3_handle, gpiod_line_settings_new);
		LIBGPIOD_DLSYM(_libgpiod3_handle, gpiod_line_settings_free);
		LIBGPIOD_DLSYM(_libgpiod3_handle, gpiod_line_settings_set_direction);
		LIBGPIOD_DLSYM(_libgpiod3_handle, gpiod_line_settings_set_output_value);
		LIBGPIOD_DLSYM(_libgpiod3_handle, gpiod_line_config_new);
		LIBGPIOD_DLSYM(_libgpiod3_handle, gpiod_line_config_free);
		LIBGPIOD_DLSYM(_libgpiod3_handle, gpiod_line_config_add_line_settings);
		LIBGPIOD_DLSYM(_libgpiod3_handle, gpiod_line_request_release);
		LIBGPIOD_DLSYM(_libgpiod3_handle, gpiod_line_request_set_value);
		LIBGPIOD_DLSYM(_libgpiod3_handle, gpiod_request_config_new);
		LIBGPIOD_DLSYM(_libgpiod3_handle, gpiod_request_config_free);
		LIBGPIOD_DLSYM(_libgpiod3_handle, gpiod_request_config_set_consumer);

		return 0;
	}

	log_error("Could not load %s: %s", _libgpiod3, dlerror());

	return -1;
}

void libgpiod_dlclose(void) {
	if (_libgpiod2_handle != NULL) {
		log_debug("Unloading %s", _libgpiod2);

		dlclose(_libgpiod2_handle);
	}

	if (_libgpiod3_handle != NULL) {
		log_debug("Unloading %s", _libgpiod3);

		dlclose(_libgpiod3_handle);
	}
}
