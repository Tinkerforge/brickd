/*
 * brickd
 * Copyright (C) 2012-2014, 2016-2018 Matthias Bolte <matthias@tinkerforge.com>
 *
 * main_winapi.c: Brick Daemon starting point for Windows
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
#include <stdio.h>
#include <stdlib.h>
#include <winsock2.h>
#include <windows.h>
#include <dbt.h>
#include <conio.h>
#include <fcntl.h>
#include <sys\stat.h>

#ifndef BRICKD_WDK_BUILD
	#include <tlhelp32.h>
#else
typedef struct {
	DWORD dwSize;
	DWORD cntUsage;
	DWORD th32ProcessID;
	ULONG_PTR th32DefaultHeapID;
	DWORD th32ModuleID;
	DWORD cntThreads;
	DWORD th32ParentProcessID;
	LONG pcPriClassBase;
	DWORD dwFlags;
	TCHAR szExeFile[MAX_PATH];
} PROCESSENTRY32;

#define TH32CS_SNAPPROCESS 0x00000002

HANDLE WINAPI CreateToolhelp32Snapshot(DWORD dwFlags, DWORD th32ProcessID);
BOOL WINAPI Process32First(HANDLE hSnapshot, PROCESSENTRY32 *lppe);
BOOL WINAPI Process32Next(HANDLE hSnapshot, PROCESSENTRY32 *lppe);
#endif

#include <daemonlib/config.h>
#include <daemonlib/event.h>
#include <daemonlib/file.h>
#include <daemonlib/log.h>
#include <daemonlib/utils.h>

#include "hardware.h"
#include "network.h"
#include "service.h"
#include "usb.h"
#include "mesh.h"
#include "version.h"

static LogSource _log_source = LOG_SOURCE_INITIALIZER;

static char _config_filename[1024];
static bool _run_as_service = true;
static bool _pause_before_exit = false;

typedef BOOL (WINAPI *query_full_process_image_name_t)(HANDLE, DWORD, char *, DWORD *);

extern void usb_handle_device_event(DWORD event_type, DEV_BROADCAST_HDR *event_data);

static int get_process_image_name(PROCESSENTRY32 entry, char *buffer, DWORD length) {
	int rc;
	HANDLE handle = NULL;
	query_full_process_image_name_t query_full_process_image_name = NULL;

	handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
	                     entry.th32ProcessID);

	if (handle == NULL && GetLastError() == ERROR_ACCESS_DENIED) {
		handle = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE,
		                     entry.th32ProcessID);
	}

	if (handle == NULL) {
		rc = ERRNO_WINAPI_OFFSET + GetLastError();

		log_warn("Could not open process with ID %u: %s (%d)",
		         (uint32_t)entry.th32ProcessID, get_errno_name(rc), rc);

		return -1;
	}

	query_full_process_image_name =
	  (query_full_process_image_name_t)GetProcAddress(GetModuleHandleA("kernel32"),
	                                                  "QueryFullProcessImageNameA");

	if (query_full_process_image_name != NULL) {
		if (query_full_process_image_name(handle, 0, buffer, &length) == 0) {
			rc = ERRNO_WINAPI_OFFSET + GetLastError();

			log_warn("Could not get image name of process with ID %u: %s (%d)",
			         (uint32_t)entry.th32ProcessID, get_errno_name(rc), rc);

			return -1;
		}
	} else {
		memcpy(buffer, entry.szExeFile, length);
		buffer[length - 1] = '\0';
	}

	CloseHandle(handle);

	return 0;
}

static bool started_by_explorer(bool log_available) {
	int rc;
	bool result = false;
	PROCESSENTRY32 entry;
	DWORD process_id = GetCurrentProcessId();
	HANDLE handle = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	char buffer[MAX_PATH];
	size_t length;

	if (handle == INVALID_HANDLE_VALUE) {
		rc = ERRNO_WINAPI_OFFSET + GetLastError();

		if (log_available) {
			log_warn("Could not create process list snapshot: %s (%d)",
			         get_errno_name(rc), rc);
		} else {
			fprintf(stderr, "Could not create process list snapshot: %s (%d)\n",
			        get_errno_name(rc), rc);
		}

		return false;
	}

	memset(&entry, 0, sizeof(entry));

	entry.dwSize = sizeof(PROCESSENTRY32);

	if (Process32First(handle, &entry)) {
		do {
			if (entry.th32ProcessID == process_id) {
				process_id = entry.th32ParentProcessID;

				if (Process32First(handle, &entry)) {
					do {
						if (entry.th32ProcessID == process_id) {
							if (get_process_image_name(entry, buffer,
							                           sizeof(buffer)) < 0) {
								break;
							}

							if (strcasecmp(buffer, "explorer.exe") == 0) {
								result = true;
							} else {
								length = strlen(buffer);

								if (length > 13 /* = strlen("\\explorer.exe") */ &&
								    (strcasecmp(buffer + length - 13, "\\explorer.exe") == 0 ||
								     strcasecmp(buffer + length - 13, ":explorer.exe") == 0)) {
									result = true;
								}
							}

							break;
						}
					} while (Process32Next(handle, &entry));
				}

				break;
			}
		} while (Process32Next(handle, &entry));
	}

	CloseHandle(handle);

	return result;
}

