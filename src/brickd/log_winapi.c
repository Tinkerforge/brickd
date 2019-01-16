/*
 * brickd
 * Copyright (C) 2012-2019 Matthias Bolte <matthias@tinkerforge.com>
 *
 * log_winapi.c: Windows Event Log and log viewer handling
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

#include <io.h>
#include <stdbool.h>
#ifndef _MSC_VER
	#include <sys/time.h>
#endif
#include <windows.h>

#include <daemonlib/log.h>
#include <daemonlib/threads.h>
#include <daemonlib/utils.h>

#include "log_messages.h"

static LogSource _log_source = LOG_SOURCE_INITIALIZER;

enum { // bitmask
	LOG_PIPE_MESSAGE_FLAG_LIBUSB = 0x0001
};

#include <daemonlib/packed_begin.h>

typedef struct {
	uint16_t length;
	uint8_t flags;
	uint64_t timestamp; // in microseconds
	uint8_t level;
	char source[128];
	int line;
	char message[1024];
} ATTRIBUTE_PACKED LogPipeMessage;

#include <daemonlib/packed_end.h>

#define NAMED_PIPE_BUFFER_LENGTH (sizeof(LogPipeMessage) * 4)
#define FOREGROUND_ALL (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY)
#define BACKGROUND_ALL (BACKGROUND_RED | BACKGROUND_GREEN | BACKGROUND_BLUE | BACKGROUND_INTENSITY)
#define FOREGROUND_YELLOW (FOREGROUND_RED | FOREGROUND_GREEN)
#define FOREGROUND_WHITE (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE)

static IO *_output = NULL;
static HANDLE _console = NULL;
static WORD _default_attributes = 0;
static HANDLE _event_log = NULL;
static bool _named_pipe_connected = false;
static bool _named_pipe_running = false;
static HANDLE _named_pipe = INVALID_HANDLE_VALUE;
static Thread _named_pipe_thread;
static HANDLE _named_pipe_write_event = NULL;
static Mutex _named_pipe_write_event_mutex; // protects _named_pipe_write_event
static HANDLE _named_pipe_stop_event = NULL;

void log_set_output_platform(IO *output);
void log_apply_color_platform(LogLevel level, bool begin);

static WORD log_prepare_color_attributes(WORD color) {
	WORD attributes = _default_attributes;
	WORD background = BACKGROUND_INTENSITY;

	if ((color & FOREGROUND_RED) != 0) {
		background |= BACKGROUND_RED;
	}

	if ((color & FOREGROUND_GREEN) != 0) {
		background |= BACKGROUND_GREEN;
	}

	if ((color & FOREGROUND_BLUE) != 0) {
		background |= BACKGROUND_BLUE;
	}

	if ((attributes & BACKGROUND_ALL) == background) {
		attributes &= ~FOREGROUND_ALL;
		attributes |= FOREGROUND_ALL & ~color;
	} else {
		attributes &= ~FOREGROUND_ALL;
		attributes |= color;
	}

	return attributes;
}

static void log_send_pipe_message(LogPipeMessage *pipe_message) {
	OVERLAPPED overlapped;
	DWORD bytes_written;

	if (!_named_pipe_connected) {
		return;
	}

	mutex_lock(&_named_pipe_write_event_mutex);

	memset(&overlapped, 0, sizeof(overlapped));
	overlapped.hEvent = _named_pipe_write_event;

	if (!WriteFile(_named_pipe, pipe_message, sizeof(*pipe_message),
	               NULL, &overlapped) &&
	    GetLastError() == ERROR_IO_PENDING) {
		// wait for result of overlapped I/O to avoid a race condition with
		// the next WriteFile call that will reuse the same event handle
		GetOverlappedResult(_named_pipe, &overlapped, &bytes_written, TRUE);
	}

	mutex_unlock(&_named_pipe_write_event_mutex);
}

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

		log_error("Could not create named pipe overlapped connect/read event: %s (%d)",
		          get_errno_name(rc), rc);

		goto cleanup;
	}

	phase = 1;

	// create stop event
	_named_pipe_stop_event = CreateEvent(NULL, TRUE, FALSE, NULL);

	if (_named_pipe_stop_event == NULL) {
		rc = ERRNO_WINAPI_OFFSET + GetLastError();

		log_error("Could not create named pipe stop event: %s (%d)",
		          get_errno_name(rc), rc);

		goto cleanup;
	}

	phase = 2;

	// start loop
	_named_pipe_running = true;
	semaphore_release(handshake);

	log_debug("Started named pipe connect thread");

	while (_named_pipe_running) {
		// connect named pipe
		memset(&overlapped, 0, sizeof(overlapped));
		overlapped.hEvent = overlapped_event;

		if (ConnectNamedPipe(_named_pipe, &overlapped)) {
			rc = ERRNO_WINAPI_OFFSET + GetLastError();

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
				// named pipe connect thread stopped
				goto cleanup;
			} else if (rc == WAIT_OBJECT_0 + 1) {
				_named_pipe_connected = true;

				log_info("Log Viewer connected");
			} else {
				rc = ERRNO_WINAPI_OFFSET + GetLastError();

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
				DisconnectNamedPipe(_named_pipe);

				log_info("Log Viewer disconnected");

				_named_pipe_connected = false;

				break;
			}

			events[0] = _named_pipe_stop_event;
			events[1] = overlapped_event;

			rc = WaitForMultipleObjects(2, events, FALSE, INFINITE);

			if (rc == WAIT_OBJECT_0) {
				// named pipe connect thread stopped
				goto cleanup;
			} else if (rc == WAIT_OBJECT_0 + 1) {
				DisconnectNamedPipe(_named_pipe);

				log_info("Log Viewer disconnected");

				_named_pipe_connected = false;

				break;
			} else {
				rc = ERRNO_WINAPI_OFFSET + GetLastError();

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
		// fall through

	case 1:
		CloseHandle(overlapped_event);
		// fall through

	default:
		break;
	}

	_named_pipe_running = false;
}

void log_init_platform(IO *output) {
	int rc;
	Semaphore handshake;

	log_set_output_platform(output);

	mutex_create(&_named_pipe_write_event_mutex);

	// open event log
	_event_log = RegisterEventSourceA(NULL, "Brick Daemon");

	if (_event_log == NULL) {
		rc = ERRNO_WINAPI_OFFSET + GetLastError();

		log_error("Could not open Windows event log: %s (%d)",
		          get_errno_name(rc), rc);
	}

	// create named pipe for log messages
	_named_pipe_write_event = CreateEventA(NULL, TRUE, FALSE, NULL);

	if (_named_pipe_write_event == NULL) {
		rc = ERRNO_WINAPI_OFFSET + GetLastError();

		log_error("Could not create named pipe overlapped write event: %s (%d)",
		          get_errno_name(rc), rc);
	} else {
		_named_pipe = CreateNamedPipeA("\\\\.\\pipe\\tinkerforge-brick-daemon-debug-log",
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
			// because another instance of brickd is already running. no point
			// in logging this cases on debug level here, as log level is still
			// at the default info level yet. so just ignore these two cases
			if (rc != ERRNO_WINAPI_OFFSET + ERROR_PIPE_BUSY &&
			    rc != ERRNO_WINAPI_OFFSET + ERROR_ACCESS_DENIED) {
				log_error("Could not create named pipe: %s (%d)",
				          get_errno_name(rc), rc);
			}
		} else {
			// create named pipe connect thread
			if (semaphore_create(&handshake) < 0) {
				rc = ERRNO_WINAPI_OFFSET + GetLastError();

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

	_named_pipe_connected = false;

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

	mutex_destroy(&_named_pipe_write_event_mutex);
}

void log_set_output_platform(IO *output) {
	HANDLE console;
	CONSOLE_SCREEN_BUFFER_INFO screen_buffer_info;

	_output = output;
	_console = NULL;

	if (_output != &log_stderr_output) {
		return;
	}

	console = (HANDLE)_get_osfhandle(_output->write_handle);

	if (console == INVALID_HANDLE_VALUE) {
		return;
	}

	if (GetFileType(console) != FILE_TYPE_CHAR) {
		return;
	}

	if (!GetConsoleScreenBufferInfo(console, &screen_buffer_info)) {
		return;
	}

	_console = console;
	_default_attributes = screen_buffer_info.wAttributes;
}

void log_apply_color_platform(LogLevel level, bool begin) {
	WORD attributes = _default_attributes;

	if (_console == NULL) {
		return;
	}

	if (begin) {
		switch (level) {
		case LOG_LEVEL_DUMMY: // ignore this to avoid compiler warning
			break;

		case LOG_LEVEL_ERROR:
			attributes = log_prepare_color_attributes(FOREGROUND_RED | FOREGROUND_INTENSITY);
			break;

		case LOG_LEVEL_WARN:
			// FIXME: select blue or yellow depending on background color
			attributes = log_prepare_color_attributes(FOREGROUND_BLUE | FOREGROUND_INTENSITY);
			break;

		case LOG_LEVEL_INFO:
			attributes = log_prepare_color_attributes(FOREGROUND_WHITE | FOREGROUND_INTENSITY);
			break;

		case LOG_LEVEL_DEBUG:
			attributes = log_prepare_color_attributes(FOREGROUND_WHITE);
			break;
		}

		SetConsoleTextAttribute(_console, attributes);
	} else {
		SetConsoleTextAttribute(_console, _default_attributes);
	}
}

bool log_is_included_platform(LogLevel level, LogSource *source,
                              LogDebugGroup debug_group) {
	(void)source;
	(void)debug_group;

	return _named_pipe_connected ||
	       level == LOG_LEVEL_ERROR || level == LOG_LEVEL_WARN;
}

// NOTE: assumes that _mutex (in log.c) is locked
void log_write_platform(struct timeval *timestamp, LogLevel level,
                        LogSource *source, LogDebugGroup debug_group,
                        const char *function, int line,
                        const char *format, va_list arguments) {
	bool libusb;
	LogPipeMessage message;
	WORD type;
	DWORD event_id;
	LPCSTR insert_strings[1];

	(void)debug_group;

	if (_event_log == NULL && !_named_pipe_connected) {
		return;
	}

	libusb = strcmp(source->name, "libusb") == 0;

	vsnprintf(message.message, sizeof(message.message), format, arguments);

	if (_event_log != NULL) {
		switch (level) {
		case LOG_LEVEL_ERROR:
			type = EVENTLOG_ERROR_TYPE;
			event_id = libusb ? BRICKD_LIBUSB_ERROR : BRICKD_GENERIC_ERROR;
			insert_strings[0] = message.message;
			break;

		case LOG_LEVEL_WARN:
			type = EVENTLOG_WARNING_TYPE;
			event_id = libusb ? BRICKD_LIBUSB_WARNING : BRICKD_GENERIC_WARNING;
			insert_strings[0] = message.message;
			break;

		default:
			// ignore all other log levels for the event log
			type = 0;
			event_id = 0;
			insert_strings[0] = NULL;
			break;
		}

		if (insert_strings[0] != NULL) {
			ReportEventA(_event_log, type, 0, event_id, NULL, 1, 0,
			             insert_strings, NULL);
		}
	}

	if (_named_pipe_connected) {
		message.length = sizeof(message);
		message.flags = libusb ? LOG_PIPE_MESSAGE_FLAG_LIBUSB : 0;
		message.timestamp = (uint64_t)timestamp->tv_sec * 1000000 + timestamp->tv_usec;
		message.level = level;
		message.line = line;

		string_copy(message.source, sizeof(message.source),
		            libusb ? function : source->name, -1);

		log_send_pipe_message(&message);
	}
}
