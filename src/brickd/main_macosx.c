/*
 * brickd
 * Copyright (C) 2012-2013 Matthias Bolte <matthias@tinkerforge.com>
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

#include "config.h"
#include "event.h"
#include "log.h"
#include "network.h"
#include "pidfile.h"
#include "iokit.h"
#include "usb.h"
#include "version.h"

#define LOG_CATEGORY LOG_CATEGORY_OTHER

#define CONFIG_FILENAME SYSCONFDIR"/brickd.conf"
#define PID_FILENAME LOCALSTATEDIR"/run/brickd.pid"
#define LOG_FILENAME LOCALSTATEDIR"/log/brickd.log"

static void print_usage(void) {
	printf("Usage:\n"
	       "  brickd [--help|--version|--check-config|--daemon] [--debug]\n"
	       "\n"
	       "Options:\n"
	       "  --help          Show this help\n"
	       "  --version       Show version number\n"
	       "  --check-config  Check config file for errors\n"
	       "  --daemon        Run as daemon and write PID file\n"
	       "  --debug         Set all log levels to debug\n");
}

static int daemon_start(void) {
	int8_t status = 0;
	int pid_fd;
	FILE *log_file;
	int stdin_fd = -1;
	int stdout_fd = -1;

	// write pid
	pid_fd = pidfile_acquire(PID_FILENAME, getpid());

	if (pid_fd < 0) {
		if (pid_fd < -1) {
			fprintf(stderr, "Already running according to '%s'\n", PID_FILENAME);

			status = 2;
		}

		goto cleanup;
	}

	// open log file
	log_file = fopen(LOG_FILENAME, "a+");

	if (log_file == NULL) {
		fprintf(stderr, "Could not open logfile '%s': %s (%d)\n",
		        LOG_FILENAME, get_errno_name(errno), errno);

		goto cleanup;
	}

	log_set_file(log_file);

	// redirect standard file descriptors
	stdin_fd = open("/dev/null", O_RDONLY);

	if (stdin_fd < 0) {
		fprintf(stderr, "Could not open /dev/null to redirect stdin to: %s (%d)\n",
		        get_errno_name(errno), errno);

		goto cleanup;
	}

	stdout_fd = open("/dev/null", O_WRONLY);

	if (stdout_fd < 0) {
		fprintf(stderr, "Could not open /dev/null to redirect stdout/stderr to: %s (%d)\n",
		        get_errno_name(errno), errno);

		goto cleanup;
	}

	if (dup2(stdin_fd, STDIN_FILENO) != STDIN_FILENO) {
		fprintf(stderr, "Could not redirect stdin: %s (%d)\n",
		        get_errno_name(errno), errno);

		goto cleanup;
	}

	if (dup2(stdout_fd, STDOUT_FILENO) != STDOUT_FILENO) {
		fprintf(stderr, "Could not redirect stdout: %s (%d)\n",
		        get_errno_name(errno), errno);

		goto cleanup;
	}

	if (dup2(stdout_fd, STDERR_FILENO) != STDERR_FILENO) {
		fprintf(stderr, "Could not redirect stderr: %s (%d)\n",
		        get_errno_name(errno), errno);

		goto cleanup;
	}

	status = 1;

cleanup:
	if (stdin_fd > STDERR_FILENO) {
		close(stdin_fd);
	}

	if (stdout_fd > STDERR_FILENO) {
		close(stdout_fd);
	}

	return status == 1 ? pid_fd : -1;
}

int main(int argc, char **argv) {
	int exit_code = EXIT_FAILURE;
	int i;
	int help = 0;
	int version = 0;
	int check_config = 0;
	int daemon = 0;
	int debug = 0;
	int pid_fd = -1;

	for (i = 1; i < argc; ++i) {
		if (strcmp(argv[i], "--help") == 0) {
			help = 1;
		} else if (strcmp(argv[i], "--version") == 0) {
			version = 1;
		} else if (strcmp(argv[i], "--check-config") == 0) {
			check_config = 1;
		} else if (strcmp(argv[i], "--daemon") == 0) {
			daemon = 1;
		} else if (strcmp(argv[i], "--debug") == 0) {
			debug = 1;
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
		pid_fd = daemon_start();
	} else {
		pid_fd = pidfile_acquire(PID_FILENAME, getpid());
	}

	if (pid_fd < 0) {
		if (!daemon && pid_fd < -1) {
			fprintf(stderr, "Already running according to '%s'\n", PID_FILENAME);
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
		log_set_level(LOG_CATEGORY_EVENT, config_get_log_level(LOG_CATEGORY_EVENT));
		log_set_level(LOG_CATEGORY_USB, config_get_log_level(LOG_CATEGORY_USB));
		log_set_level(LOG_CATEGORY_NETWORK, config_get_log_level(LOG_CATEGORY_NETWORK));
		log_set_level(LOG_CATEGORY_HOTPLUG, config_get_log_level(LOG_CATEGORY_HOTPLUG));
		log_set_level(LOG_CATEGORY_OTHER, config_get_log_level(LOG_CATEGORY_OTHER));
	}

	if (daemon) {
		log_info("Brick Daemon %s started (daemonized)", VERSION_STRING);
	} else {
		log_info("Brick Daemon %s started", VERSION_STRING);
	}

	if (config_has_error()) {
		log_warn("Errors found in config file '%s', run with --check-config option for details",
		         CONFIG_FILENAME);
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

	if (pid_fd >= 0) {
		pidfile_release(PID_FILENAME, pid_fd);
	}

	config_exit();

	return exit_code;
}
