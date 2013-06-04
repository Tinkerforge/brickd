/*
 * brickd
 * Copyright (C) 2012-2013 Matthias Bolte <matthias@tinkerforge.com>
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
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <pwd.h>
#include <unistd.h>
#include <fcntl.h>

#include "config.h"
#include "event.h"
#include "log.h"
#include "network.h"
#include "pidfile.h"
#include "udev.h"
#include "usb.h"
#include "version.h"

#define LOG_CATEGORY LOG_CATEGORY_OTHER

static char _config_filename[1024] = SYSCONFDIR"/brickd.conf";
static char _pid_filename[1024] = LOCALSTATEDIR"/run/brickd.pid";
static char _log_filename[1024] = LOCALSTATEDIR"/log/brickd.log";

static int prepare_paths(void) {
	char *home;
	struct passwd *pwd;
	char brickd_dirname[1024];
	struct stat st;

	if (getuid() == 0) {
		return 0;
	}

	home = getenv("HOME");

	if (home == NULL || *home == '\0') {
		pwd = getpwuid(getuid());

		if (pwd == NULL) {
			fprintf(stderr, "Could not determine home directory: %s (%d)\n",
			        get_errno_name(errno), errno);

			return -1;
		}

		home = pwd->pw_dir;
	}

	if (strlen(home) + strlen("/.brickd/brickd.conf") >= sizeof(brickd_dirname)) {
		fprintf(stderr, "Home directory name is too long\n");

		return -1;
	}

	snprintf(brickd_dirname, sizeof(brickd_dirname), "%s/.brickd", home);
	snprintf(_config_filename, sizeof(_config_filename), "%s/.brickd/brickd.conf", home);
	snprintf(_pid_filename, sizeof(_pid_filename), "%s/.brickd/brickd.pid", home);
	snprintf(_log_filename, sizeof(_log_filename), "%s/.brickd/brickd.log", home);

	if (stat(brickd_dirname, &st) < 0) {
		if (errno != ENOENT) {
			fprintf(stderr, "Could not stat '%s': %s (%d)\n",
			        brickd_dirname, get_errno_name(errno), errno);

			return -1;
		}

		if (mkdir(brickd_dirname, 0700) < 0 && errno != EEXIST) {
			fprintf(stderr, "Could not create '%s': %s (%d)\n",
			        brickd_dirname, get_errno_name(errno), errno);

			return -1;
		}
	} else if (!S_ISDIR(st.st_mode)) {
		fprintf(stderr, "'%s' is not a directory\n", brickd_dirname);

		return -1;
	}

	return 0;
}

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

static int daemon_parent(pid_t child, int status_read) {
	int8_t status = -1;
	ssize_t rc;

	// wait for first child to exit
	while (waitpid(child, NULL, 0) < 0 && errno == EINTR) {
	}

	/*if (waitpid(pid, NULL, 0) < 0) {
		fprintf(stderr, "Could not wait for first child process to exit: %s (%d)\n",
		        get_errno_name(errno), errno);

		close(status_pipe[0]);

		return -1;
	}*/

	// wait for second child to start successfully
	while ((rc = read(status_read, &status, 1)) < 0 && errno == EINTR) {
	}

	if (status < 0) {
		if (rc < 0) {
			fprintf(stderr, "Could not read from status pipe: %s (%d)\n",
			        get_errno_name(errno), errno);
		}

		close(status_read);

		exit(EXIT_FAILURE);
	}

	close(status_read);

	if (status != 1) {
		if (status == 2) {
			fprintf(stderr, "Already running according to '%s'\n", _pid_filename);
		} else {
			fprintf(stderr, "Second child process exited with an error (status: %d)\n",
			        status);
		}

		exit(EXIT_FAILURE);
	}

	// exit first parent
	exit(EXIT_SUCCESS);
}

static int daemon_start(void) {
	int status_pipe[2];
	pid_t pid;
	int8_t status = 0;
	FILE *log_file;
	int pid_fd = -1;
	int stdin_fd = -1;
	int stdout_fd = -1;

	// create status pipe
	if (pipe(status_pipe) < 0) {
		fprintf(stderr, "Could not create status pipe: %s (%d)\n",
		        get_errno_name(errno), errno);

		return -1;
	}

	pid = fork();

	if (pid < 0) {
		fprintf(stderr, "Could not fork first child process: %s (%d)\n",
		        get_errno_name(errno), errno);

		close(status_pipe[0]);
		close(status_pipe[1]);

		return -1;
	}

	if (pid > 0) { // first parent
		close(status_pipe[1]);

		daemon_parent(pid, status_pipe[0]);
	}

	// first child, decouple from parent environment
	close(status_pipe[0]);

	if (chdir("/") < 0) {
		fprintf(stderr, "Could not change directory to '/': %s (%d)\n",
		        get_errno_name(errno), errno);

		close(status_pipe[1]);

		exit(EXIT_FAILURE);
	}

	// FIXME: check error
	setsid();
	umask(0);

	pid = fork();

	if (pid < 0) {
		fprintf(stderr, "Could not fork second child process: %s (%d)\n",
		        get_errno_name(errno), errno);

		close(status_pipe[1]);

		exit(EXIT_FAILURE);
	}

	if (pid > 0) {
		// exit second parent
		exit(EXIT_SUCCESS);
	}

	// second child, write pid
	pid_fd = pidfile_acquire(_pid_filename, getpid());

	if (pid_fd < 0) {
		if (pid_fd < -1) {
			status = 2;
		}

		goto cleanup;
	}

	// open log file
	log_file = fopen(_log_filename, "a+");

	if (log_file == NULL) {
		fprintf(stderr, "Could not open log file '%s': %s (%d)\n",
		        _log_filename, get_errno_name(errno), errno);

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

	while (write(status_pipe[1], &status, 1) < 0 && errno == EINTR) {
	}

	close(status_pipe[1]);

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

	prepare_paths();

	if (check_config) {
		return config_check(_config_filename) < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
	}

	config_init(_config_filename);

	log_init();

	if (daemon) {
		pid_fd = daemon_start();
	} else {
		pid_fd = pidfile_acquire(_pid_filename, getpid());
	}

	if (pid_fd < 0) {
		if (!daemon && pid_fd < -1) {
			fprintf(stderr, "Already running according to '%s'\n", _pid_filename);
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
		         _config_filename);
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

	exit_code = EXIT_SUCCESS;

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

error_log:
	log_exit();

	if (pid_fd >= 0) {
		pidfile_release(_pid_filename, pid_fd);
	}

	config_exit();

	return exit_code;
}
