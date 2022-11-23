/*
 * brickd
 * Copyright (C) 2012-2019, 2021 Matthias Bolte <matthias@tinkerforge.com>
 * Copyright (C) 2016-2017 Ishraq Ibne Ashraf <ishraq@tinkerforge.com>
 *
 * main_macos.c: Brick Daemon starting point for macOS
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

static const char *_config_filename = SYSCONFDIR"/brickd.conf";
static const char *_pid_filename = LOCALSTATEDIR"/run/brickd.pid";
static const char *_log_filename = LOCALSTATEDIR"/log/brickd.log";
static File _log_file;

static void print_usage(void) {
	printf("Usage:\n"
	       "  brickd [--help|--version|--check-config|--daemon [<log-file>]|--launchd [<log-file>]]\n"
	       "         [--debug [<filter>]] [--config-file <config-file>] [--pid-file <pid-file>]\n"
	       "\n"
	       "Options:\n"
	       "  --help                       Show this help and exit\n"
	       "  --version                    Show version number and exit\n"
	       "  --check-config               Check config file for errors and exit\n"
	       "  --daemon [<log-file>]        Run as daemon and write log file to overridable location\n"
	       "  --launchd [<log-file>]       Run as launchd daemon and write log file to overridable location\n"
	       "  --debug [<filter>]           Set log level to debug and apply optional filter\n"
	       "  --config-file <config-file>  Read config from <config-file> instead of default location\n"
	       "  --pid-file <pid-file>        Write PID to <pid-file> instead of default location\n");
}

static void handle_sigusr1(void) {
#ifdef BRICKD_WITH_USB_REOPEN_ON_SIGUSR1
	log_info("Reopening all USB devices, triggered by SIGUSR1");

	usb_reopen(NULL);
#else
	log_info("Starting USB device scan, triggered by SIGUSR1");

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

			if (i + 1 < argc && strncmp(argv[i + 1], "--", 2) != 0) {
				_log_filename = argv[++i];

				if (_log_filename[0] != '/') {
					fprintf(stderr, "Option --daemon requires an absolute path\n\n");
					print_usage();

					return EXIT_FAILURE;
				}
			}
		} else if (strcmp(argv[i], "--launchd") == 0) {
			launchd = true;

			if (i + 1 < argc && strncmp(argv[i + 1], "--", 2) != 0) {
				_log_filename = argv[++i];
			}
		} else if (strcmp(argv[i], "--debug") == 0) {
			if (i + 1 < argc && strncmp(argv[i + 1], "--", 2) != 0) {
				debug_filter = argv[++i];
			} else {
				debug_filter = "";
			}
		} else if (strcmp(argv[i], "--config-file") == 0) {
			if (i + 1 < argc && strncmp(argv[i + 1], "--", 2) != 0) {
				_config_filename = argv[++i];
			} else {
				fprintf(stderr, "Option --config-file requires <config-file>\n\n");
				print_usage();

				return EXIT_FAILURE;
			}
		} else if (strcmp(argv[i], "--pid-file") == 0) {
			if (i + 1 < argc && strncmp(argv[i + 1], "--", 2) != 0) {
				_pid_filename = argv[++i];
			} else {
				fprintf(stderr, "Option --pid-file requires <pid-file>\n\n");
				print_usage();

				return EXIT_FAILURE;
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
		return config_check(_config_filename) < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
	}

	if (daemon && launchd) {
		fprintf(stderr, "Options --daemon and --launchd cannot be used at the same time\n\n");
		print_usage();

		return EXIT_FAILURE;
	}

	config_init(_config_filename, false);

	phase = 1;

	if (config_has_error()) {
		fprintf(stderr, "Error(s) occurred while reading config file '%s'\n",
		        _config_filename);

		goto cleanup;
	}

	if (daemon || launchd) {
		pid_fd = daemon_start(_log_filename, &_log_file, _pid_filename, !launchd);
	} else {
		pid_fd = pid_file_acquire(_pid_filename, getpid());

		if (pid_fd == PID_FILE_ALREADY_ACQUIRED) {
			fprintf(stderr, "Already running according to '%s'\n", _pid_filename);
		}
	}

	log_init();

	phase = 2;

	if (pid_fd < 0) {
		goto cleanup;
	}

	log_info("Brick Daemon %s started (pid: %u, daemonized: %d)",
	         VERSION_STRING, getpid(), daemon || launchd ? 1 : 0);

	phase = 3;

	if (debug_filter != NULL) {
		log_enable_debug_override(debug_filter);
	}

	log_debug("Using config file: %s", _config_filename);

	if (daemon || launchd) {
		log_debug("Using log file: %s", _log_filename);
	}

	log_debug("Using PID file: %s", _pid_filename);

	if (config_has_warning()) {
		log_warn("Warning(s) in config file '%s', run with --check-config option for details",
		         _config_filename);
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
			pid_file_release(_pid_filename, pid_fd);
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