static DWORD WINAPI service_control_handler(DWORD control, DWORD event_type,
                                            LPVOID event_data, LPVOID context) {
	(void)event_data;
	(void)context;

	switch (control) {
	case SERVICE_CONTROL_INTERROGATE:
		return NO_ERROR;

	case SERVICE_CONTROL_SHUTDOWN:
	case SERVICE_CONTROL_STOP:
		if (control == SERVICE_CONTROL_SHUTDOWN) {
			log_info("Received shutdown command");
		} else {
			log_info("Received stop command");
		}

		service_set_status(SERVICE_STOP_PENDING, NO_ERROR);
		event_stop();

		return NO_ERROR;

	case SERVICE_CONTROL_DEVICEEVENT:
		usb_handle_device_event(event_type, event_data);

		return NO_ERROR;
	}

	return ERROR_CALL_NOT_IMPLEMENTED;
}

static BOOL WINAPI console_ctrl_handler(DWORD ctrl_type) {
	switch (ctrl_type) {
	case CTRL_C_EVENT:
		log_info("Received CTRL_C_EVENT");
		break;

	case CTRL_CLOSE_EVENT:
		log_info("Received CTRL_CLOSE_EVENT");
		break;

	case CTRL_BREAK_EVENT:
		log_info("Received CTRL_BREAK_EVENT");
		break;

	case CTRL_LOGOFF_EVENT:
		log_info("Received CTRL_LOGOFF_EVENT");
		break;

	case CTRL_SHUTDOWN_EVENT:
		log_info("Received CTRL_SHUTDOWN_EVENT");
		break;

	default:
		log_warn("Received unknown event %u", (uint32_t)ctrl_type);

		return FALSE;
	}

	_pause_before_exit = false;

	event_stop();

	return TRUE;
}

static void handle_event_cleanup(void) {
	network_cleanup_clients_and_zombies();
	mesh_cleanup_stacks();
}

