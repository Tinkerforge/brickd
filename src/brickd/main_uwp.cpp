/*
 * brickd
 * Copyright (C) 2016-2017 Matthias Bolte <matthias@tinkerforge.com>
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

extern "C" {

#include <daemonlib\config.h>
#include <daemonlib\event.h>
#include <daemonlib\file.h>
#include <daemonlib\pipe.h>
#include <daemonlib\socket.h>
#include <daemonlib\utils.h>

#include "hardware.h"
#include "network.h"
#include "usb.h"
#include "mesh.h"
#include "version.h"

}

using namespace concurrency;
using namespace Platform;
using namespace Windows::ApplicationModel;
using namespace Windows::ApplicationModel::Background;
using namespace Windows::Foundation;
using namespace Windows::Storage;
using namespace Windows::Storage::Search;

extern "C" void LIBUSB_CALL usbi_init(void);

#define CONFIG_FILENAME (ApplicationData::Current->LocalFolder->Path + "\\brickd.ini")
#define LOG_FILENAME (ApplicationData::Current->LocalFolder->Path + "\\brickd.log")

static LogSource _log_source = LOG_SOURCE_INITIALIZER;

static Pipe _cancellation_pipe;

static void forward_cancellation(void *opaque) {
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

static void handle_event_cleanup(void) {
	network_cleanup_clients_and_zombies();
	mesh_cleanup_stacks();
}

namespace brickd_uwp {
	[Windows::Foundation::Metadata::WebHostHidden]
	public ref class StartupTask sealed : public IBackgroundTask {
	public:
		virtual void Run(IBackgroundTaskInstance ^taskInstance);
	};
}

void brickd_uwp::StartupTask::Run(IBackgroundTaskInstance ^taskInstance) {
	int phase = 0;
	char config_filename[1024];
	char buffer[1024];
	char log_filename[1024];
	char *log_filename_ptr = log_filename;
	int rc;
	File log_file;
	WSADATA wsa_data;
	IStorageQueryResultBase ^query;
	File *log_file_ptr = &log_file;

	fixes_init();

	usbi_init();

	if (WideCharToMultiByte(CP_UTF8, 0, CONFIG_FILENAME->Data(), -1, config_filename,
	                        sizeof(config_filename), NULL, NULL) == 0) {
		rc = ERRNO_WINAPI_OFFSET + GetLastError();

		snprintf(buffer, sizeof(buffer),
		         "Could not convert config filename to UTF-8: %s (%d)\n",
		         get_errno_name(rc), rc);

		OutputDebugStringA(buffer);

		return;
	}

	config_init(config_filename);

	phase = 1;

	if (config_has_error()) {
		snprintf(buffer, sizeof(buffer),
		         "Error(s) occurred while reading config file '%s'\n",
		         config_filename);

		OutputDebugStringA(buffer);

		goto cleanup;
	}

	log_init();

	if (WideCharToMultiByte(CP_UTF8, 0, LOG_FILENAME->Data(), -1, log_filename,
	                        sizeof(log_filename), NULL, NULL) == 0) {
		rc = ERRNO_WINAPI_OFFSET + GetLastError();

		log_warn("Could not convert log filename to UTF-8: %s (%d)",
		         get_errno_name(rc), rc);
	} else {
		if (file_create(&log_file, log_filename,
		                O_CREAT | O_WRONLY | O_APPEND | O_BINARY,
		                S_IREAD | S_IWRITE) < 0) {
			log_warn("Could not open log file '%s': %s (%d)",
			         log_filename, get_errno_name(errno), errno);
		} else {
			log_set_output(&log_file.base);
		}
	}

	log_info("Brick Daemon %s started", VERSION_STRING);

	if (config_has_warning()) {
		log_warn("Warning(s) in config file '%s'", config_filename);
	}

	phase = 2;

	// initialize WinSock2
	if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
		rc = ERRNO_WINAPI_OFFSET + WSAGetLastError();

		log_error("Could not initialize Windows Sockets 2.2: %s (%d)",
		          get_errno_name(rc), rc);

		goto cleanup;
	}

	if (event_init() < 0) {
		goto cleanup;
	}

	phase = 3;
	query = ApplicationData::Current->LocalFolder->CreateFileQuery();

	query->ContentsChanged += ref new TypedEventHandler<IStorageQueryResultBase ^, Object ^>(
	[log_filename_ptr, log_file_ptr](IStorageQueryResultBase ^sender, Object ^args) {
		create_task(ApplicationData::Current->LocalFolder->GetFilesAsync())
		.then([log_filename_ptr, log_file_ptr](Collections::IVectorView<StorageFile ^> ^files) {
			for (size_t i = 0; i < files->Size; ++i) {
				if (files->GetAt(i)->Name->Equals("open-log.action")) {
					create_task(files->GetAt(i)->DeleteAsync(StorageDeleteOption::PermanentDelete)).wait();

					log_info("Found open-log.action file");

					if (log_get_output() == &log_file_ptr->base) {
						log_info("Log file is already open");

						continue;
					}

					if (file_create(log_file_ptr, log_filename_ptr,
						            O_CREAT | O_WRONLY | O_APPEND | O_BINARY,
						            S_IREAD | S_IWRITE) < 0) {
						log_warn("Could not open log file '%s': %s (%d)",
						         log_filename_ptr, get_errno_name(errno), errno);

						continue;
					}

					log_set_output(&log_file_ptr->base);

					log_info("Opened log file '%s'", log_filename_ptr);
				} else if (files->GetAt(i)->Name->Equals("close-log.action")) {
					create_task(files->GetAt(i)->DeleteAsync(StorageDeleteOption::PermanentDelete)).wait();

					log_info("Found close-log.action file");

					if (log_get_output() != &log_file_ptr->base) {
						log_info("Log file is already closed");

						continue;
					}

					log_set_output(&log_stderr_output);

					file_destroy(log_file_ptr);

					log_info("Closed log file '%s'", log_filename_ptr);
				}
			}
		}).wait();
	});

	query->GetItemCountAsync();

	if (hardware_init() < 0) {
		goto cleanup;
	}

	phase = 4;

	if (usb_init() < 0) {
		goto cleanup;
	}

	phase = 5;

	if (pipe_create(&_cancellation_pipe, 0) < 0) {
		log_error("Could not create cancellation pipe: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 6;

	if (event_add_source(_cancellation_pipe.read_end, EVENT_SOURCE_TYPE_GENERIC,
	                     EVENT_READ, forward_cancellation, nullptr) < 0) {
		goto cleanup;
	}

	phase = 7;

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

	phase = 8;

	if (mesh_init() < 0) {
		goto cleanup;
	}

	phase = 9;

	if (event_run(handle_event_cleanup) < 0) {
		goto cleanup;
	}

cleanup:
	switch (phase) { // no breaks, all cases fall through intentionally
	case 9:
		mesh_exit();

	case 8:
		network_exit();

	case 7:
		event_remove_source(_cancellation_pipe.read_end, EVENT_SOURCE_TYPE_GENERIC);

	case 6:
		pipe_destroy(&_cancellation_pipe);

	case 5:
		usb_exit();

	case 4:
		hardware_exit();

	case 3:
		event_exit();

	case 2:
		log_info("Brick Daemon %s stopped", VERSION_STRING);
		log_exit();

	case 1:
		config_exit();

	default:
		break;
	}
}
