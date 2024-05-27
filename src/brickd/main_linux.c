/*
 * brickd
 * Copyright (C) 2012-2021 Matthias Bolte <matthias@tinkerforge.com>
 * Copyright (C) 2014, 2018 Olaf Lüke <olaf@tinkerforge.com>
 * Copyright (C) 2014, 2016-2017 Ishraq Ibne Ashraf <ishraq@tinkerforge.com>
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
#include <sys/utsname.h>

#include <daemonlib/config.h>
#include <daemonlib/daemon.h>
#include <daemonlib/event.h>
#include <daemonlib/log.h>
#include <daemonlib/pid_file.h>
#ifdef BRICKD_WITH_RED_BRICK
	#include <daemonlib/gpio_red.h>
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
#ifdef BRICKD_WITH_BRICKLET
	#include "bricklet.h"
#endif
#include "usb.h"
#include "mesh.h"
#include "version.h"

static LogSource _log_source = LOG_SOURCE_INITIALIZER;

static char _config_filename_default[1024] = SYSCONFDIR"/brickd.conf";
static const char *_config_filename = _config_filename_default;
static char _pid_filename_default[1024] = RUNSTATEDIR"/brickd.pid";
static const char *_pid_filename = _pid_filename_default;
static char _log_filename_default[1024] = LOCALSTATEDIR"/log/brickd.log";
static const char *_log_filename = _log_filename_default;
static File _log_file;

#ifdef BRICKD_WITH_LIBUSB_HOTPLUG_MKNOD
extern bool usb_hotplug_mknod;
#endif

static int prepare_paths(bool daemon) {
#ifdef DAEMONLIB_WITH_STATIC
	(void)daemon;

	if (getuid() != 0) {
		// FIXME: glibc function getpwuid requires external glibc plugins, this
		//        breaks the static linking use case for brickd. therefore, just
		//        don't use getpwuid in a static brickd build.
		fprintf(stderr, "Cannot run static linked brickd as user, has to run as root\n");

		return -1;
	}
#else
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

	if (robust_snprintf(_config_filename_default, sizeof(_config_filename_default),
	                    "%s/.brickd/brickd.conf", home) < 0) {
		fprintf(stderr, "Could not format ~/.brickd/brickd.conf file name: %s (%d)\n",
		        get_errno_name(errno), errno);

		return -1;
	}

	if (robust_snprintf(_pid_filename_default, sizeof(_pid_filename_default),
	                    "%s/.brickd/brickd.pid", home) < 0) {
		fprintf(stderr, "Could not format ~/.brickd/brickd.pid file name: %s (%d)\n",
		        get_errno_name(errno), errno);

		return -1;
	}

	if (robust_snprintf(_log_filename_default, sizeof(_log_filename_default),
	                    "%s/.brickd/brickd.log", home) < 0) {
		fprintf(stderr, "Could not format ~/.brickd/brickd.log file name: %s (%d)\n",
		        get_errno_name(errno), errno);

		return -1;
	}

	// only create ~/.brickd directory if necessary
	if (strcmp(_config_filename, _config_filename_default) != 0 &&
	    strcmp(_pid_filename, _pid_filename_default) != 0 &&
	    (!daemon || strcmp(_log_filename, _log_filename_default) != 0)) {
		return 0;
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
#endif

	return 0;
}

static void print_usage(void) {
	printf("Usage:\n"
	       "  brickd [--help|--version|--check-config|--daemon [<log-file>]] [--debug [<filter>]]\n"
	       "         [--config-file <config-file>] [--pid-file <pid-file>]"
#ifdef BRICKD_WITH_LIBUSB_HOTPLUG_MKNOD
	       " [--libusb-hotplug-mknod]"
#endif
	       "\n"
	       "\n"
	       "Options:\n"
	       "  --help                       Show this help and exit\n"
	       "  --version                    Show version number and exit\n"
	       "  --check-config               Check config file for errors and exit\n"
	       "  --daemon [<log-file>]        Run as daemon and write log file to overridable location\n"
	       "  --debug [<filter>]           Set log level to debug and apply optional <filter>\n"
	       "  --config-file <config-file>  Read config from <config-file> instead of default location\n"
	       "  --pid-file <pid-file>        Write PID to <pid-file> instead of default location\n"
#ifdef BRICKD_WITH_LIBUSB_HOTPLUG_MKNOD
	       "  --libusb-hotplug-mknod       Enable mknod handling on libusb hotplug events\n"
#endif
	       );
}

static void handle_sighup(void) {
	IO *output;

	log_get_output(&output, NULL);

	if (output != &_log_file.base) {
		return;
	}

	log_set_output(&log_stderr_output, NULL);

	file_destroy(&_log_file);

	if (file_create(&_log_file, _log_filename,
	                O_CREAT | O_WRONLY | O_APPEND, 0644) < 0) {
		log_error("Could not reopen log file '%s': %s (%d)",
		          _log_filename, get_errno_name(errno), errno);

		return;
	}

	log_set_output(&_log_file.base, NULL);

	log_info("Reopened log file '%s'", _log_filename);
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
	const char *debug_filter = NULL;
	int pid_fd = -1;
	struct utsname uts;

#ifdef BRICKD_WITH_LIBUSB_HOTPLUG_MKNOD
	usb_hotplug_mknod = false;
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

			if (i + 1 < argc && strncmp(argv[i + 1], "--", 2) != 0) {
				_log_filename = argv[++i];

				if (_log_filename[0] != '/') {
					fprintf(stderr, "Option --daemon requires an absolute path\n\n");
					print_usage();

					return EXIT_FAILURE;
				}
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
#ifdef BRICKD_WITH_LIBUSB_HOTPLUG_MKNOD
		} else if (strcmp(argv[i], "--libusb-hotplug-mknod") == 0) {
			usb_hotplug_mknod = true;
#endif
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

	if (prepare_paths(daemon) < 0) {
		return EXIT_FAILURE;
	}

	if (check_config) {
		return config_check(_config_filename) < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
	}

	config_init(_config_filename, false);

	phase = 1;

	if (config_has_error()) {
		fprintf(stderr, "Error(s) occurred while reading config file '%s'\n",
		        _config_filename);

		goto cleanup;
	}

	if (daemon) {
		pid_fd = daemon_start(_log_filename, &_log_file, _pid_filename, true);
	} else {
		pid_fd = pid_file_acquire(_pid_filename, getpid());

		if (pid_fd == PID_FILE_ALREADY_ACQUIRED) {
			fprintf(stderr, "Already running according to '%s'\n", _pid_filename);
		}
	}

	phase = 2;

	if (pid_fd < 0) {
		goto cleanup;
	}

	if (!daemon) {
		log_init(); // daemon_start calls log_init
	}

	log_info("Brick Daemon %s started (pid: %u, daemonized: %d)",
	         VERSION_STRING, getpid(), daemon ? 1 : 0);

	if (uname(&uts) < 0) {
		log_warn("Could not get Linux system information: %s (%d)", get_errno_name(errno), errno);
	} else {
		log_info("Running on Linux system (sysname: %s, release: %s, version: %s, machine: %s)",
		         uts.sysname, uts.release, uts.version, uts.machine);
	}

	phase = 3;

	if (debug_filter != NULL) {
		log_enable_debug_override(debug_filter);
	}

	log_debug("Using config file: %s", _config_filename);

	if (daemon) {
		log_debug("Using log file: %s", _log_filename);
	}

	log_debug("Using PID file: %s", _pid_filename);

	if (config_has_warning()) {
		log_warn("Warning(s) in config file '%s', run with --check-config option for details",
		         _config_filename);
	}

#ifdef BRICKD_WITH_LIBUSB_DLOPEN
	if (libusb_init_dlopen() < 0) {
		goto cleanup;
	}

	phase = 4;
#endif

	if (event_init() < 0) {
		goto cleanup;
	}

	phase = 5;

	if (signal_init(handle_sighup, handle_sigusr1) < 0) {
		goto cleanup;
	}

	phase = 6;

	if (hardware_init() < 0) {
		goto cleanup;
	}

	phase = 7;

	if (usb_init() < 0) {
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

#ifdef BRICKD_WITH_RED_BRICK
	if (gpio_red_init() < 0) {
		goto cleanup;
	}

	phase = 11;

	if (redapid_init() < 0) {
		goto cleanup;
	}

	phase = 12;

	if (red_stack_init() < 0) {
		goto cleanup;
	}

	phase = 13;

	if (red_extension_init() < 0) {
		goto cleanup;
	}

	phase = 14;

	if (red_usb_gadget_init() < 0) {
		goto cleanup;
	}

	phase = 15;

	red_led_set_trigger(RED_LED_GREEN, config_get_option_value("led_trigger.green")->symbol);
	red_led_set_trigger(RED_LED_RED, config_get_option_value("led_trigger.red")->symbol);
#endif
#ifdef BRICKD_WITH_BRICKLET
	if (bricklet_init() < 0) {
		goto cleanup;
	}

	phase = 16;
#endif

	log_debug("Starting initial USB device scan");

	if (usb_rescan() < 0) {
		goto cleanup;
	}

	if (event_run(handle_event_cleanup) < 0) {
		goto cleanup;
	}

#ifdef BRICKD_WITH_RED_BRICK
	hardware_announce_disconnect();
	network_announce_red_brick_disconnect();
	red_usb_gadget_announce_red_brick_disconnect();
#endif

	exit_code = EXIT_SUCCESS;

cleanup:
	switch (phase) { // no breaks, all cases fall through intentionally
#ifdef BRICKD_WITH_BRICKLET
	case 16:
		bricklet_exit();
#endif
#ifdef BRICKD_WITH_RED_BRICK
		// fall through

	case 15:
		red_usb_gadget_exit();
		// fall through

	case 14:
		red_extension_exit();
		// fall through

	case 13:
		red_stack_exit();
		// fall through

	case 12:
		redapid_exit();
		// fall through

	case 11:
		//gpio_red_exit();
#endif
		// fall through

	case 10:
		mesh_exit();
		// fall through

	case 9:
		network_exit();
		// fall through

	case 8:
		usb_exit();
		// fall through

	case 7:
		hardware_exit();
		// fall through

	case 6:
		signal_exit();
		// fall through

	case 5:
		event_exit();

#ifdef BRICKD_WITH_LIBUSB_DLOPEN
		// fall through

	case 4:
		libusb_exit_dlopen();
#endif
		// fall through

	case 3:
		log_info("Brick Daemon %s stopped", VERSION_STRING);
		log_exit();
		// fall through

	case 2:
		if (pid_fd >= 0) {
			pid_file_release(_pid_filename, pid_fd);
		}

		// fall through

	case 1:
		config_exit();
		// fall through

	default:
		break;
	}

	return exit_code;
}