// NOTE: RegisterServiceCtrlHandlerEx (via service_init) and SetServiceStatus
//       (via service_set_status) need to be called in all circumstances if
//       brickd is running as service
static int generic_main(bool log_to_file, const char *debug_filter) {
	int phase = 0;
	int exit_code = EXIT_FAILURE;
	const char *mutex_name = "Global\\Tinkerforge-Brick-Daemon-Single-Instance";
	HANDLE mutex_handle;
	bool fatal_error = false;
	DWORD service_exit_code = NO_ERROR;
	int rc;
	char filename[1024];
	int i;
	File log_file;
	WSADATA wsa_data;

	mutex_handle = OpenMutexA(SYNCHRONIZE, FALSE, mutex_name);

	if (mutex_handle == NULL) {
		rc = GetLastError();

		if (rc == ERROR_ACCESS_DENIED) {
			rc = service_is_running();

			if (rc < 0) {
				fatal_error = true;
				// FIXME: set service_exit_code

				goto init;
			} else if (rc > 0) {
				fatal_error = true;
				service_exit_code = ERROR_SERVICE_ALREADY_RUNNING;

				log_error("Could not start as %s, another instance is already running as service",
				          _run_as_service ? "service" : "console application");

				goto init;
			}
		}

		if (rc != ERROR_FILE_NOT_FOUND) {
			fatal_error = true;
			// FIXME: set service_exit_code
			rc += ERRNO_WINAPI_OFFSET;

			log_error("Could not open single instance mutex: %s (%d)",
			          get_errno_name(rc), rc);

			goto init;
		}
	}

	if (mutex_handle != NULL) {
		fatal_error = true;
		service_exit_code = ERROR_SERVICE_ALREADY_RUNNING;

		log_error("Could not start as %s, another instance is already running",
		          _run_as_service ? "service" : "console application");

		goto init;
	}

	mutex_handle = CreateMutexA(NULL, FALSE, mutex_name);

	if (mutex_handle == NULL) {
		fatal_error = true;
		// FIXME: set service_exit_code
		rc = ERRNO_WINAPI_OFFSET + GetLastError();

		log_error("Could not create single instance mutex: %s (%d)",
		          get_errno_name(rc), rc);

		goto init;
	}

	if (log_to_file) {
		if (GetModuleFileNameA(NULL, filename, sizeof(filename)) == 0) {
			rc = ERRNO_WINAPI_OFFSET + GetLastError();

			log_warn("Could not get module file name: %s (%d)",
			         get_errno_name(rc), rc);
		} else {
			i = strlen(filename);

			if (i < 4) {
				log_warn("Module file name '%s' is too short", filename);
			} else {
				filename[i - 3] = '\0';
				string_append(filename, sizeof(filename), "log");

				if (file_create(&log_file, filename,
				                _O_CREAT | _O_WRONLY | _O_APPEND,
				                _S_IREAD | _S_IWRITE) < 0) {
					log_warn("Could not open log file '%s': %s (%d)",
					         filename, get_errno_name(errno), errno);
				} else {
					printf("Logging to '%s'\n", filename);

					log_set_output(&log_file.base);
				}
			}
		}
	} else if (_run_as_service) {
		log_set_output(NULL);
	}

	if (!_run_as_service &&
	    !SetConsoleCtrlHandler(console_ctrl_handler, TRUE)) {
		rc = ERRNO_WINAPI_OFFSET + GetLastError();

		log_warn("Could not set console control handler: %s (%d)",
		         get_errno_name(rc), rc);
	}

	if (config_has_error()) {
		fatal_error = true;
		// FIXME: set service_exit_code

		log_error("Error(s) occurred while reading config file '%s'",
		          _config_filename);

		goto init;
	}

	if (_run_as_service) {
		log_info("Brick Daemon %s started (as service)", VERSION_STRING);
	} else {
		log_info("Brick Daemon %s started", VERSION_STRING);
	}

	if (debug_filter != NULL) {
		log_enable_debug_override(debug_filter);
	}

	if (config_has_warning()) {
		log_warn("Warning(s) in config file '%s', run with --check-config option for details",
		         _config_filename);
	}

	// initialize service status
init:
	if (_run_as_service) {
		if (service_init(service_control_handler) < 0) {
			// FIXME: set service_exit_code
			goto cleanup;
		}

		if (!fatal_error) {
			// service is starting
			service_set_status(SERVICE_START_PENDING, NO_ERROR);
		}
	}

	if (fatal_error) {
		goto exit;
	}

	// initialize WinSock2
	if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
		// FIXME: set service_exit_code
		rc = ERRNO_WINAPI_OFFSET + WSAGetLastError();

		log_error("Could not initialize Windows Sockets 2.2: %s (%d)",
		          get_errno_name(rc), rc);

		goto cleanup;
	}

	if (event_init() < 0) {
		// FIXME: set service_exit_code
		goto cleanup;
	}

	phase = 1;

	if (hardware_init() < 0) {
		// FIXME: set service_exit_code
		goto cleanup;
	}

	phase = 2;

	if (usb_init() < 0) {
		// FIXME: set service_exit_code
		goto cleanup;
	}

	phase = 3;

	if (network_init() < 0) {
		// FIXME: set service_exit_code
		goto cleanup;
	}

	phase = 4;

	if (mesh_init() < 0) {
		// FIXME: set service_exit_code
		goto cleanup;
	}

	phase = 5;

	// running
	if (_run_as_service) {
		service_set_status(SERVICE_RUNNING, NO_ERROR);
	}

	if (event_run(handle_event_cleanup) < 0) {
		// FIXME: set service_exit_code
		goto cleanup;
	}

	exit_code = EXIT_SUCCESS;

