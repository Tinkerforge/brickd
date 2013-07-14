/*
 * brickd
 * Copyright (C) 2012-2013 Matthias Bolte <matthias@tinkerforge.com>
 *
 * main_windows.c: Brick Daemon starting point for Windows
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
#include <stdio.h>
#include <stdlib.h>
#include <winsock2.h>
#include <windows.h>
#include <dbt.h>
#include <conio.h>

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

#include "config.h"
#include "event.h"
#include "log.h"
#include "network.h"
#include "pipe.h"
#include "service.h"
#include "threads.h"
#include "usb.h"
#include "utils.h"
#include "version.h"

#define LOG_CATEGORY LOG_CATEGORY_OTHER

static const GUID GUID_DEVINTERFACE_USB_DEVICE =
{ 0xA5DCBF10L, 0x6530, 0x11D2, { 0x90, 0x1F, 0x00, 0xC0, 0x4F, 0xB9, 0x51, 0xED } };

static char _config_filename[1024];
static int _run_as_service = 1;
static int _pause_before_exit = 0;
static EventHandle _notification_pipe[2] = { INVALID_EVENT_HANDLE,
                                             INVALID_EVENT_HANDLE };
static HWND _message_pump_hwnd = NULL;
static Thread _message_pump_thread;
static int _message_pump_running = 0;

typedef BOOL (WINAPI *QUERYFULLPROCESSIMAGENAME)(HANDLE, DWORD, LPTSTR, PDWORD);

static int get_process_image_name(PROCESSENTRY32 entry, char *buffer, DWORD length) {
	int rc;
	HANDLE handle = NULL;
	QUERYFULLPROCESSIMAGENAME query_full_process_image_name = NULL;

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
	  (QUERYFULLPROCESSIMAGENAME)GetProcAddress(GetModuleHandle("kernel32"),
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

static int started_by_explorer(void) {
	int rc;
	int result = 0;
	PROCESSENTRY32 entry;
	DWORD process_id = GetCurrentProcessId();
	HANDLE handle = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	char buffer[MAX_PATH];
	size_t length;

	if (handle == INVALID_HANDLE_VALUE) {
		rc = ERRNO_WINAPI_OFFSET + GetLastError();

		log_warn("Could not create process list snapshot: %s (%d)",
		         get_errno_name(rc), rc);

		return 0;
	}

	ZeroMemory(&entry, sizeof(entry));

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

							if (stricmp(buffer, "explorer.exe") == 0) {
								result = 1;
							} else {
								length = strlen(buffer);

								if (length > 13 /* = strlen("\\explorer.exe") */ &&
								    (stricmp(buffer + length - 13, "\\explorer.exe") == 0 ||
								     stricmp(buffer + length - 13, ":explorer.exe") == 0)) {
									result = 1;
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

static void forward_notifications(void *opaque) {
	uint8_t byte;

	(void)opaque;

	if (pipe_read(_notification_pipe[0], &byte, sizeof(byte)) < 0) {
		log_error("Could not read from notification pipe: %s (%d)",
		          get_errno_name(errno), errno);

		return;
	}

	usb_update();
}

static void handle_device_event(DWORD event_type) {
	uint8_t byte = 0;

	switch (event_type) {
	case DBT_DEVICEARRIVAL:
		log_debug("Received device notification (type: arrival)");

		if (pipe_write(_notification_pipe[1], &byte, sizeof(byte)) < 0) {
			log_error("Could not write to notification pipe: %s (%d)",
			          get_errno_name(errno), errno);
		}

		break;

	case DBT_DEVICEREMOVECOMPLETE:
		log_debug("Received device notification (type: removal)");

		if (pipe_write(_notification_pipe[1], &byte, sizeof(byte)) < 0) {
			log_error("Could not write to notification pipe: %s (%d)",
			          get_errno_name(errno), errno);
		}

		break;
	}
}

static LRESULT CALLBACK message_pump_window_proc(HWND hwnd, UINT msg,
                                                 WPARAM wparam, LPARAM lparam) {
	int rc;

	switch (msg) {
	case WM_USER:
		log_debug("Destroying message pump window");

		if (!DestroyWindow(hwnd)) {
			rc = ERRNO_WINAPI_OFFSET + GetLastError();

			log_warn("Could not destroy message pump window: %s (%d)",
			         get_errno_name(rc), rc);
		}

		return 0;

	case WM_DESTROY:
		log_debug("Posting quit message message loop");

		PostQuitMessage(0);

		return 0;

	case WM_DEVICECHANGE:
		handle_device_event(wparam);

		return TRUE;
	}

	return DefWindowProc(hwnd, msg, wparam, lparam);
}

static void message_pump_thread_proc(void *opaque) {
	const char *class_name = "brickd_message_pump";
	Semaphore *handshake = opaque;
	WNDCLASSEX wc;
	int rc;
	MSG msg;

	log_debug("Started message pump thread");

	ZeroMemory(&wc, sizeof(wc));

	wc.cbSize = sizeof(wc);
	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc = (WNDPROC)message_pump_window_proc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = NULL;
	wc.hIcon = NULL;
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)COLOR_WINDOW;
	wc.lpszMenuName = NULL;
	wc.lpszClassName = class_name;
	wc.hIconSm = NULL;

	if (RegisterClassEx(&wc) == 0) {
		rc = ERRNO_WINAPI_OFFSET + GetLastError();

		log_error("Could not register message pump window class: %s (%d)",
		          get_errno_name(rc), rc);

		return;
	}

	_message_pump_hwnd = CreateWindowEx(0, class_name, "brickd message pump",
	                                    0, 0, 0, 0, 0, HWND_MESSAGE,
	                                    NULL, NULL, NULL);

	if (_message_pump_hwnd == NULL) {
		rc = ERRNO_WINAPI_OFFSET + GetLastError();

		log_error("Could not create message pump window: %s (%d)",
		          get_errno_name(rc), rc);

		return;
	}

	_message_pump_running = 1;

	semaphore_release(handshake);

	while (_message_pump_running &&
	       (rc = GetMessage(&msg, _message_pump_hwnd, 0, 0)) != 0) {
		if (rc < 0) {
			rc = ERRNO_WINAPI_OFFSET + GetLastError();

			if (rc == ERRNO_WINAPI_OFFSET + ERROR_INVALID_WINDOW_HANDLE) {
				log_debug("Message pump window seems to be destroyed");

				break;
			}

			log_warn("Could not get window message: %s (%d)",
			          get_errno_name(rc), rc);
		} else {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}

	log_debug("Stopped message pump thread");

	_message_pump_running = 0;
}

static int message_pump_start(void) {
	Semaphore handshake;

	log_debug("Starting message pump");

	semaphore_create(&handshake);

	thread_create(&_message_pump_thread, message_pump_thread_proc, &handshake);

	semaphore_acquire(&handshake);
	semaphore_destroy(&handshake);

	return _message_pump_hwnd == NULL ? -1 : 0;
}

static void message_pump_stop(void) {
	int rc;

	log_debug("Stopping message pump");

	_message_pump_running = 0;

	if (!PostMessage(_message_pump_hwnd, WM_USER, 0, 0)) {
		rc = ERRNO_WINAPI_OFFSET + GetLastError();

		log_warn("Could not trigger destruction of message pump window: %s (%d)",
		         get_errno_name(rc), rc);
	} else {
		thread_join(&_message_pump_thread);
	}

	thread_destroy(&_message_pump_thread);
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
		handle_device_event(event_type);

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

	_pause_before_exit = 0;

	event_stop();

	return TRUE;
}

// NOTE: this function needs to call RegisterServiceCtrlHandlerEx and
// SetServiceStatus in all circumstances if brickd is running as service
static int generic_main(int log_to_file, int debug) {
	int exit_code = EXIT_FAILURE;
	const char *mutex_name = "Global\\Tinkerforge-Brick-Daemon-Single-Instance";
	HANDLE mutex_handle = NULL;
	int mutex_error = 0;
	DWORD service_exit_code = NO_ERROR;
	int rc;
	char filename[1024];
	int i;
	FILE *logfile = NULL;
	WSADATA wsa_data;
	DEV_BROADCAST_DEVICEINTERFACE notification_filter;
	HDEVNOTIFY notification_handle;

	mutex_handle = OpenMutex(SYNCHRONIZE, FALSE, mutex_name);

	if (mutex_handle == NULL) {
		rc = GetLastError();

		if (rc == ERROR_ACCESS_DENIED) {
			rc = service_is_running();

			if (rc < 0) {
				mutex_error = 1;
				// FIXME: set service_exit_code

				goto error_mutex;
			} else if (rc) {
				mutex_error = 1;
				service_exit_code = ERROR_SERVICE_ALREADY_RUNNING;

				log_error("Could not start as %s, another instance is already running as service",
				          _run_as_service ? "service" : "console application");

				goto error_mutex;
			}
		}

		if (rc != ERROR_FILE_NOT_FOUND) {
			mutex_error = 1;
			// FIXME: set service_exit_code
			rc += ERRNO_WINAPI_OFFSET;

			log_error("Could not open single instance mutex: %s (%d)",
			          get_errno_name(rc), rc);

			goto error_mutex;
		}
	}

	if (mutex_handle != NULL) {
		mutex_error = 1;
		service_exit_code = ERROR_SERVICE_ALREADY_RUNNING;

		log_error("Could not start as %s, another instance is already running",
		          _run_as_service ? "service" : "console application");

		goto error_mutex;
	}

	mutex_handle = CreateMutex(NULL, FALSE, mutex_name);

	if (mutex_handle == NULL) {
		mutex_error = 1;
		// FIXME: set service_exit_code
		rc = ERRNO_WINAPI_OFFSET + GetLastError();

		log_error("Could not create single instance mutex: %s (%d)",
		          get_errno_name(rc), rc);

		goto error_mutex;
	}

	if (!_run_as_service &&
	    !SetConsoleCtrlHandler(console_ctrl_handler, TRUE)) {
		rc = ERRNO_WINAPI_OFFSET + GetLastError();

		log_warn("Could not set console control handler: %s (%d)",
		          get_errno_name(rc), rc);
	}

	if (log_to_file) {
		if (GetModuleFileName(NULL, filename, sizeof(filename)) == 0) {
			rc = ERRNO_WINAPI_OFFSET + GetLastError();

			log_warn("Could not get module file name: %s (%d)",
			         get_errno_name(rc), rc);
		} else {
			i = strlen(filename);

			if (i < 4) {
				log_warn("Module file name '%s' is too short", filename);
			} else {
				strcpy(filename + i - 3, "log");

				logfile = fopen(filename, "a+");

				if (logfile == NULL) {
					log_warn("Could not open logfile '%s'", filename);
				} else {
					log_set_file(logfile);
				}
			}
		}
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

	if (_run_as_service) {
		log_info("Brick Daemon %s started (as service)", VERSION_STRING);
	} else {
		log_info("Brick Daemon %s started", VERSION_STRING);
	}

	if (config_has_error()) {
		log_warn("Errors found in config file '%s', run with --check-config option for details",
		         _config_filename);
	}

	// initialize service status
error_mutex:
	if (_run_as_service) {
		if (service_init(service_control_handler) < 0) {
			// FIXME: set service_exit_code
			goto error;
		}

		if (!mutex_error) {
			// service is starting
			service_set_status(SERVICE_START_PENDING, NO_ERROR);
		}
	}

	if (mutex_error) {
		goto error;
	}

	// initialize WinSock2
	if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
		// FIXME: set service_exit_code
		rc = ERRNO_WINAPI_OFFSET + WSAGetLastError();

		log_error("Could not initialize Windows Sockets 2.2: %s (%d)",
		          get_errno_name(rc), rc);

		goto error_event;
	}

	if (event_init() < 0) {
		// FIXME: set service_exit_code
		goto error_event;
	}

	if (usb_init() < 0) {
		// FIXME: set service_exit_code
		goto error_usb;
	}

	// create notification pipe
	if (pipe_create(_notification_pipe) < 0) {
		// FIXME: set service_exit_code

		log_error("Could not create notification pipe: %s (%d)",
		          get_errno_name(errno), errno);

		goto error_pipe;
	}

	if (event_add_source(_notification_pipe[0], EVENT_SOURCE_TYPE_GENERIC,
	                     EVENT_READ, forward_notifications, NULL) < 0) {
		// FIXME: set service_exit_code
		goto error_pipe_add;
	}

	// register device notification
	ZeroMemory(&notification_filter, sizeof(notification_filter));

	notification_filter.dbcc_size = sizeof(notification_filter);
	notification_filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
	notification_filter.dbcc_classguid = GUID_DEVINTERFACE_USB_DEVICE;

	if (_run_as_service) {
		notification_handle = RegisterDeviceNotification((HANDLE)service_get_status_handle(),
		                                                 &notification_filter,
		                                                 DEVICE_NOTIFY_SERVICE_HANDLE);
	} else {
		if (message_pump_start() < 0) {
			// FIXME: set service_exit_code
			goto error_pipe_add;
		}

		notification_handle = RegisterDeviceNotification(_message_pump_hwnd,
		                                                 &notification_filter,
		                                                 DEVICE_NOTIFY_WINDOW_HANDLE);
	}

	if (notification_handle == NULL) {
		// FIXME: set service_exit_code
		rc = ERRNO_WINAPI_OFFSET + GetLastError();

		log_error("Could not register for device notification: %s (%d)",
		          get_errno_name(rc), rc);

		goto error_notification;
	}

	if (network_init() < 0) {
		// FIXME: set service_exit_code
		goto error_network;
	}

	// running
	if (_run_as_service) {
		service_set_status(SERVICE_RUNNING, NO_ERROR);
	}

	if (event_run() < 0) {
		// FIXME: set service_exit_code
		goto error_run;
	}

	exit_code = EXIT_SUCCESS;

error_run:
	network_exit();

error_network:
	UnregisterDeviceNotification(notification_handle);

error_notification:
	if (!_run_as_service) {
		message_pump_stop();
	}

	event_remove_source(_notification_pipe[0], EVENT_SOURCE_TYPE_GENERIC);

error_pipe_add:
	pipe_destroy(_notification_pipe);

error_pipe:
	usb_exit();

error_usb:
	event_exit();

error_event:
	log_info("Brick Daemon %s stopped", VERSION_STRING);

error:
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

static void WINAPI service_main(DWORD argc, LPTSTR *argv) {
	DWORD i;
	int log_to_file = 0;
	int debug = 0;

	for (i = 1; i < argc; ++i) {
		if (strcmp(argv[i], "--log-to-file") == 0) {
			log_to_file = 1;
		} else if (strcmp(argv[i], "--debug") == 0) {
			debug = 1;
		} else {
			log_warn("Unknown start parameter '%s'", argv[i]);
		}
	}

	generic_main(log_to_file, debug);
}

static int service_run(int log_to_file, int debug) {
	SERVICE_TABLE_ENTRY service_table[2];
	int rc;

	service_table[0].lpServiceName = service_get_name();
	service_table[0].lpServiceProc = service_main;

	service_table[1].lpServiceName = NULL;
	service_table[1].lpServiceProc = NULL;

	if (!StartServiceCtrlDispatcher(service_table)) {
		rc = ERRNO_WINAPI_OFFSET + GetLastError();

		if (rc == ERRNO_WINAPI_OFFSET + ERROR_FAILED_SERVICE_CONTROLLER_CONNECT) {
			log_info("Could not start as service, starting as console application");

			_run_as_service = 0;
			_pause_before_exit = started_by_explorer();

			return generic_main(log_to_file, debug);
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
	       "         [--log-to-file] [--debug]\n"
	       "\n"
	       "Options:\n"
	       "  --help          Show this help\n"
	       "  --version       Show version number\n"
	       "  --check-config  Check config file for errors\n"
	       "  --install       Register as a service and start it\n"
	       "  --uninstall     Stop service and unregister it\n"
	       "  --console       Force start as console application\n"
	       "  --log-to-file   Write log messages to file\n"
	       "  --debug         Set all log levels to debug\n");
}

int main(int argc, char **argv) {
	int i;
	int help = 0;
	int version = 0;
	int check_config = 0;
	int install = 0;
	int uninstall = 0;
	int console = 0;
	int log_to_file = 0;
	int debug = 0;
	int rc;

	for (i = 1; i < argc; ++i) {
		if (strcmp(argv[i], "--help") == 0) {
			help = 1;
		} else if (strcmp(argv[i], "--version") == 0) {
			version = 1;
		} else if (strcmp(argv[i], "--check-config") == 0) {
			check_config = 1;
		} else if (strcmp(argv[i], "--install") == 0) {
			install = 1;
		} else if (strcmp(argv[i], "--uninstall") == 0) {
			uninstall = 1;
		} else if (strcmp(argv[i], "--console") == 0) {
			console = 1;
		} else if (strcmp(argv[i], "--log-to-file") == 0) {
			log_to_file = 1;
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

	if (GetModuleFileName(NULL, _config_filename, sizeof(_config_filename)) == 0) {
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

	strcpy(_config_filename + i - 3, "ini");

	if (check_config) {
		return config_check(_config_filename) < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
	}

	if (install && uninstall) {
		fprintf(stderr, "Invalid option combination\n");
		print_usage();

		return EXIT_FAILURE;
	}

	if (install) {
		if (service_install(log_to_file, debug) < 0) {
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
			_run_as_service = 0;
			_pause_before_exit = started_by_explorer();

			return generic_main(log_to_file, debug);
		} else {
			return service_run(log_to_file, debug);
		}
	}

	return EXIT_SUCCESS;
}
