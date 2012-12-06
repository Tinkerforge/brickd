/*
 * brickd
 * Copyright (C) 2012 Matthias Bolte <matthias@tinkerforge.com>
 *
 * main_linux.c: Brick Daemon starting point for Linux
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
#include "udev.h"
#include "usb.h"
#include "version.h"

#define LOG_CATEGORY LOG_CATEGORY_OTHER

static void print_usage(const char *binary) {
	printf("Usage: %s [--help|--version|--daemon]\n", binary);
}

int main(int argc, char **argv) {
	int exit_code = 2;
	int daemon = 0;

	if (argc > 2) {
		print_usage(argv[0]);

		return 1;
	}

	if (argc > 1) {
		if (strcmp(argv[1], "--help") == 0) {
			print_usage(argv[0]);

			return 0;
		} else if (strcmp(argv[1], "--version") == 0) {
			printf("%s\n", VERSION_STRING);

			return 0;
		} else if (strcmp(argv[1], "--daemon") == 0) {
			daemon = 1;
		} else {
			print_usage(argv[0]);

			return 1;
		}
	}

	log_init();

	// FIXME: read config
	log_set_level(LOG_CATEGORY_EVENT, LOG_LEVEL_INFO);
	log_set_level(LOG_CATEGORY_USB, LOG_LEVEL_DEBUG);
	log_set_level(LOG_CATEGORY_NETWORK, LOG_LEVEL_DEBUG);
	log_set_level(LOG_CATEGORY_HOTPLUG, LOG_LEVEL_DEBUG);
	log_set_level(LOG_CATEGORY_OTHER, LOG_LEVEL_DEBUG);

	if (daemon) {
		log_info("Brick Daemon %s started (daemonized)", VERSION_STRING);

		//if (daemonize() < 0) {
		//	goto error_event;
		//}
	} else {
		log_info("Brick Daemon %s started", VERSION_STRING);
	}

	if (event_init() < 0) {
		goto error_event;
	}

	if (usb_init() < 0) {
		goto error_usb;
	}

#ifdef BRICKD_WITH_LIBUDEV
	if (udev_init() < 0) {
		goto error_udev;
	}
#endif

	if (network_init() < 0) {
		goto error_network;
	}

	if (event_run() < 0) {
		goto error_run;
	}

	exit_code = 0;

error_run:
	network_exit();

error_network:
#ifdef BRICKD_WITH_LIBUDEV
	udev_exit();

error_udev:
#endif
	usb_exit();

error_usb:
	event_exit();

error_event:
	log_info("Brick Daemon %s stopped", VERSION_STRING);

	log_exit();

	return exit_code;
}