cleanup:
	switch (phase) { // no breaks, all cases fall through intentionally
	case 5:
		mesh_exit();
		// fall through

	case 4:
		network_exit();
		// fall through

	case 3:
		usb_exit();
		// fall through

	case 2:
		hardware_exit();
		// fall through

	case 1:
		event_exit();
		// fall through

	default:
		break;
	}

	log_info("Brick Daemon %s stopped", VERSION_STRING);

exit:
	if (!_run_as_service) {
		// unregister the console handler before exiting the log. otherwise a
		// control event might be send to the control handler after the log
		// is not available anymore and the control handler tries to write a
		// log messages triggering a crash. this situation could easily be
		// created by clicking the close button of the command prompt window
		// while the getch call is waiting for the user to press a key.
		SetConsoleCtrlHandler(console_ctrl_handler, FALSE);
	}

	log_exit();

	config_exit();

	if (_run_as_service) {
		// because the service process can be terminated at any time after
		// entering SERVICE_STOPPED state the mutex is closed beforehand,
		// even though this creates a tiny time window in which the service
		// is still running but the mutex is not held anymore
		if (mutex_handle != NULL) {
			CloseHandle(mutex_handle);
		}

		// service is now stopped
		service_set_status(SERVICE_STOPPED, service_exit_code);
	} else {
		if (_pause_before_exit) {
			printf("Press any key to exit...\n");
			getch();
		}

		if (mutex_handle != NULL) {
			CloseHandle(mutex_handle);
		}
	}

	return exit_code;
}

static void WINAPI service_main(DWORD argc, char **argv) {
	DWORD i;
	bool log_to_file = false;
	const char *debug_filter = NULL;

	for (i = 1; i < argc; ++i) {
		if (strcmp(argv[i], "--log-to-file") == 0) {
			log_to_file = true;
		} else if (strcmp(argv[i], "--debug") == 0) {
			if (i + 1 < argc && strncmp(argv[i + 1], "--", 2) != 0) {
				debug_filter = argv[++i];
			} else {
				debug_filter = "";
			}
		} else {
			log_warn("Unknown start parameter '%s'", argv[i]);
		}
	}

	generic_main(log_to_file, debug_filter);
}

static int service_run(bool log_to_file, const char *debug_filter) {
	SERVICE_TABLE_ENTRYA service_table[2];
	int rc;

	service_table[0].lpServiceName = service_get_name();
	service_table[0].lpServiceProc = service_main;

	service_table[1].lpServiceName = NULL;
	service_table[1].lpServiceProc = NULL;

	if (!StartServiceCtrlDispatcherA(service_table)) {
		rc = ERRNO_WINAPI_OFFSET + GetLastError();

		if (rc == ERRNO_WINAPI_OFFSET + ERROR_FAILED_SERVICE_CONTROLLER_CONNECT) {
			if (log_to_file) {
				printf("Could not start as service, starting as console application\n");
			} else {
				log_info("Could not start as service, starting as console application");
			}

			_run_as_service = false;
			_pause_before_exit = started_by_explorer(true);

			return generic_main(log_to_file, debug_filter);
		} else {
			log_error("Could not start service control dispatcher: %s (%d)",
			          get_errno_name(rc), rc);

			log_exit();

			config_exit();

			return EXIT_FAILURE;
		}
	}

	return EXIT_SUCCESS;
}

