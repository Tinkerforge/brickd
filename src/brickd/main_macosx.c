/*
 * brickd
 * Copyright (C) 2012-2014 Matthias Bolte <matthias@tinkerforge.com>
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

#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>

#include <daemonlib/config.h>
#include <daemonlib/daemon.h>
#include <daemonlib/event.h>
#include <daemonlib/log.h>
#include <daemonlib/pid_file.h>
#include <daemonlib/signal.h>
#include <daemonlib/utils.h>

#include "hardware.h"
#include "iokit.h"
#include "network.h"
#include "usb.h"
#include "version.h"

#define LOG_CATEGORY LOG_CATEGORY_OTHER

#define CONFIG_FILENAME (SYSCONFDIR "/brickd.conf")
#define PID_FILENAME (LOCALSTATEDIR "/run/brickd.pid")
#define LOG_FILENAME (LOCALSTATEDIR "/log/brickd.log")

static void print_usage(void) {
	printf("Usage:\n"
	       "  brickd [--help|--version|--check-config|--daemon] [--debug] [--libusb-debug]\n"
	       "\n"
	       "Options:\n"
	       "  --help          Show this help\n"
	       "  --version       Show version number\n"
	       "  --check-config  Check config file for errors\n"
	       "  --daemon        Run as daemon and write PID file\n"
	       "  --debug         Set all log levels to debug\n"
	       "  --libusb-debug  Set libusb log level to debug\n");
}

static void handle_sigusr1(void) {
#ifdef BRICKD_WITH_USB_REOPEN_ON_SIGUSR1
	usb_reopen();
#else
	usb_rescan();
#endif
}

int main(int argc, char **argv) {
	int exit_code = EXIT_FAILURE;
	int i;
	bool help = false;
	bool version = false;
	bool check_config = false;
	bool daemon = false;
	bool debug = false;
	bool libusb_debug = false;
	int pid_fd = -1;

	for (i = 1; i < argc; ++i) {
		if (strcmp(argv[i], "--help") == 0) {
			help = true;
		} else if (strcmp(argv[i], "--version") == 0) {
			version = true;
		} else if (strcmp(argv[i], "--check-config") == 0) {
			check_config = true;
		} else if (strcmp(argv[i], "--daemon") == 0) {
			daemon = true;
		} else if (strcmp(argv[i], "--debug") == 0) {
			debug = true;
		} else if (strcmp(argv[i], "--libusb-debug") == 0) {
			libusb_debug = true;
		} else {
			fprintf(stderr, "Unknown option '%s'\n\n", argv[i]);
			print_usage();

			return EXIT_FAILURE;
		}
	}

	if (help) {
		print_usage();

		return EXIT_SUCCESS;
	}

	if (version) {
		printf("%s\n", VERSION_STRING);

		return EXIT_SUCCESS;
	}

	if (check_config) {
		return config_check(CONFIG_FILENAME) < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
	}

	config_init(CONFIG_FILENAME);

	log_init();

	if (daemon) {
		pid_fd = daemon_start(LOG_FILENAME, PID_FILENAME, false);
	} else {
		pid_fd = pid_file_acquire(PID_FILENAME, getpid());

		if (pid_fd == PID_FILE_ALREADY_ACQUIRED) {
			fprintf(stderr, "Already running according to '%s'\n", PID_FILENAME);
		}
	}

	if (pid_fd < 0) {
		goto error_log;
	}

	log_set_debug_override(debug);

	log_set_level(LOG_CATEGORY_EVENT, config_get_option("log_level.event")->value.log_level);
	log_set_level(LOG_CATEGORY_USB, config_get_option("log_level.usb")->value.log_level);
	log_set_level(LOG_CATEGORY_NETWORK, config_get_option("log_level.network")->value.log_level);
	log_set_level(LOG_CATEGORY_HOTPLUG, config_get_option("log_level.hotplug")->value.log_level);
	log_set_level(LOG_CATEGORY_HARDWARE, config_get_option("log_level.hardware")->value.log_level);
	log_set_level(LOG_CATEGORY_WEBSOCKET, config_get_option("log_level.websocket")->value.log_level);
	log_set_level(LOG_CATEGORY_OTHER, config_get_option("log_level.other")->value.log_level);

	if (config_has_error()) {
		log_error("Error(s) in config file '%s', run with --check-config option for details",
		          CONFIG_FILENAME);

		goto error_config;
	}

	if (daemon) {
		log_info("Brick Daemon %s started (daemonized)", VERSION_STRING);
	} else {
		log_info("Brick Daemon %s started", VERSION_STRING);
	}

	if (config_has_warning()) {
		log_error("Warning(s) in config file '%s', run with --check-config option for details",
		          CONFIG_FILENAME);
	}

	if (event_init() < 0) {
		goto error_event;
	}

	if (signal_init(handle_sigusr1) < 0) {
		goto error_signal;
	}

	if (hardware_init() < 0) {
		goto error_hardware;
	}

	if (usb_init(libusb_debug) < 0) {
		goto error_usb;
	}

	if (iokit_init() < 0) {
		goto error_iokit;
	}

	if (network_init() < 0) {
		goto error_network;
	}

	if (event_run(network_cleanup_clients_and_zombies) < 0) {
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
	hardware_exit();

error_hardware:
	signal_exit();

error_signal:
	event_exit();

error_event:
	log_info("Brick Daemon %s stopped", VERSION_STRING);

error_config:
error_log:
	log_exit();

	if (pid_fd >= 0) {
		pid_file_release(PID_FILENAME, pid_fd);
	}

	config_exit();

	return exit_code;
}
