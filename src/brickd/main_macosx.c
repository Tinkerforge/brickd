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

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>

#include "event.h"
#include "log.h"
#include "network.h"
#include "pidfile.h"
#include "iokit.h"
#include "usb.h"
#include "version.h"

#define LOG_CATEGORY LOG_CATEGORY_OTHER

#define PIDFILE "/var/run/brickd.pid"
#define LOGFILE "/var/log/brickd.log"

static void print_usage(const char *binary) {
	printf("Usage: %s [--help|--version|--daemon] [--debug]\n", binary);
}

static int daemonize(void) {
	int8_t status = 0;
	FILE *logfile;
	int pidfile = -1;
	int stdin;
	int stdout;

	// write pid
	pidfile = pidfile_acquire(PIDFILE, getpid());

	if (pidfile < 0) {
		if (pidfile < -1) {
			fprintf(stderr, "Already running according to %s\n", PIDFILE);

			status = 2;
		}

		goto cleanup;
	}

	// open log file
	logfile = fopen(LOGFILE, "a+");

	if (logfile == NULL) {
		fprintf(stderr, "Could not open logfile '%s': %s (%d)\n",
		        LOGFILE, get_errno_name(errno), errno);

		goto cleanup;
	}

	log_set_file(logfile);

	// redirect standard file descriptors
	// FIXME: report errors
	stdin = open("/dev/null", O_RDONLY);
	stdout = open("/dev/null", O_RDONLY);

	if (stdin < 0)
		goto cleanup;
	if (stdout < 0)
		goto cleanup;
	if (dup2(stdin, STDIN_FILENO) != STDIN_FILENO)
		goto cleanup;
	if (dup2(stdout, STDOUT_FILENO) != STDOUT_FILENO)
		goto cleanup;
	if (dup2(stdout, STDERR_FILENO) != STDERR_FILENO)
		goto cleanup;
	if (stdin > STDERR_FILENO && close(stdin) < 0)
		goto cleanup;
	if (stdout > STDERR_FILENO && close(stdout) < 0)
		goto cleanup;

	status = 1;

cleanup:
	return status == 1 ? pidfile : -1;
}

int main(int argc, char **argv) {
	int exit_code = EXIT_FAILURE;
	int i;
	int help = 0;
	int version = 0;
	int daemon = 0;
	int debug = 0;
	int pidfile = -1;

	for (i = 1; i < argc; ++i) {
		if (strcmp(argv[i], "--help") == 0) {
			help = 1;
		} else if (strcmp(argv[i], "--version") == 0) {
			version = 1;
		} else if (strcmp(argv[i], "--daemon") == 0) {
			daemon = 1;
		} else if (strcmp(argv[i], "--debug") == 0) {
			debug = 1;
		} else {
			fprintf(stderr, "Unknown option '%s'\n", argv[i]);
			print_usage(argv[0]);

			return EXIT_FAILURE;
		}
	}

	if (help) {
		print_usage(argv[0]);

		return EXIT_SUCCESS;
	}

	if (version) {
		printf("%s\n", VERSION_STRING);

		return EXIT_SUCCESS;
	}

	log_init();

	if (daemon) {
		pidfile = daemonize();
	} else {
		pidfile = pidfile_acquire(PIDFILE, getpid());
	}

	if (pidfile < 0) {
		if (!daemon && pidfile < -1) {
			fprintf(stderr, "Already running according to %s\n", PIDFILE);
		}

		goto error_log;
	}

	if (debug) {
		log_set_level(LOG_CATEGORY_EVENT, LOG_LEVEL_DEBUG);
		log_set_level(LOG_CATEGORY_USB, LOG_LEVEL_DEBUG);
		log_set_level(LOG_CATEGORY_NETWORK, LOG_LEVEL_DEBUG);
		log_set_level(LOG_CATEGORY_HOTPLUG, LOG_LEVEL_DEBUG);
		log_set_level(LOG_CATEGORY_OTHER, LOG_LEVEL_DEBUG);
	} else {
		// FIXME: read config
		log_set_level(LOG_CATEGORY_EVENT, LOG_LEVEL_INFO);
		log_set_level(LOG_CATEGORY_USB, LOG_LEVEL_INFO);
		log_set_level(LOG_CATEGORY_NETWORK, LOG_LEVEL_INFO);
		log_set_level(LOG_CATEGORY_HOTPLUG, LOG_LEVEL_INFO);
		log_set_level(LOG_CATEGORY_OTHER, LOG_LEVEL_INFO);
	}

	if (daemon) {
		log_info("Brick Daemon %s started (daemonized)", VERSION_STRING);
	} else {
		log_info("Brick Daemon %s started", VERSION_STRING);
	}

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

error_log:
	log_exit();

	if (pidfile >= 0) {
		pidfile_release(PIDFILE, pidfile);
	}

	return exit_code;
}
