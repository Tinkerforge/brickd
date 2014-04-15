/*
 * brickd
 * Copyright (C) 2012, 2014 Matthias Bolte <matthias@tinkerforge.com>
 *
 * log_winapi.c: Windows Event Log handling
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

#ifndef _MSC_VER
	#include <sys/time.h>
#endif
#include <windows.h>

#include "log.h"

#include "log_messages.h"
#include "threads.h"
#include "utils.h"

#define LOG_CATEGORY LOG_CATEGORY_OTHER

#include "packed_begin.h"

typedef struct {
	uint16_t length;
	uint64_t timestamp; // in microseconds
	uint8_t level;
	uint8_t category;
	char file[256];
	int line;
	char function[256];
	char message[1024];
} LogPipeMessage;

#include "packed_end.h"

#define NAMED_PIPE_BUFFER_LENGTH (sizeof(LogPipeMessage) * 4)

int _log_debug_override_platform = 0;

static HANDLE _event_log = NULL;
static int _named_pipe_connected = 0;
static int _named_pipe_running = 0;
static HANDLE _named_pipe = INVALID_HANDLE_VALUE;
static Thread _named_pipe_thread;
static HANDLE _named_pipe_write_event = NULL;
static HANDLE _named_pipe_stop_event = NULL;

static void log_connect_named_pipe(void *opaque) {
	int phase = 0;
	HANDLE overlapped_event;
	HANDLE events[2];
	int rc;
	OVERLAPPED overlapped;
	Semaphore *handshake = opaque;
	uint8_t byte;

	// create connect/read event
	overlapped_event = CreateEvent(NULL, TRUE, FALSE, NULL);

	if (overlapped_event == NULL) {
		rc = ERRNO_WINAPI_OFFSET + GetLastError();

		// this will go to the logfile if it is enabled via --debug
		log_error("Could not create named pipe overlapped connect/read event: %s (%d)",
		          get_errno_name(rc), rc);

		goto cleanup;
	}

	phase = 1;

	// create stop event
	_named_pipe_stop_event = CreateEvent(NULL, TRUE, FALSE, NULL);

	if (_named_pipe_stop_event == NULL) {
		rc = ERRNO_WINAPI_OFFSET + GetLastError();

		// this will go to the logfile if it is enabled via --debug
		log_error("Could not create named pipe stop event: %s (%d)",
		          get_errno_name(rc), rc);

		goto cleanup;
	}

	phase = 2;

	// start loop
	_named_pipe_running = 1;
	semaphore_release(handshake);

	log_debug("Started named pipe connect thread");

	while (_named_pipe_running) {
		// connect named pipe
		memset(&overlapped, 0, sizeof(overlapped));
		overlapped.hEvent = overlapped_event;

		if (ConnectNamedPipe(_named_pipe, &overlapped)) {
			rc = ERRNO_WINAPI_OFFSET + GetLastError();

			// this will go to the logfile if it is enabled via --debug
			log_error("Could not connect named pipe: %s (%d)",
			          get_errno_name(rc), rc);

			goto cleanup;
		}

		rc = GetLastError();

		// wait for connect/stop event
		switch (rc)  {
		case ERROR_IO_PENDING: // connection in progress
			events[0] = _named_pipe_stop_event;
			events[1] = overlapped_event;

			rc = WaitForMultipleObjects(2, events, FALSE, INFINITE);

			if (rc == WAIT_OBJECT_0) {
				log_debug("Stopped named pipe connect thread");

				goto cleanup;
			} else if (rc == WAIT_OBJECT_0 + 1) {
				_named_pipe_connected = 1;
				_log_debug_override_platform = 1;

				log_info("Log Viewer connected");
			} else {
				rc = ERRNO_WINAPI_OFFSET + GetLastError();

				// this will go to the logfile if it is enabled via --debug
				log_error("Could not wait for connect/stop event: %s (%d)",
				          get_errno_name(rc), rc);

				goto cleanup;
			}

			break;

		case ERROR_PIPE_CONNECTED: // already connected
			break;

		case ERROR_NO_DATA: // last connection was not properly closed, retry
			continue;

		default:
			rc += ERRNO_WINAPI_OFFSET;

			// this will go to the logfile if it is enabled via --debug
			log_error("Could not connect named pipe: %s (%d)",
			          get_errno_name(rc), rc);

			goto cleanup;
		}

		// read from named pipe to detect client disconnect
		for (;;) {
			ResetEvent(overlapped_event);

			memset(&overlapped, 0, sizeof(overlapped));
			overlapped.hEvent = overlapped_event;

			if (ReadFile(_named_pipe, &byte, 1, NULL, &overlapped)) {
				continue;
			}

			if (GetLastError() != ERROR_IO_PENDING) {
				rc = GetLastError();

				DisconnectNamedPipe(_named_pipe);

				log_info("Log Viewer disconnected");

				_named_pipe_connected = 0;
				_log_debug_override_platform = 0;

				break;
			}

			events[0] = _named_pipe_stop_event;
			events[1] = overlapped_event;

			rc = WaitForMultipleObjects(2, events, FALSE, INFINITE);

			if (rc == WAIT_OBJECT_0) {
				log_debug("Stopped named pipe connect thread");

				goto cleanup;
			} else if (rc == WAIT_OBJECT_0 + 1) {
				DisconnectNamedPipe(_named_pipe);

				log_info("Log Viewer disconnected");

				_named_pipe_connected = 0;
				_log_debug_override_platform = 0;

				break;
			} else {
				rc = ERRNO_WINAPI_OFFSET + GetLastError();

				// this will go to the logfile if it is enabled via --debug
				log_error("Could not wait for connect/stop event: %s (%d)",
				          get_errno_name(rc), rc);

				goto cleanup;
			}
		}
	}

cleanup:
	if (!_named_pipe_running) {
		// need to release the handshake in all cases, otherwise
		// log_init_platform will block forever in semaphore_acquire
		semaphore_release(handshake);
	}

	switch (phase) { // no breaks, all cases fall through intentionally
	case 2:
		CloseHandle(_named_pipe_stop_event);

	case 1:
		CloseHandle(overlapped_event);

	default:
		break;
	}

	_named_pipe_running = 0;
}

void log_init_platform(void) {
	int rc;
	Semaphore handshake;

	// open event log
	_event_log = RegisterEventSource(NULL, "Brick Daemon");

	if (_event_log == NULL) {
		rc = ERRNO_WINAPI_OFFSET + GetLastError();

		// this will go to the logfile if it is enabled via --debug
		log_error("Could not open Windows event log: %s (%d)",
		          get_errno_name(rc), rc);
	}

	// create named pipe for log messages
	_named_pipe_write_event = CreateEvent(NULL, TRUE, FALSE, NULL);

	if (_named_pipe_write_event == NULL) {
		rc = ERRNO_WINAPI_OFFSET + GetLastError();

		// this will go to the logfile if it is enabled via --debug
		log_error("Could not create named pipe overlapped write event: %s (%d)",
		          get_errno_name(rc), rc);
	} else {
		_named_pipe = CreateNamedPipe("\\\\.\\pipe\\tinkerforge-brick-daemon-debug-log",
		                              PIPE_ACCESS_DUPLEX |
		                              FILE_FLAG_OVERLAPPED |
		                              FILE_FLAG_FIRST_PIPE_INSTANCE,
		                              PIPE_TYPE_MESSAGE |
		                              PIPE_WAIT,
		                              1,
		                              NAMED_PIPE_BUFFER_LENGTH,
		                              NAMED_PIPE_BUFFER_LENGTH,
		                              0,
		                              NULL);

		if (_named_pipe == INVALID_HANDLE_VALUE) {
			rc = ERRNO_WINAPI_OFFSET + GetLastError();

			// ERROR_PIPE_BUSY/ERROR_ACCESS_DENIED means pipe already exists
			if (rc == ERRNO_WINAPI_OFFSET + ERROR_PIPE_BUSY ||
			    rc == ERRNO_WINAPI_OFFSET + ERROR_ACCESS_DENIED) {
				// this will go to the logfile if it is enabled via --debug
				log_debug("Could not create named pipe: %s (%d)",
				          get_errno_name(rc), rc);
			} else {
				// this will go to the logfile if it is enabled via --debug
				log_error("Could not create named pipe: %s (%d)",
				          get_errno_name(rc), rc);
			}
		} else {
			// create named pipe connect thread
			if (semaphore_create(&handshake) < 0) {
				rc = ERRNO_WINAPI_OFFSET + GetLastError();

				// this will go to the logfile if it is enabled via --debug
				log_error("Could not create handshake semaphore: %s (%d)",
				          get_errno_name(rc), rc);
			} else {
				thread_create(&_named_pipe_thread, log_connect_named_pipe, &handshake);

				semaphore_acquire(&handshake);
				semaphore_destroy(&handshake);
			}
		}
	}
}

void log_exit_platform(void) {
	if (_named_pipe_running) {
		SetEvent(_named_pipe_stop_event);

		thread_join(&_named_pipe_thread);
		thread_destroy(&_named_pipe_thread);
	}

	_named_pipe_connected = 0;
	_log_debug_override_platform = 0;

	if (_named_pipe != INVALID_HANDLE_VALUE) {
		CloseHandle(_named_pipe);
	}

	if (_named_pipe_stop_event != NULL) {
		CloseHandle(_named_pipe_stop_event);
	}

	if (_named_pipe_write_event != NULL) {
		CloseHandle(_named_pipe_write_event);
	}

	if (_event_log != NULL) {
		DeregisterEventSource(_event_log);
	}
}

// NOTE: assumes that _mutex (in log.c) is locked
void log_handler_platform(struct timeval *timestamp,
                          LogCategory category, LogLevel level,
                          const char *file, int line,
                          const char *function, const char *format,
                          va_list arguments) {
	WORD type = 0;
	DWORD event_id = 0;
	LPCSTR insert_strings[1] = {NULL};
	LogPipeMessage pipe_message;
	OVERLAPPED overlapped;
	DWORD bytes_written;

	if (_event_log == NULL && !_named_pipe_connected) {
		return;
	}

	switch (level) {
	case LOG_LEVEL_ERROR:
		type = EVENTLOG_ERROR_TYPE;
		event_id = BRICKD_GENERIC_ERROR;
		insert_strings[0] = pipe_message.message;
		break;

	case LOG_LEVEL_WARN:
		type = EVENTLOG_WARNING_TYPE;
		event_id = BRICKD_GENERIC_WARNING;
		insert_strings[0] = pipe_message.message;
		break;

	default:
		// ignore all other log levels for the event log
		insert_strings[0] = NULL;
		break;
	}

#ifdef _MSC_VER
	_vsnprintf_s(pipe_message.message, sizeof(pipe_message.message),
	             sizeof(pipe_message.message) - 1, format, arguments);
#else
	vsnprintf(pipe_message.message, sizeof(pipe_message.message), format, arguments);
#endif

	if (_event_log != NULL && insert_strings[0] != NULL) {
		ReportEvent(_event_log, type, 0, event_id, NULL, 1, 0, insert_strings, NULL);
	}

	if (_named_pipe_connected) {
		pipe_message.length = sizeof(pipe_message);
		pipe_message.timestamp = (uint64_t)timestamp->tv_sec * 1000000 + timestamp->tv_usec;
		pipe_message.level = level;
		pipe_message.category = category;

		strncpy(pipe_message.file, file, sizeof(pipe_message.file));
		pipe_message.file[sizeof(pipe_message.file) - 1] = '\0';

		pipe_message.line = line;

		strncpy(pipe_message.function, function, sizeof(pipe_message.function));
		pipe_message.function[sizeof(pipe_message.function) - 1] = '\0';

		memset(&overlapped, 0, sizeof(overlapped));
		overlapped.hEvent = _named_pipe_write_event;

		if (!WriteFile(_named_pipe, &pipe_message, sizeof(pipe_message), NULL, &overlapped) &&
		    GetLastError() == ERROR_IO_PENDING) {
			// wait for result of overlapped I/O to avoid a race condition with
			// the next WriteFile call that will reuse the same event handle
			GetOverlappedResult(_named_pipe, &overlapped, &bytes_written, TRUE);
		}
	}
}
