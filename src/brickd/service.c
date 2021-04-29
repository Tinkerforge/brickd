/*
 * brickd
 * Copyright (C) 2012-2014, 2016, 2018-2019, 2021 Matthias Bolte <matthias@tinkerforge.com>
 *
 * service.c: Windows service specific functions
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

#include <stdio.h>
#include <windows.h>

#include <daemonlib/log.h>
#include <daemonlib/utils.h>

#include "service.h"

static LogSource _log_source = LOG_SOURCE_INITIALIZER;

static char *_service_name = "Brick Daemon";
static char *_service_description = "Brick Daemon is a bridge between USB devices (Bricks) and TCP/IP sockets. It can be used to read out and control Bricks.";
static SERVICE_STATUS _service_status;

// has to be initialized here, to cover the service_get_status_handle
// call in case of not running as service
static SERVICE_STATUS_HANDLE _service_status_handle = NULL;

int service_init(LPHANDLER_FUNCTION_EX handler) {
	int rc;

	_service_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
	_service_status.dwCurrentState = SERVICE_STOPPED;
	_service_status.dwControlsAccepted = 0;
	_service_status.dwWin32ExitCode = NO_ERROR;
	_service_status.dwServiceSpecificExitCode = NO_ERROR;
	_service_status.dwCheckPoint = 0;
	_service_status.dwWaitHint = 0;

	_service_status_handle = RegisterServiceCtrlHandlerExA(_service_name,
	                                                       handler, NULL);

	if (_service_status_handle == NULL) {
		rc = ERRNO_WINAPI_OFFSET + GetLastError();

		log_error("Could not register service control handler: %s (%d)",
		          get_errno_name(rc), rc);

		return -1;
	}

	return 0;
}

void service_exit(void) {
	_service_status_handle = NULL;
}

int service_is_running(void) {
	SC_HANDLE service_control_manager;
	int rc;
	SC_HANDLE service;
	SERVICE_STATUS service_status;

	// open service control manager
	service_control_manager = OpenSCManagerA(NULL, NULL, SC_MANAGER_CONNECT);

	if (service_control_manager == NULL) {
		rc = ERRNO_WINAPI_OFFSET + GetLastError();

		log_error("Could not open service control manager: %s (%d)",
		          get_errno_name(rc), rc);

		return -1;
	}

	// open service
	service = OpenServiceA(service_control_manager, _service_name,
	                       SERVICE_QUERY_STATUS);

	if (service == NULL) {
		rc = GetLastError();

		if (rc == ERROR_SERVICE_DOES_NOT_EXIST) {
			CloseServiceHandle(service_control_manager);

			return 0;
		}

		rc += ERRNO_WINAPI_OFFSET;

		log_error("Could not open '%s' service: %s (%d)",
		          _service_name, get_errno_name(rc), rc);

		CloseServiceHandle(service_control_manager);

		return -1;
	}

	// get service status
	if (!QueryServiceStatus(service, &service_status)) {
		rc = ERRNO_WINAPI_OFFSET + GetLastError();

		log_error("Could not query status of '%s' service: %s (%d)",
		          _service_name, get_errno_name(rc), rc);

		CloseServiceHandle(service);
		CloseServiceHandle(service_control_manager);

		return -1;
	}

	CloseServiceHandle(service);
	CloseServiceHandle(service_control_manager);

	return service_status.dwCurrentState != SERVICE_STOPPED ? 1 : 0;
}

SERVICE_STATUS_HANDLE service_get_status_handle(void) {
	return _service_status_handle;
}

void service_set_status(DWORD status, DWORD exit_code) {
	_service_status.dwCurrentState = status;
	_service_status.dwWin32ExitCode = exit_code;

	if (status == SERVICE_RUNNING) {
		_service_status.dwControlsAccepted |= SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
	} else if (status == SERVICE_STOPPED) {
		_service_status.dwControlsAccepted &= ~(SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN);
	}

	SetServiceStatus(_service_status_handle, &_service_status);
}

char *service_get_name(void) {
	return _service_name;
}

int service_install(const char *debug_filter) {
	SC_HANDLE service_control_manager;
	int rc;
	char filename[1024];
	char quoted_filename[1024];
	SC_HANDLE service;
	SERVICE_DESCRIPTIONA description;
	const char *buffer[2];
	DWORD argc = 0;
	const char **argv = NULL;

	if (debug_filter != NULL) {
		buffer[argc++] = "--debug";

		if (*debug_filter != '\0') {
			buffer[argc++] = debug_filter;
		}

		argv = buffer;
	}

	if (GetModuleFileNameA(NULL, filename, sizeof(filename)) == 0) {
		rc = ERRNO_WINAPI_OFFSET + GetLastError();

		fprintf(stderr, "Could not get module file name: %s (%d)\n",
		        get_errno_name(rc), rc);

		return -1;
	}

	// open service control manager
	service_control_manager = OpenSCManagerA(NULL, NULL, SC_MANAGER_CREATE_SERVICE);

	if (service_control_manager == NULL) {
		rc = ERRNO_WINAPI_OFFSET + GetLastError();

		fprintf(stderr, "Could not open service control manager: %s (%d)\n",
		        get_errno_name(rc), rc);

		return -1;
	}

	// install service
	snprintf(quoted_filename, sizeof(quoted_filename), "\"%s\"", filename);

	service = CreateServiceA(service_control_manager, _service_name, _service_name,
	                         SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS,
	                         SERVICE_AUTO_START, SERVICE_ERROR_NORMAL, quoted_filename,
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

			service = OpenServiceA(service_control_manager, _service_name,
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

	if (!ChangeServiceConfig2A(service, SERVICE_CONFIG_DESCRIPTION,
	                           &description)) {
		rc = ERRNO_WINAPI_OFFSET + GetLastError();

		fprintf(stderr, "Could not update description of '%s' service: %s (%d)\n",
		        _service_name, get_errno_name(rc), rc);

		CloseServiceHandle(service);
		CloseServiceHandle(service_control_manager);

		return -1;
	}

	// start service
	if (!StartServiceA(service, argc, argv)) {
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
		// FIXME: query status and wait until service is really started

		if (debug_filter != NULL) {
			printf("Started '%s' service with --debug option\n", _service_name);
		} else {
			printf("Started '%s' service\n", _service_name);
		}
	}

	CloseServiceHandle(service);
	CloseServiceHandle(service_control_manager);

	return 0;
}

int service_uninstall(void) {
	SC_HANDLE service_control_manager;
	int rc;
	SC_HANDLE service;
	SERVICE_STATUS service_status;
	int tries = 0;

	// open service control manager
	service_control_manager = OpenSCManagerA(NULL, NULL, SC_MANAGER_CONNECT);

	if (service_control_manager == NULL) {
		rc = ERRNO_WINAPI_OFFSET + GetLastError();

		fprintf(stderr, "Could not open service control manager: %s (%d)\n",
		        get_errno_name(rc), rc);

		return -1;
	}

	// open service
	service = OpenServiceA(service_control_manager, _service_name,
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

			millisleep(500);

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
