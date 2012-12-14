/*
 * brickd
 * Copyright (C) 2012 Matthias Bolte <matthias@tinkerforge.com>
 *
 * main_macosx.c: Brick Daemon starting point for Mac OS X
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

#include <string.h>

#include "event.h"
#include "log.h"
#include "network.h"
#include "iokit.h"
#include "usb.h"
#include "version.h"

#define LOG_CATEGORY LOG_CATEGORY_OTHER

static void print_usage(const char *binary) {
	printf("Usage: %s [--help|--version]\n", binary);
}

int main(int argc, char **argv) {
	int exit_code = EXIT_FAILURE;

	if (argc > 2) {
		print_usage(argv[0]);

		return EXIT_FAILURE;
	}

	if (argc > 1) {
		if (strcmp(argv[1], "--help") == 0) {
			print_usage(argv[0]);

			return EXIT_SUCCESS;
		} else if (strcmp(argv[1], "--version") == 0) {
			printf("%s\n", VERSION_STRING);

			return EXIT_SUCCESS;
		} else {
			print_usage(argv[0]);

			return EXIT_FAILURE;
		}
	}

	log_init();

	// FIXME: read config
	log_set_level(LOG_CATEGORY_EVENT, LOG_LEVEL_DEBUG);
	log_set_level(LOG_CATEGORY_USB, LOG_LEVEL_DEBUG);
	log_set_level(LOG_CATEGORY_NETWORK, LOG_LEVEL_DEBUG);
	log_set_level(LOG_CATEGORY_HOTPLUG, LOG_LEVEL_DEBUG);
	log_set_level(LOG_CATEGORY_OTHER, LOG_LEVEL_DEBUG);

	log_info("Brick Daemon %s started", VERSION_STRING);

	if (event_init() < 0) {
		goto error_event;
	}

	if (usb_init() < 0) {
		goto error_usb;
	}

	if (iokit_init() < 0) {
		goto error_iokit;
	}

	if (network_init() < 0) {
		goto error_network;
	}

	if (event_run() < 0) {
		goto error_run;
	}

	exit_code = EXIT_SUCCESS;

error_run:
	network_exit();

error_network:
	iokit_exit();

error_iokit:
	usb_exit();

error_usb:
	event_exit();

error_event:
	log_info("Brick Daemon %s stopped", VERSION_STRING);

	log_exit();

	return exit_code;
}
