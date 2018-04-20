/*
 * brickd
 * Copyright (C) 2012-2014, 2016-2017 Matthias Bolte <matthias@tinkerforge.com>
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
#include "mesh.h"
#include "version.h"

static LogSource _log_source = LOG_SOURCE_INITIALIZER;

#define CONFIG_FILENAME (SYSCONFDIR "/brickd.conf")
#define PID_FILENAME (LOCALSTATEDIR "/run/brickd.pid")
#define LOG_FILENAME (LOCALSTATEDIR "/log/brickd.log")
static File _log_file;

static void print_usage(void) {
	printf("Usage:\n"
	       "  brickd [--help|--version|--check-config|--daemon|--launchd]\n"
	       "         [--debug [<filter>]]\n"
	       "\n"
	       "Options:\n"
	       "  --help              Show this help\n"
	       "  --version           Show version number\n"
	       "  --check-config      Check config file for errors\n"
	       "  --daemon            Run as daemon and write PID and log file\n"
	       "  --launchd           Run as launchd daemon and write PID and log file\n"
	       "  --debug [<filter>]  Set log level to debug and apply optional filter\n");
}

static void handle_sigusr1(void) {
#ifdef BRICKD_WITH_USB_REOPEN_ON_SIGUSR1
	log_debug("Reopening all USB devices, triggered by SIGUSR1");

	usb_reopen(NULL);
#else
	log_debug("Starting USB device scan, triggered by SIGUSR1");

	usb_rescan();
#endif
}

static void handle_event_cleanup(void) {
	network_cleanup_clients_and_zombies();
	mesh_cleanup_stacks();
}

int main(int argc, char **argv) {
	int phase = 0;
	int exit_code = EXIT_FAILURE;
	int i;
	bool help = false;
	bool version = false;
	bool check_config = false;
	bool daemon = false;
	bool launchd = false;
	const char *debug_filter = NULL;
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
		} else if (strcmp(argv[i], "--launchd") == 0) {
			launchd = true;
		} else if (strcmp(argv[i], "--debug") == 0) {
			if (i + 1 < argc && strncmp(argv[i + 1], "--", 2) != 0) {
				debug_filter = argv[++i];
			} else {
				debug_filter = "";
			}
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

	phase = 1;

	if (config_has_error()) {
		fprintf(stderr, "Error(s) occurred while reading config file '%s'\n",
		        CONFIG_FILENAME);

		goto cleanup;
	}

	log_init();

	if (daemon || launchd) {
		pid_fd = daemon_start(LOG_FILENAME, &_log_file, PID_FILENAME, !launchd);
	} else {
		pid_fd = pid_file_acquire(PID_FILENAME, getpid());

		if (pid_fd == PID_FILE_ALREADY_ACQUIRED) {
			fprintf(stderr, "Already running according to '%s'\n", PID_FILENAME);
		}
	}

	phase = 2;

	if (pid_fd < 0) {
		goto cleanup;
	}

	log_info("Brick Daemon %s started (pid: %u, daemonized: %d)",
	         VERSION_STRING, getpid(), daemon ? 1 : 0);

	phase = 3;

	if (debug_filter != NULL) {
		log_enable_debug_override(debug_filter);
	}

	if (config_has_warning()) {
		log_warn("Warning(s) in config file '%s', run with --check-config option for details",
		         CONFIG_FILENAME);
	}

	if (event_init() < 0) {
		goto cleanup;
	}

	phase = 4;

	if (signal_init(NULL, handle_sigusr1) < 0) {
		goto cleanup;
	}

	phase = 5;

	if (hardware_init() < 0) {
		goto cleanup;
	}

	phase = 6;

	if (usb_init() < 0) {
		goto cleanup;
	}

	phase = 7;

	if (iokit_init() < 0) {
		goto cleanup;
	}

	phase = 8;

	if (network_init() < 0) {
		goto cleanup;
	}

	phase = 9;

	if (mesh_init() < 0) {
		goto cleanup;
	}

	phase = 10;

	if (event_run(handle_event_cleanup) < 0) {
		goto cleanup;
	}

	exit_code = EXIT_SUCCESS;

cleanup:
	switch (phase) { // no breaks, all cases fall through intentionally
	case 10:
		mesh_exit();
		// fall through

	case 9:
		network_exit();
		// fall through

	case 8:
		iokit_exit();
		// fall through

	case 7:
		usb_exit();
		// fall through

	case 6:
		hardware_exit();
		// fall through

	case 5:
		signal_exit();
		// fall through

	case 4:
		event_exit();
		// fall through

	case 3:
		log_info("Brick Daemon %s stopped", VERSION_STRING);
		// fall through

	case 2:
		if (pid_fd >= 0) {
			pid_file_release(PID_FILENAME, pid_fd);
		}

		log_exit();
		// fall through

	case 1:
		config_exit();
		// fall through

	default:
		break;
	}

	return exit_code;
}
