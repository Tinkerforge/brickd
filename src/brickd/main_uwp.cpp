/*
 * brickd
 * Copyright (C) 2016-2018 Matthias Bolte <matthias@tinkerforge.com>
 * Copyright (C) 2016-2017 Ishraq Ibne Ashraf <ishraq@tinkerforge.com>
 *
 * main_uwp.cpp: Brick Daemon starting point for Universal Windows Platform
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

#include <fcntl.h>
#include <winsock2.h>
#include <ppltasks.h>
#include <agile.h>

extern "C" {

#include <daemonlib\config.h>
#include <daemonlib\event.h>
#include <daemonlib\file.h>
#include <daemonlib\pipe.h>
#include <daemonlib\socket.h>
#include <daemonlib\utils.h>
#include <daemonlib\utils_uwp.h>

#include "app_service.h"
#include "hardware.h"
#include "network.h"
#include "usb.h"
#include "mesh.h"
#include "version.h"
#ifdef BRICKD_WITH_BRICKLET
	#include "bricklet.h"
#endif

}

using namespace concurrency;
using namespace Platform;
using namespace Windows::ApplicationModel;
using namespace Windows::ApplicationModel::AppService;
using namespace Windows::ApplicationModel::Background;

//#define LOG_SERVER_ADDRESS "192.168.178.52"
//#define LOG_SERVER_PORT 11111

typedef struct {
	char *caller;
	Agile<BackgroundTaskDeferral ^> deferral;
	Agile<AppServiceConnection ^> connection;
} AppServiceAccept;

extern "C" void LIBUSB_CALL usbi_init(void);

static LogSource _log_source = LOG_SOURCE_INITIALIZER;

static bool _main_running = false;
static Pipe _cancellation_pipe;
static Pipe _app_service_accept_pipe;

static void debugf(const char *format, ...) {
	va_list arguments;
	char buffer[1024];

	va_start(arguments, format);
	vsnprintf(buffer, sizeof(buffer), format, arguments);
	va_end(arguments);

	OutputDebugStringA(buffer);
}

static void handle_cancellation(void *opaque) {
	int value;
	const char *reason = "<unknown>";

	(void)opaque;

	if (pipe_read(&_cancellation_pipe, &value, sizeof(value)) < 0) {
		log_error("Could not read from cancellation pipe: %s (%d)",
		          get_errno_name(errno), errno);

		return;
	}

	switch ((BackgroundTaskCancellationReason)value) {
	case BackgroundTaskCancellationReason::Abort:
		reason = "abort";
		break;

	case BackgroundTaskCancellationReason::ConditionLoss:
		reason = "condition-lost";
		break;

	case BackgroundTaskCancellationReason::EnergySaver:
		reason = "energy-saver";
		break;

	case BackgroundTaskCancellationReason::ExecutionTimeExceeded:
		reason = "execution-time-exceeded";
		break;

	case BackgroundTaskCancellationReason::IdleTask:
		reason = "idle-task";
		break;

	case BackgroundTaskCancellationReason::LoggingOff:
		reason = "logging-off";
		break;

	case BackgroundTaskCancellationReason::ResourceRevocation:
		reason = "resource-revocation";
		break;

	case BackgroundTaskCancellationReason::ServicingUpdate:
		reason = "servicing-update";
		break;

	case BackgroundTaskCancellationReason::SystemPolicy:
		reason = "system-policy";
		break;

	case BackgroundTaskCancellationReason::Terminating:
		reason = "terminating";
		break;

	case BackgroundTaskCancellationReason::Uninstall:
		reason = "uninstall";
		break;

	default:
		break;
	}

	log_info("Got cancelled (reason: %s)", reason);

	event_stop();
}

static void handle_app_service_accept(void *opaque) {
	int phase = 0;
	AppServiceAccept *accept;
	AppService_ *app_service;

	(void)opaque;

	if (pipe_read(&_app_service_accept_pipe, &accept, sizeof(accept)) < 0) {
		log_error("Could not read from AppService accept pipe: %s (%d)",
		          get_errno_name(errno), errno);

		return;
	}

	phase = 1;

	// create new AppService
	app_service = (AppService_ *)calloc(1, sizeof(AppService_));

	if (app_service == nullptr) {
		log_error("Could not allocate AppService (caller: %s): %s (%d)",
		          accept->caller, get_errno_name(ENOMEM), ENOMEM);

		goto error;
	}

	phase = 2;

	if (app_service_create(app_service, accept->caller,
	                       accept->deferral, accept->connection) < 0) {
		log_error("Could not create AppService (caller: %s): %s (%d)",
		          accept->caller, get_errno_name(errno), errno);

		goto error;
	}

	phase = 3;

	// create new client
	if (network_create_client(accept->caller, &app_service->base) == NULL) {
		goto error;
	}

	free(accept->caller);
	free(accept);

	return;

error:
	accept->deferral->Complete();

	switch (phase) { // no breaks, all cases fall through intentionally
	case 3:
		app_service_destroy(app_service);
		// fall through

	case 2:
		free(app_service);
		// fall through

	case 1:
		free(accept->caller);
		free(accept);
		// fall through

	default:
		break;
	}
}

// NOTE: assumes that main is running
static void accept_app_service(IBackgroundTaskInstance ^task_instance) {
	AppServiceTriggerDetails ^trigger_details;
	char *caller;
	BackgroundTaskDeferral ^deferral;
	AppServiceConnection ^connection;
	AppServiceAccept *accept;

	trigger_details = dynamic_cast<AppServiceTriggerDetails ^>(task_instance->TriggerDetails);

	if (trigger_details == nullptr) {
		return;
	}

	caller = string_convert_ascii(trigger_details->CallerPackageFamilyName);

	if (caller == nullptr) {
		log_error("Could not convert AppService caller name: %s (%d)",
		          get_errno_name(errno), errno);

		return;
	}

	log_info("Accepting AppService (caller: %s)", caller);

	deferral = task_instance->GetDeferral();
	connection = trigger_details->AppServiceConnection;
	accept = (AppServiceAccept *)calloc(1, sizeof(AppServiceAccept));

	if (accept == nullptr) {
		log_error("Could not allocate AppService (caller: %s) accept: %s (%d)",
		          caller, get_errno_name(ENOMEM), ENOMEM);

		deferral->Complete();
		free(caller);

		return;
	}

	accept->caller = caller;
	accept->deferral = Agile<BackgroundTaskDeferral ^>(deferral);
	accept->connection = Agile<AppServiceConnection ^>(connection);

	if (pipe_write(&_app_service_accept_pipe, &accept, sizeof(accept)) < 0) {
		log_error("Could not write to AppService accept pipe: %s (%d)",
		          get_errno_name(errno), errno);

		deferral->Complete();
		free(caller);
		free(accept);

		return;
	}
}

static void handle_event_cleanup(void) {
	network_cleanup_clients_and_zombies();
	mesh_cleanup_stacks();
}

namespace brickd_uwp {
	[Windows::Foundation::Metadata::WebHostHidden]
	public ref class MainTask sealed : public IBackgroundTask {
	public:
		virtual void Run(IBackgroundTaskInstance ^taskInstance);
	};
}

void brickd_uwp::MainTask::Run(IBackgroundTaskInstance ^taskInstance) {
	int phase = 0;
	int rc;
	WSADATA wsa_data;
#ifdef LOG_SERVER_ADDRESS
	Socket log_socket;
	struct addrinfo *resolved_address = nullptr;
#endif

	if (_main_running) {
		accept_app_service(taskInstance);

		return;
	}

	_main_running = true;

	fixes_init();

	usbi_init();

	config_init(nullptr);

	phase = 1;

#if false // FIXME: config cannot have errors, because not config file is loaded
	if (config_has_error()) {
		debugf("Error(s) occurred while reading config file '%s'\n",
		       config_filename);

		goto cleanup;
	}
#endif

	if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
		rc = ERRNO_WINAPI_OFFSET + WSAGetLastError();

		debugf("Could not initialize Windows Sockets 2.2: %s (%d)\n",
		       get_errno_name(rc), rc);

		goto cleanup;
	}

#ifdef LOG_SERVER_ADDRESS
	if (socket_create(&log_socket) < 0) {
		debugf("Could not create log socket: %s (%d)\n", get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 2;
	resolved_address = socket_hostname_to_address(LOG_SERVER_ADDRESS, LOG_SERVER_PORT);

	if (resolved_address == NULL) {
		debugf("Could not resolve log server address '%s' (port: %u): %s (%d)\n",
		       LOG_SERVER_ADDRESS, LOG_SERVER_PORT, get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 3;

	if (socket_open(&log_socket, resolved_address->ai_family,
		            resolved_address->ai_socktype, resolved_address->ai_protocol) < 0) {
		debugf("Could not open log server socket: %s (%d)\n",
		       get_errno_name(errno), errno);

		goto cleanup;
	}

	if (socket_connect(&log_socket, resolved_address->ai_addr,
		               resolved_address->ai_addrlen) < 0) {
		debugf("Could not connect to log server '%s' on port %u: %s (%d)\n",
		       LOG_SERVER_ADDRESS, LOG_SERVER_PORT, get_errno_name(errno), errno);

		goto cleanup;
	}

	log_init();
	log_set_output(&log_socket.base);
#else
	log_init();
#endif

	phase = 4;

	log_info("Brick Daemon %s started", VERSION_STRING);

#if false // FIXME: config cannot have warnings, because not config file is loaded
	if (config_has_warning()) {
		log_warn("Warning(s) in config file '%s'", config_filename);
	}
#endif

	if (event_init() < 0) {
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

	if (pipe_create(&_cancellation_pipe, 0) < 0) {
		log_error("Could not create cancellation pipe: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 8;

	if (event_add_source(_cancellation_pipe.base.read_handle, EVENT_SOURCE_TYPE_GENERIC,
	                     "cancellation", EVENT_READ, handle_cancellation, nullptr) < 0) {
		goto cleanup;
	}

	phase = 9;

	taskInstance->Canceled += ref new BackgroundTaskCanceledEventHandler(
	[](IBackgroundTaskInstance ^sender, BackgroundTaskCancellationReason reason) {
		int value = (int)reason;

		if (pipe_write(&_cancellation_pipe, &value, sizeof(value)) < 0) {
			log_error("Could not write to cancellation pipe: %s (%d)",
			          get_errno_name(errno), errno);
		}
	});

	if (network_init() < 0) {
		goto cleanup;
	}

	phase = 10;

	if (mesh_init() < 0) {
		goto cleanup;
	}

	phase = 11;

	if (pipe_create(&_app_service_accept_pipe, 0) < 0) {
		log_error("Could not create AppService accept pipe: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 12;

	if (event_add_source(_app_service_accept_pipe.base.read_handle,
	                     EVENT_SOURCE_TYPE_GENERIC, "app-service-accept",
	                     EVENT_READ, handle_app_service_accept, nullptr) < 0) {
		goto cleanup;
	}

	phase = 13;

#ifdef BRICKD_WITH_BRICKLET
	if (bricklet_init() < 0) {
		goto cleanup;
	}

	phase = 14;
#endif

	accept_app_service(taskInstance);

	if (event_run(handle_event_cleanup) < 0) {
		goto cleanup;
	}

cleanup:
	switch (phase) { // no breaks, all cases fall through intentionally
#ifdef BRICKD_WITH_BRICKLET
	case 14:
		bricklet_exit();
#endif
		// fall through

	case 13:
		event_remove_source(_app_service_accept_pipe.base.read_handle, EVENT_SOURCE_TYPE_GENERIC);
		// fall through

	case 12:
		pipe_destroy(&_app_service_accept_pipe);
		// fall through

	case 11:
		mesh_exit();
		// fall through

	case 10:
		network_exit();
		// fall through

	case 9:
		event_remove_source(_cancellation_pipe.base.read_handle, EVENT_SOURCE_TYPE_GENERIC);
		// fall through

	case 8:
		pipe_destroy(&_cancellation_pipe);
		// fall through

	case 7:
		usb_exit();
		// fall through

	case 6:
		hardware_exit();
		// fall through

	case 5:
		event_exit();
		// fall through

	case 4:
		log_info("Brick Daemon %s stopped", VERSION_STRING);
		log_exit();
		// fall through

#ifdef LOG_SERVER_ADDRESS
	case 3:
		freeaddrinfo(resolved_address);
		// fall through

	case 2:
		socket_destroy(&log_socket);
		// fall through
#endif

	case 1:
		config_exit();
		// fall through

	default:
		break;
	}

	_main_running = false;
}
