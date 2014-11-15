/*
 * brickd
 * Copyright (C) 2012-2014 Matthias Bolte <matthias@tinkerforge.com>
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

#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <pwd.h>
#include <unistd.h>
#include <fcntl.h>

#include <daemonlib/config.h>
#include <daemonlib/daemon.h>
#include <daemonlib/event.h>
#include <daemonlib/log.h>
#include <daemonlib/pid_file.h>
#ifdef BRICKD_WITH_RED_BRICK
	#include <daemonlib/red_gpio.h>
	#include <daemonlib/red_led.h>
#endif
#include <daemonlib/signal.h>
#include <daemonlib/utils.h>

#include "hardware.h"
#include "network.h"
#ifdef BRICKD_WITH_RED_BRICK
	#include "redapid.h"
	#include "red_stack.h"
	#include "red_usb_gadget.h"
    #include "red_extension.h"
#endif
#ifdef BRICKD_WITH_LIBUDEV
	#include "udev.h"
#endif
#include "usb.h"
#include "version.h"

static char _config_filename[1024] = SYSCONFDIR "/brickd.conf";
static char _pid_filename[1024] = LOCALSTATEDIR "/run/brickd.pid";
static char _log_filename[1024] = LOCALSTATEDIR "/log/brickd.log";

static int prepare_paths(void) {
	char *home;
	struct passwd *pw;
	char brickd_dirname[1024];
	struct stat st;

	if (getuid() == 0) {
		return 0;
	}

	home = getenv("HOME");

	if (home == NULL || *home == '\0') {
		pw = getpwuid(getuid());

		if (pw == NULL) {
			fprintf(stderr, "Could not determine home directory: %s (%d)\n",
			        get_errno_name(errno), errno);

			return -1;
		}

		home = pw->pw_dir;
	}

	if (robust_snprintf(brickd_dirname, sizeof(brickd_dirname),
	                    "%s/.brickd", home) < 0) {
		fprintf(stderr, "Could not format ~/.brickd directory name: %s (%d)\n",
		        get_errno_name(errno), errno);

		return -1;
	}

	if (robust_snprintf(_config_filename, sizeof(_config_filename),
	                    "%s/.brickd/brickd.conf", home) < 0) {
		fprintf(stderr, "Could not format ~/.brickd/brickd.conf file name: %s (%d)\n",
		        get_errno_name(errno), errno);

		return -1;
	}

	if (robust_snprintf(_pid_filename, sizeof(_pid_filename),
	                    "%s/.brickd/brickd.pid", home) < 0) {
		fprintf(stderr, "Could not format ~/.brickd/brickd.pid file name: %s (%d)\n",
		        get_errno_name(errno), errno);

		return -1;
	}

	if (robust_snprintf(_log_filename, sizeof(_log_filename),
	                    "%s/.brickd/brickd.log", home) < 0) {
		fprintf(stderr, "Could not format ~/.brickd/brickd.log file name: %s (%d)\n",
		        get_errno_name(errno), errno);

		return -1;
	}

	if (mkdir(brickd_dirname, 0755) < 0) {
		if (errno != EEXIST) {
			fprintf(stderr, "Could not create directory '%s': %s (%d)\n",
			        brickd_dirname, get_errno_name(errno), errno);

			return -1;
		}

		if (stat(brickd_dirname, &st) < 0) {
			fprintf(stderr, "Could not get information for '%s': %s (%d)\n",
			        brickd_dirname, get_errno_name(errno), errno);

			return -1;
		}

		if (!S_ISDIR(st.st_mode)) {
			fprintf(stderr, "Expecting '%s' to be a directory\n", brickd_dirname);

			return -1;
		}
	}

	return 0;
}

static void print_usage(void) {
	printf("Usage:\n"
	       "  brickd [--help|--version|--check-config|--daemon] [--debug] [--libusb-debug]\n"
	       "\n"
	       "Options:\n"
	       "  --help          Show this help\n"
	       "  --version       Show version number\n"
	       "  --check-config  Check config file for errors\n"
	       "  --daemon        Run as daemon and write PID and log file\n"
	       "  --debug         Set all log levels to debug\n"
	       "  --libusb-debug  Set libusb log level to debug\n");
}

static void handle_sighup(void) {
	FILE *log_file = log_get_file();

	if (log_file != NULL) {
		if (fileno(log_file) == STDOUT_FILENO || fileno(log_file) == STDERR_FILENO) {
			return; // don't close stdout or stderr
		}

		fclose(log_file);
	}

	log_file = fopen(_log_filename, "a+");

	if (log_file == NULL) {
		log_set_file(stderr);

		log_error("Could not reopen log file '%s': %s (%d)",
		          _log_filename, get_errno_name(errno), errno);

		return;
	}

	log_set_file(log_file);

	log_info("Reopened log file '%s'", _log_filename);
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
#ifdef BRICKD_WITH_LIBUDEV
	bool initialized_udev = false;
#endif

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

	if (prepare_paths() < 0) {
		return EXIT_FAILURE;
	}

	if (check_config) {
		return config_check(_config_filename) < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
	}

	config_init(_config_filename);

	log_init();
	log_set_debug_override(debug);

	if (daemon) {
		pid_fd = daemon_start(_log_filename, _pid_filename, true);
	} else {
		pid_fd = pid_file_acquire(_pid_filename, getpid());

		if (pid_fd == PID_FILE_ALREADY_ACQUIRED) {
			fprintf(stderr, "Already running according to '%s'\n", _pid_filename);
		}
	}

	if (pid_fd < 0) {
		goto error_log;
	}

	if (config_has_error()) {
		log_error("Error(s) occurred while reading config file '%s'",
		          _config_filename);

		goto error_config;
	}

	if (daemon) {
		log_info("Brick Daemon %s started (daemonized)", VERSION_STRING);
	} else {
		log_info("Brick Daemon %s started", VERSION_STRING);
	}

	if (config_has_warning()) {
		log_warn("Warning(s) in config file '%s', run with --check-config option for details",
		         _config_filename);
	}

	if (event_init() < 0) {
		goto error_event;
	}

	if (signal_init(handle_sighup, handle_sigusr1) < 0) {
		goto error_signal;
	}

	if (hardware_init() < 0) {
		goto error_hardware;
	}

	if (usb_init(libusb_debug) < 0) {
		goto error_usb;
	}

#ifdef BRICKD_WITH_LIBUDEV
	if (!usb_has_hotplug()) {
		if (udev_init() < 0) {
			goto error_udev;
		}

		initialized_udev = true;
	}
#endif

	if (network_init() < 0) {
		goto error_network;
	}

#ifdef BRICKD_WITH_RED_BRICK
	if (gpio_init() < 0) {
		goto error_gpio;
	}

	if (redapid_init() < 0) {
		goto error_redapid;
	}

	if (red_stack_init() < 0) {
		goto error_red_stack;
	}

	if (red_extension_init() < 0) {
		goto error_red_extension;
	}

	if (red_usb_gadget_init() < 0) {
		goto error_red_usb_gadget;
	}

	red_led_set_trigger(RED_LED_GREEN, config_get_option_value("led_trigger.green")->symbol);
	red_led_set_trigger(RED_LED_RED, config_get_option_value("led_trigger.red")->symbol);
#endif

	if (event_run(network_cleanup_clients_and_zombies) < 0) {
		goto error_run;
	}

#ifdef BRICKD_WITH_RED_BRICK
	hardware_announce_disconnect();
	network_announce_red_brick_disconnect();
	red_usb_gadget_announce_red_brick_disconnect();
#endif

	exit_code = EXIT_SUCCESS;

error_run:
#ifdef BRICKD_WITH_RED_BRICK
	red_usb_gadget_exit();

error_red_usb_gadget:
	red_extension_exit();

error_red_extension:
	red_stack_exit();

error_red_stack:
	redapid_exit();

error_redapid:
	//gpio_exit();

error_gpio:
#endif
	network_exit();

error_network:
#ifdef BRICKD_WITH_LIBUDEV
	if (initialized_udev) {
		udev_exit();
	}

error_udev:
#endif
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
		pid_file_release(_pid_filename, pid_fd);
	}

	config_exit();

	return exit_code;
}
