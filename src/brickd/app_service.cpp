/*
 * brickd
 * Copyright (C) 2017-2019 Matthias Bolte <matthias@tinkerforge.com>
 *
 * app_service.cpp: AppService based I/O device for Universal Windows Platform
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

#include <ppltasks.h>

extern "C" {

#include <daemonlib/event.h>
#include <daemonlib/io.h>
#include <daemonlib/log.h>
#include <daemonlib/utils.h>

}

using namespace concurrency;
using namespace Platform;
using namespace Windows::ApplicationModel::AppService;
using namespace Windows::ApplicationModel::Background;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;
using namespace Windows::Security::Cryptography;
using namespace Windows::Storage::Streams;

#include "app_service.h"

static LogSource _log_source = LOG_SOURCE_INITIALIZER;

static void app_service_forward_write(void *opaque) {
	AppService_ *app_service = (AppService_ *)opaque;
	uint8_t buffer[1024];
	int length;
	ValueSet ^value_set;
	Array<uint8_t> ^data;

	length = pipe_read(&app_service->write_pipe, buffer, sizeof(buffer));

	if (length < 0) {
		log_error("Could not read from AppService (caller: %s) write pipe: %s (%d)",
		          app_service->caller, get_errno_name(errno), errno);

		return;
	}

	value_set = ref new ValueSet();
	data = ref new Array<uint8_t>(length); // FIXME: how to handle length == 0?

	memcpy(data->Data, buffer, length);
	value_set->Insert("data", data);

	create_task(app_service->connection->SendMessageAsync(value_set)).wait(); // FIXME: look at response status?
}

static void app_service_handle_close(void *opaque) {
	AppService_ *app_service = (AppService_ *)opaque;
	int value;
	const char *status = "<unknown>";

	if (pipe_read(&app_service->close_pipe, &value, sizeof(value)) < 0) {
		log_error("Could not read from AppService (caller: %s) close pipe: %s (%d)",
		          app_service->caller, get_errno_name(errno), errno);
	} else {
		switch ((AppServiceClosedStatus)value) {
		case AppServiceClosedStatus::Canceled:
			status = "canceled";
			break;

		case AppServiceClosedStatus::Completed:
			status = "completed";
			break;

		case AppServiceClosedStatus::ResourceLimitsExceeded:
			status = "resource-limits-exceeded";
			break;

		case AppServiceClosedStatus::Unknown:
			status = "unknown";
			break;

		default:
			break;
		}
	}

	log_info("AppService (caller: %s) got closed (status: %s)",
	         app_service->caller, status);

	// make app_service_read return 0
	app_service->connection = nullptr;
	value = 0;

	if (pipe_write(&app_service->read_pipe, &value, sizeof(value)) < 0) {
		log_error("Could not write to AppService (caller: %s) read pipe: %s (%d)",
		          app_service->caller, get_errno_name(errno), errno);
	}

	// end task
	app_service->deferral->Complete();
	app_service->deferral = nullptr;
}

// sets errno on error
extern "C" int app_service_create(AppService_ *app_service, const char *caller,
                                  AgileBackgroundTaskDeferral deferral,
                                  AgileAppServiceConnection connection) {
	int phase = 0;
	int saved_errno;

	if (io_create(&app_service->base, "AppService",
	              (IODestroyFunction)app_service_destroy,
	              (IOReadFunction)app_service_read,
	              (IOWriteFunction)app_service_write,
	              NULL) < 0) {
		goto error;
	}

	if (pipe_create(&app_service->read_pipe, PIPE_FLAG_NON_BLOCKING_READ) < 0) {
		goto error;
	}

	phase = 1;

	if (pipe_create(&app_service->write_pipe, PIPE_FLAG_NON_BLOCKING_READ) < 0) {
		goto error;
	}

	phase = 2;

	if (event_add_source(app_service->write_pipe.base.read_handle,
	                     EVENT_SOURCE_TYPE_GENERIC, "app-service-write",
	                     EVENT_READ, app_service_forward_write, app_service) < 0) {
		goto error;
	}

	phase = 3;

	if (pipe_create(&app_service->close_pipe, PIPE_FLAG_NON_BLOCKING_READ) < 0) {
		goto error;
	}

	phase = 4;

	if (event_add_source(app_service->close_pipe.base.read_handle,
	                     EVENT_SOURCE_TYPE_GENERIC, "app-service-close",
	                     EVENT_READ, app_service_handle_close, app_service) < 0) {
		goto error;
	}

	phase = 5;

	app_service->base.read_handle = app_service->read_pipe.base.read_handle;
	app_service->base.write_handle = app_service->write_pipe.base.write_handle;

	string_copy(app_service->caller, sizeof(app_service->caller), caller, -1);
	app_service->deferral = deferral;
	app_service->connection = connection;

	connection->RequestReceived += ref new TypedEventHandler<AppServiceConnection ^, AppServiceRequestReceivedEventArgs ^>(
	[app_service](AppServiceConnection ^sender, AppServiceRequestReceivedEventArgs ^args) {
		IBoxArray<uint8_t> ^data = safe_cast<IBoxArray<uint8_t> ^>(args->Request->Message->Lookup("data"));

		if (pipe_write(&app_service->read_pipe, data->Value->Data, data->Value->Length) < 0) {
			log_error("Could not write to AppService (caller: %s) read pipe: %s (%d)",
			          app_service->caller, get_errno_name(errno), errno);
		}

		create_task(args->Request->SendResponseAsync(ref new ValueSet())).wait(); // FIXME: look at response status?
	});

	connection->ServiceClosed += ref new TypedEventHandler<AppServiceConnection ^, AppServiceClosedEventArgs ^>(
	[app_service](AppServiceConnection ^sender, AppServiceClosedEventArgs ^args) {
		int value = (int)args->Status;

		if (pipe_write(&app_service->close_pipe, &value, sizeof(value)) < 0) {
			log_error("Could not write to AppService (caller: %s) close pipe: %s (%d)",
			          app_service->caller, get_errno_name(errno), errno);
		}
	});

	// FIXME: send initial handshake message

	return 0;

error:
	saved_errno = errno;

	switch (phase) { // no breaks, all cases fall through intentionally
	case 5:
		event_remove_source(app_service->close_pipe.base.read_handle, EVENT_SOURCE_TYPE_GENERIC);
		// fall through

	case 4:
		pipe_destroy(&app_service->close_pipe);
		// fall through

	case 3:
		event_remove_source(app_service->write_pipe.base.read_handle, EVENT_SOURCE_TYPE_GENERIC);
		// fall through

	case 2:
		pipe_destroy(&app_service->write_pipe);
		// fall through

	case 1:
		pipe_destroy(&app_service->read_pipe);
		// fall through

	default:
		break;
	}

	errno = saved_errno;

	return -1;
}

extern "C" void app_service_destroy(AppService_ *app_service) {
	event_remove_source(app_service->close_pipe.base.read_handle, EVENT_SOURCE_TYPE_GENERIC);
	pipe_destroy(&app_service->close_pipe);

	event_remove_source(app_service->write_pipe.base.read_handle, EVENT_SOURCE_TYPE_GENERIC);
	pipe_destroy(&app_service->write_pipe);

	pipe_destroy(&app_service->read_pipe);

	if (app_service->deferral != nullptr) {
		app_service->deferral->Complete();
	}
}

// sets errno on error
extern "C" int app_service_read(AppService_ *app_service, void *buffer, int length) {
	if (app_service->connection == nullptr) {
		return 0;
	} else {
		return pipe_read(&app_service->read_pipe, buffer, length);
	}
}

// sets errno on error
extern "C" int app_service_write(AppService_ *app_service, const void *buffer, int length) {
	return pipe_write(&app_service->write_pipe, buffer, length);
}