static void print_usage(void) {
	printf("Usage:\n"
	       "  brickd [--help|--version|--check-config|--install|--uninstall|--console]\n"
	       "         [--log-to-file] [--debug [<filter>]]\n"
	       "\n"
	       "Options:\n"
	       "  --help              Show this help\n"
	       "  --version           Show version number\n"
	       "  --check-config      Check config file for errors\n"
	       "  --install           Register as a service and start it\n"
	       "  --uninstall         Stop service and unregister it\n"
	       "  --console           Force start as console application\n"
	       "  --log-to-file       Write log messages to file\n"
	       "  --debug [<filter>]  Set log level to debug and apply optional filter\n");

	if (started_by_explorer(false)) {
		printf("\nPress any key to exit...\n");
		getch();
	}
}

static void print_version(void) {
	printf("%s\n", VERSION_STRING);

	if (started_by_explorer(false)) {
		printf("\nPress any key to exit...\n");
		getch();
	}
}

// NOTE: generic_main (directly or via service_run) needs to be called in all
//       circumstances if brickd is running as service
int main(int argc, char **argv) {
	int i;
	bool help = false;
	bool version = false;
	bool check_config = false;
	bool install = false;
	bool uninstall = false;
	bool console = false;
	bool log_to_file = false;
	const char *debug_filter = NULL;
	int rc;

	fixes_init();

	for (i = 1; i < argc; ++i) {
		if (strcmp(argv[i], "--help") == 0) {
			help = true;
		} else if (strcmp(argv[i], "--version") == 0) {
			version = true;
		} else if (strcmp(argv[i], "--check-config") == 0) {
			check_config = true;
		} else if (strcmp(argv[i], "--install") == 0) {
			install = true;
		} else if (strcmp(argv[i], "--uninstall") == 0) {
			uninstall = true;
		} else if (strcmp(argv[i], "--console") == 0) {
			console = true;
		} else if (strcmp(argv[i], "--log-to-file") == 0) {
			log_to_file = true;
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
		print_version();

		return EXIT_SUCCESS;
	}

	if (GetModuleFileNameA(NULL, _config_filename, sizeof(_config_filename)) == 0) {
		rc = ERRNO_WINAPI_OFFSET + GetLastError();

		fprintf(stderr, "Could not get module file name: %s (%d)\n",
		        get_errno_name(rc), rc);

		return EXIT_FAILURE;
	}

	i = strlen(_config_filename);

	if (i < 4) {
		fprintf(stderr, "Module file name '%s' is too short", _config_filename);

		return EXIT_FAILURE;
	}

	_config_filename[i - 3] = '\0';
	string_append(_config_filename, sizeof(_config_filename), "ini");

	if (check_config) {
		rc = config_check(_config_filename);

		if (started_by_explorer(false)) {
			printf("\nPress any key to exit...\n");
			getch();
		}

		return rc < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
	}

	if (install && uninstall) {
		fprintf(stderr, "Invalid option combination\n");
		print_usage();

		return EXIT_FAILURE;
	}

	if (install) {
		if (service_install(log_to_file, debug_filter) < 0) {
			return EXIT_FAILURE;
		}
	} else if (uninstall) {
		if (service_uninstall() < 0) {
			return EXIT_FAILURE;
		}
	} else {
		printf("Starting...\n");

		config_init(_config_filename);

		log_init();

		if (console) {
			_run_as_service = false;
			_pause_before_exit = started_by_explorer(true);

			return generic_main(log_to_file, debug_filter);
		} else {
			return service_run(log_to_file, debug_filter);
		}
	}

	return EXIT_SUCCESS;
}
