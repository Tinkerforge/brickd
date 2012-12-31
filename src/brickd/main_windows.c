/*
 * brickd
 * Copyright (C) 2012 Matthias Bolte <matthias@tinkerforge.com>
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
#include <windows.h>
#include <dbt.h>

#include "event.h"
#include "log.h"
#include "network.h"
#include "pipe.h"
#include "usb.h"
#include "utils.h"
#include "version.h"

#define LOG_CATEGORY LOG_CATEGORY_OTHER

static const GUID GUID_DEVINTERFACE_USB_DEVICE =
{ 0xA5DCBF10L, 0x6530, 0x11D2, { 0x90, 0x1F, 0x00, 0xC0, 0x4F, 0xB9, 0x51, 0xED } };

static char *_service_name = "Brick Daemon";
static char *_service_description = "Brick Daemon is a bridge between USB devices (Bricks) and TCP/IP sockets. It can be used to read out and control Bricks.";
static SERVICE_STATUS _service_status;
static SERVICE_STATUS_HANDLE _service_status_handle = 0;
static EventHandle _notification_pipe[2] = { INVALID_EVENT_HANDLE,
                                             INVALID_EVENT_HANDLE };

static void forward_notifications(void *opaque) {
	uint8_t byte;

	(void)opaque;

	if (pipe_read(_notification_pipe[0], &byte, sizeof(uint8_t)) < 0) {
		log_error("Could not read from notification pipe: %s (%d)",
		          get_errno_name(errno), errno);

		return;
	}

	usb_update();
}

static DWORD WINAPI service_control_handler(DWORD control, DWORD eventType,
                                            LPVOID eventData, LPVOID context) {
	uint8_t byte = 0;

	(void)eventData;
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

		_service_status.dwCurrentState = SERVICE_STOP_PENDING;

		SetServiceStatus(_service_status_handle, &_service_status);

		event_stop();

		return NO_ERROR;

	case SERVICE_CONTROL_DEVICEEVENT:
		switch (eventType) {
		case DBT_DEVICEARRIVAL:
			log_debug("Received device notification (type: arrival)");

			if (pipe_write(_notification_pipe[1], &byte, sizeof(uint8_t)) < 0) {
				log_error("Could not write to notification pipe: %s (%d)",
				          get_errno_name(errno), errno);
			}

			break;

		case DBT_DEVICEREMOVECOMPLETE:
			log_debug("Received device notification (type: removal)");

			if (pipe_write(_notification_pipe[1], &byte, sizeof(uint8_t)) < 0) {
				log_error("Could not write to notification pipe: %s (%d)",
				          get_errno_name(errno), errno);
			}

			break;
		}

		return NO_ERROR;
	}

	return ERROR_CALL_NOT_IMPLEMENTED;
}

static void WINAPI service_main(DWORD argc, LPTSTR *argv) {
	DWORD i;
	int debug = 0;
	char path[1024];
	int rc;
	FILE *logfile = NULL;
	errno_t error;
	WSADATA wsa_data;
	DEV_BROADCAST_DEVICEINTERFACE notification_filter;
	HDEVNOTIFY notification_handle;

	for (i = 1; i < argc; ++i) {
		if (strcmp(argv[i], "--debug") == 0) {
			debug = 1;
		} else {
			log_warn("Unknown option '%s'", argv[i]);
		}
	}

	// open debug log
	if (debug) {
		if (GetModuleFileName(NULL, path, sizeof(path)) == 0) {
			rc = ERRNO_WINAPI_OFFSET + GetLastError();

			log_warn("Could not get module file name: %s (%d)",
			         get_errno_name(rc), rc);
		} else {
			i = strlen(path);

			if (i < 4) {
				log_warn("Module file name '%s' is too short", path);
			} else {
				strncpy_s(path + i - 3, sizeof(path) - i + 3, "log", 3);

				error = fopen_s(&logfile, path, "a+");

				if (error != 0) {
					log_warn("Could not open logfile '%s': %s (%d)",
					         path, get_errno_name(error), error);
				} else {
					log_set_file(logfile);
				}
			}
		}

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

	log_info("Brick Daemon %s started", VERSION_STRING);

	// initialize service status
	_service_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
	_service_status.dwCurrentState = SERVICE_STOPPED;
	_service_status.dwControlsAccepted = 0;
	_service_status.dwWin32ExitCode = NO_ERROR;
	_service_status.dwServiceSpecificExitCode = NO_ERROR;
	_service_status.dwCheckPoint = 0;
	_service_status.dwWaitHint = 0;

	_service_status_handle = RegisterServiceCtrlHandlerEx(_service_name,
	                                                      service_control_handler,
	                                                      NULL);

	if (_service_status_handle == NULL) {
		rc = ERRNO_WINAPI_OFFSET + GetLastError();

		log_error("Could not register service control handler: %s (%d)",
		          get_errno_name(rc), rc);

		return;
	}

	// service is starting
	_service_status.dwCurrentState = SERVICE_START_PENDING;

	SetServiceStatus(_service_status_handle, &_service_status);

	// initialize Winsock2
	if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
		rc = ERRNO_WINSOCK2_OFFSET + WSAGetLastError();

		log_error("Could not initialize Windows Sockets 2.2: %s (%d)",
		          get_errno_name(rc), rc);

		goto error_event;
	}

	if (event_init() < 0) {
		goto error_event;
	}

	if (usb_init() < 0) {
		goto error_usb;
	}

	// create notification pipe
	if (pipe_create(_notification_pipe) < 0) {
		log_error("Could not create notification pipe: %s (%d)",
		          get_errno_name(errno), errno);

		goto error_pipe;
	}

	if (event_add_source(_notification_pipe[0], EVENT_SOURCE_TYPE_GENERIC,
	                     EVENT_READ, forward_notifications, NULL) < 0) {
		goto error_pipe_add;
	}

	// register device notification
	ZeroMemory(&notification_filter, sizeof(DEV_BROADCAST_DEVICEINTERFACE));

	notification_filter.dbcc_size = sizeof(DEV_BROADCAST_DEVICEINTERFACE);
	notification_filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
	notification_filter.dbcc_classguid = GUID_DEVINTERFACE_USB_DEVICE;

	notification_handle = RegisterDeviceNotification(_service_status_handle,
	                                                 &notification_filter,
	                                                 DEVICE_NOTIFY_SERVICE_HANDLE);

	if (notification_handle == NULL) {
		rc = ERRNO_WINAPI_OFFSET + GetLastError();

		log_error("Could not register for device notification: %s (%d)",
		          get_errno_name(rc), rc);

		goto error_notification;
	}

	if (network_init() < 0) {
		goto error_network;
	}

	// running
	_service_status.dwControlsAccepted |= SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
	_service_status.dwCurrentState = SERVICE_RUNNING;

	SetServiceStatus(_service_status_handle, &_service_status);

	if (event_run() < 0) {
		goto error_run;
	}

error_run:
	network_exit();

error_network:
	UnregisterDeviceNotification(notification_handle);

error_notification:
	event_remove_source(_notification_pipe[0], EVENT_SOURCE_TYPE_GENERIC);

error_pipe_add:
	pipe_destroy(_notification_pipe);

error_pipe:
	usb_exit();

error_usb:
	event_exit();

error_event:
	log_info("Brick Daemon %s stopped", VERSION_STRING);

	log_exit();

	// service is now stopped
	_service_status.dwControlsAccepted &= ~(SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN);
	_service_status.dwCurrentState = SERVICE_STOPPED;

	SetServiceStatus(_service_status_handle, &_service_status);
}

static void service_run(void) {
	SERVICE_TABLE_ENTRY service_table[2];
	int rc;

	service_table[0].lpServiceName = _service_name;
	service_table[0].lpServiceProc = service_main;

	service_table[1].lpServiceName = NULL;
	service_table[1].lpServiceProc = NULL;

	if (!StartServiceCtrlDispatcher(service_table)) {
		rc = ERRNO_WINAPI_OFFSET + GetLastError();

		log_error("Could not start service control dispatcher: %s (%d)",
		          get_errno_name(rc), rc);

		return;
	}
}

static int service_install(int debug) {
	SC_HANDLE service_control_manager;
	int rc;
	char path[1024];
	SC_HANDLE service;
	SERVICE_DESCRIPTION description;
	LPCTSTR debug_argv[1];
	DWORD argc = 0;
	LPCTSTR *argv = NULL;

	if (debug) {
		debug_argv[0] = "--debug";

		argc = 1;
		argv = debug_argv;
	}

	// open service control manager
	service_control_manager = OpenSCManager(0, 0, SC_MANAGER_CREATE_SERVICE);

	if (service_control_manager == NULL) {
		rc = ERRNO_WINAPI_OFFSET + GetLastError();

		fprintf(stderr, "Could not open service control manager: %s (%d)\n",
		        get_errno_name(rc), rc);

		return -1;
	}

	if (GetModuleFileName(NULL, path, sizeof(path)) == 0) {
		rc = ERRNO_WINAPI_OFFSET + GetLastError();

		fprintf(stderr, "Could not get module file name: %s (%d)\n",
		        get_errno_name(rc), rc);

		CloseServiceHandle(service_control_manager);

		return -1;
	}

	// install service
	service = CreateService(service_control_manager, _service_name, _service_name,
	                        SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS,
	                        SERVICE_AUTO_START, SERVICE_ERROR_NORMAL, path,
	                        NULL, NULL, NULL, NULL, NULL);

	if (service == NULL) {
		rc = GetLastError();

		if (rc != ERROR_SERVICE_EXISTS) {
			rc += ERRNO_WINAPI_OFFSET;

			fprintf(stderr, "Could not install '%s' service: %s (%d)\n",
			        _service_name, get_errno_name(rc), rc);

			CloseServiceHandle(service_control_manager);

			return -1;
		} else {
			printf("'%s' service is already installed\n", _service_name);

			service = OpenService(service_control_manager, _service_name,
			                      SERVICE_CHANGE_CONFIG | SERVICE_START);

			if (service == NULL) {
				rc = ERRNO_WINAPI_OFFSET + GetLastError();

				fprintf(stderr, "Could not open '%s' service: %s (%d)\n",
				        _service_name, get_errno_name(rc), rc);

				CloseServiceHandle(service_control_manager);

				return -1;
			}
		}
	} else {
		printf("Installed '%s' service\n", _service_name);
	}

	// update description
	description.lpDescription = _service_description;

	if (!ChangeServiceConfig2(service, SERVICE_CONFIG_DESCRIPTION,
	                          &description)) {
		rc = ERRNO_WINAPI_OFFSET + GetLastError();

		fprintf(stderr, "Could not update description of '%s' service: %s (%d)\n",
		        _service_name, get_errno_name(rc), rc);

		CloseServiceHandle(service);
		CloseServiceHandle(service_control_manager);

		return -1;
	}

	// start service
	if (!StartService(service, argc, argv)) {
		rc = GetLastError();

		if (rc != ERROR_SERVICE_ALREADY_RUNNING) {
			rc += ERRNO_WINAPI_OFFSET;

			fprintf(stderr, "Could not start '%s' service: %s (%d)\n",
			        _service_name, get_errno_name(rc), rc);

			CloseServiceHandle(service);
			CloseServiceHandle(service_control_manager);

			return -1;
		} else {
			printf("'%s' service is already running\n", _service_name);
		}
	} else {
		if (debug) {
			printf("Started '%s' service with --debug option\n", _service_name);
		} else {
			printf("Started '%s' service\n", _service_name);
		}
	}

	CloseServiceHandle(service);
	CloseServiceHandle(service_control_manager);

	return 0;
}

static int service_uninstall(void) {
	SC_HANDLE service_control_manager;
	int rc;
	SC_HANDLE service;
	SERVICE_STATUS service_status;
	int tries = 0;

	// open service control manager
	service_control_manager = OpenSCManager(0, 0, SC_MANAGER_CONNECT);

	if (service_control_manager == NULL) {
		rc = ERRNO_WINAPI_OFFSET + GetLastError();

		fprintf(stderr, "Could not open service control manager: %s (%d)\n",
		        get_errno_name(rc), rc);

		return -1;
	}

	// open service
	service = OpenService(service_control_manager, _service_name,
	                      SERVICE_QUERY_STATUS | SERVICE_STOP | DELETE);

	if (service == NULL) {
		rc = GetLastError();

		if (rc == ERROR_SERVICE_DOES_NOT_EXIST) {
			fprintf(stderr, "'%s' service is not installed\n", _service_name);

			CloseServiceHandle(service_control_manager);

			return -1;
		}

		rc += ERRNO_WINAPI_OFFSET;

		fprintf(stderr, "Could not open '%s' service: %s (%d)\n",
		        _service_name, get_errno_name(rc), rc);

		CloseServiceHandle(service_control_manager);

		return -1;
	}

	// get service status
	if (!QueryServiceStatus(service, &service_status)) {
		rc = ERRNO_WINAPI_OFFSET + GetLastError();

		fprintf(stderr, "Could not query status of '%s' service: %s (%d)\n",
		        _service_name, get_errno_name(rc), rc);

		CloseServiceHandle(service);
		CloseServiceHandle(service_control_manager);

		return -1;
	}

	// stop service
	if (service_status.dwCurrentState != SERVICE_STOPPED) {
		if (!ControlService(service, SERVICE_CONTROL_STOP, &service_status)) {
			rc = ERRNO_WINAPI_OFFSET + GetLastError();

			fprintf(stderr, "Could not send stop control code to '%s' service: %s (%d)\n",
			        _service_name, get_errno_name(rc), rc);

			CloseServiceHandle(service);
			CloseServiceHandle(service_control_manager);

			return -1;
		}

		while (service_status.dwCurrentState != SERVICE_STOPPED && tries < 60) {
			if (!QueryServiceStatus(service, &service_status)) {
				rc = ERRNO_WINAPI_OFFSET + GetLastError();

				fprintf(stderr, "Could not query status of '%s' service: %s (%d)\n",
				        _service_name, get_errno_name(rc), rc);

				CloseServiceHandle(service);
				CloseServiceHandle(service_control_manager);

				return -1;
			}

			Sleep(500);

			++tries;
		}

		if (service_status.dwCurrentState != SERVICE_STOPPED) {
			fprintf(stderr, "Could not stop '%s' service after 30 seconds\n",
			        _service_name);

			CloseServiceHandle(service);
			CloseServiceHandle(service_control_manager);

			return -1;
		}

		printf("Stopped '%s' service\n", _service_name);
	}

	// uninstall service
	if (!DeleteService(service)) {
		rc = ERRNO_WINAPI_OFFSET + GetLastError();

		fprintf(stderr, "Could not uninstall '%s' service: %s (%d)\n",
		        _service_name, get_errno_name(rc), rc);

		CloseServiceHandle(service);
		CloseServiceHandle(service_control_manager);

		return -1;
	}

	printf("Uninstalled '%s' service\n", _service_name);

	CloseServiceHandle(service);
	CloseServiceHandle(service_control_manager);

	return 0;
}

static void print_usage(const char *binary) {
	printf("Usage: %s [--help|--version|--install|--uninstall] [--debug]\n", binary);
}

int main(int argc, char **argv) {
	int i;
	int help = 0;
	int version = 0;
	int install = 0;
	int uninstall = 0;
	int debug = 0;

	for (i = 1; i < argc; ++i) {
		if (strcmp(argv[i], "--help") == 0) {
			help = 1;
		} else if (strcmp(argv[i], "--version") == 0) {
			version = 1;
		} else if (strcmp(argv[i], "--install") == 0) {
			install = 1;
		} else if (strcmp(argv[i], "--uninstall") == 0) {
			uninstall = 1;
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

	if (install && uninstall) {
		fprintf(stderr, "Invalid option combination\n");
		print_usage(argv[0]);

		return EXIT_FAILURE;
	}

	if (install) {
		if (service_install(debug) < 0) {
			return EXIT_FAILURE;
		}
	} else if (uninstall) {
		if (service_uninstall() < 0) {
			return EXIT_FAILURE;
		}
	} else {
		log_init();

		service_run();
	}

	return EXIT_SUCCESS;
}
