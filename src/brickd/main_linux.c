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
#include <daemonlib/red_gpio.h>
#include <daemonlib/signal.h>
#include <daemonlib/utils.h>

#include "hardware.h"
#include "network.h"
#ifdef BRICKD_WITH_RED_BRICK
	#include "redapid.h"
	#include "red_stack.h"
	#include "red_usb_gadget.h"
    #include "rs485_extension.h"
#endif
#ifdef BRICKD_WITH_LIBUDEV
	#include "udev.h"
#endif
#include "usb.h"
#include "version.h"

#define LOG_CATEGORY LOG_CATEGORY_OTHER

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

	if (strlen(home) + strlen("/.brickd/brickd.conf") >= sizeof(brickd_dirname)) {
		fprintf(stderr, "Home directory name is too long\n");

		return -1;
	}

	snprintf(brickd_dirname, sizeof(brickd_dirname), "%s/.brickd", home);
	snprintf(_config_filename, sizeof(_config_filename), "%s/.brickd/brickd.conf", home);
	snprintf(_pid_filename, sizeof(_pid_filename), "%s/.brickd/brickd.pid", home);
	snprintf(_log_filename, sizeof(_log_filename), "%s/.brickd/brickd.log", home);

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

	log_set_debug_override(debug);

	log_set_level(LOG_CATEGORY_EVENT, config_get_option_value("log_level.event")->log_level);
	log_set_level(LOG_CATEGORY_USB, config_get_option_value("log_level.usb")->log_level);
	log_set_level(LOG_CATEGORY_NETWORK, config_get_option_value("log_level.network")->log_level);
	log_set_level(LOG_CATEGORY_HOTPLUG, config_get_option_value("log_level.hotplug")->log_level);
	log_set_level(LOG_CATEGORY_HARDWARE, config_get_option_value("log_level.hardware")->log_level);
	log_set_level(LOG_CATEGORY_WEBSOCKET, config_get_option_value("log_level.websocket")->log_level);
#ifdef BRICKD_WITH_RED_BRICK
	log_set_level(LOG_CATEGORY_RED_BRICK, config_get_option_value("log_level.red_brick")->log_level);
	log_set_level(LOG_CATEGORY_SPI, config_get_option_value("log_level.spi")->log_level);
	log_set_level(LOG_CATEGORY_RS485, config_get_option_value("log_level.rs485")->log_level);
#endif
	log_set_level(LOG_CATEGORY_OTHER, config_get_option_value("log_level.other")->log_level);

	if (config_has_error()) {
		log_error("Error(s) while reading config file '%s'",
		          _config_filename);

		goto error_config;
	}

	if (daemon) {
		log_info("Brick Daemon %s started (daemonized)", VERSION_STRING);
	} else {
		log_info("Brick Daemon %s started", VERSION_STRING);
	}

	if (config_has_warning()) {
		log_error("Warning(s) in config file '%s', run with --check-config option for details",
		          _config_filename);
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

	if (red_usb_gadget_init() < 0) {
		goto error_red_usb_gadget;
	}

	if (redapid_init() < 0) {
		goto error_redapid;
	}

	if (red_stack_init() < 0) {
		goto error_red_stack;
	}

	if (rs485_extension_init() < 0) {
		goto error_rs485_extension;
	}
#endif

	if (event_run(network_cleanup_clients_and_zombies) < 0) {
		goto error_run;
	}

	exit_code = EXIT_SUCCESS;

error_run:
#ifdef BRICKD_WITH_RED_BRICK
	rs485_extension_exit();

error_rs485_extension:
	red_stack_exit();
    
error_red_stack:
	redapid_exit();

error_redapid:
	red_usb_gadget_exit();

error_red_usb_gadget:
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
