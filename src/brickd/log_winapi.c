/*
 * brickd
 * Copyright (C) 2012, 2014, 2016 Matthias Bolte <matthias@tinkerforge.com>
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

#include <io.h>
#include <libusb.h>
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

#include <daemonlib/packed_begin.h>

typedef enum { // bitmask
	LOG_PIPE_MESSAGE_FLAG_LIBUSB = 0x0001
} LogPipeMessageFlag;

typedef struct {
	uint16_t length;
	uint8_t flags;
	uint64_t timestamp; // in microseconds
	uint8_t level;
	char source[128];
	int line;
	char message[1024];
} LogPipeMessage;

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

bool log_libusb_debug = false;

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

	if (!WriteFile(_named_pipe, pipe_message, sizeof(*pipe_message), NULL, &overlapped) &&
	    GetLastError() == ERROR_IO_PENDING) {
		// wait for result of overlapped I/O to avoid a race condition with
		// the next WriteFile call that will reuse the same event handle
		GetOverlappedResult(_named_pipe, &overlapped, &bytes_written, TRUE);
	}

	mutex_unlock(&_named_pipe_write_event_mutex);
}

static void LIBUSB_CALL log_forward_libusb_message(libusb_context *ctx,
                                                   enum libusb_log_level libusb_level,
                                                   const char *function,
                                                   const char *format,
                                                   va_list arguments) {
	struct timeval timestamp;
	time_t unix_seconds;
	struct tm localized_timestamp;
	char formatted_timestamp[64] = "<unknown>";
	LogLevel level;
	char level_char;
	char buffer[1024] = "<unknown>";
	int length;
	LogPipeMessage pipe_message;

	(void)ctx;

	if (libusb_level == LIBUSB_LOG_LEVEL_NONE) {
		return;
	}

	if (!log_libusb_debug && !_named_pipe_connected) {
		return;
	}

	if (gettimeofday(&timestamp, NULL) < 0) {
		timestamp.tv_sec = time(NULL);
		timestamp.tv_usec = 0;
	}

	if (log_libusb_debug && _output != NULL) {
		// copy value to time_t variable because timeval.tv_sec and time_t
		// can have different sizes between different compilers and compiler
		// version and platforms. for example with WDK 7 both are 4 byte in
		// size, but with MSVC 2010 time_t is 8 byte in size but timeval.tv_sec
		// is still 4 byte in size.
		unix_seconds = timestamp.tv_sec;

		// format time
		if (localtime_r(&unix_seconds, &localized_timestamp) != NULL) {
			strftime(formatted_timestamp, sizeof(formatted_timestamp),
			         "%Y-%m-%d %H:%M:%S", &localized_timestamp);
		}

		// format level
		switch (libusb_level) {
		case LIBUSB_LOG_LEVEL_ERROR:   level = LOG_LEVEL_ERROR; level_char = 'E'; break;
		case LIBUSB_LOG_LEVEL_WARNING: level = LOG_LEVEL_WARN;  level_char = 'W'; break;
		case LIBUSB_LOG_LEVEL_INFO:    level = LOG_LEVEL_INFO;  level_char = 'I'; break;
		case LIBUSB_LOG_LEVEL_DEBUG:   level = LOG_LEVEL_DEBUG; level_char = 'D'; break;
		default:                       level = LOG_LEVEL_ERROR; level_char = 'U'; break;
		}

		// format prefix
		snprintf(buffer, sizeof(buffer), "%s.%06d <%c> <libusb|%s> ",
		         formatted_timestamp, (int)timestamp.tv_usec, level_char,
		         function);

		length = strlen(buffer);

		// format message
		vsnprintf(buffer + length, sizeof(buffer) - length, format, arguments);

		length = strlen(buffer);

		// format newline
		snprintf(buffer + length, sizeof(buffer) - length, "\n");

		length = strlen(buffer);

		// write output
		log_lock();
		log_apply_color_platform(level, true);
		io_write(_output, buffer, length);
		log_apply_color_platform(level, false);
		log_unlock();
	}

	if (_named_pipe_connected) {
		pipe_message.length = sizeof(pipe_message);
		pipe_message.flags = LOG_PIPE_MESSAGE_FLAG_LIBUSB;
		pipe_message.timestamp = (uint64_t)timestamp.tv_sec * 1000000 + timestamp.tv_usec;
		pipe_message.line = -1;

		switch (libusb_level) {
		default:
		case LIBUSB_LOG_LEVEL_ERROR:   pipe_message.level = LOG_LEVEL_ERROR; break;
		case LIBUSB_LOG_LEVEL_WARNING: pipe_message.level = LOG_LEVEL_WARN;  break;
		case LIBUSB_LOG_LEVEL_INFO:    pipe_message.level = LOG_LEVEL_INFO;  break;
		case LIBUSB_LOG_LEVEL_DEBUG:   pipe_message.level = LOG_LEVEL_DEBUG; break;
		}

		string_copy(pipe_message.source, sizeof(pipe_message.source), function);
		vsnprintf(pipe_message.message, sizeof(pipe_message.message), format,
		          arguments);

		log_send_pipe_message(&pipe_message);
	}
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

	case 1:
		CloseHandle(overlapped_event);

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
	_event_log = RegisterEventSource(NULL, "Brick Daemon");

	if (_event_log == NULL) {
		rc = ERRNO_WINAPI_OFFSET + GetLastError();

		log_error("Could not open Windows event log: %s (%d)",
		          get_errno_name(rc), rc);
	}

	// create named pipe for log messages
	_named_pipe_write_event = CreateEvent(NULL, TRUE, FALSE, NULL);

	if (_named_pipe_write_event == NULL) {
		rc = ERRNO_WINAPI_OFFSET + GetLastError();

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

	// set libusb log funcrion
	libusb_set_log_function(log_forward_libusb_message);
}

void log_exit_platform(void) {
	libusb_set_log_function(NULL);

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

	if (_output == NULL || strcmp(_output->type, "stderr") != 0) {
		return;
	}

	console = (HANDLE)_get_osfhandle(_output->handle);

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

bool log_is_message_included_platform(LogLevel level) {
	if (_named_pipe_connected) {
		return true;
	}

	return level == LOG_LEVEL_ERROR || level == LOG_LEVEL_WARN;
}

// NOTE: assumes that _mutex (in log.c) is locked
void log_secondary_output_platform(struct timeval *timestamp, LogLevel level,
                                   LogSource *source, int line,
                                   const char *format, va_list arguments) {
	WORD type = 0;
	DWORD event_id = 0;
	LPCSTR insert_strings[1] = {NULL};
	LogPipeMessage pipe_message;

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

	vsnprintf(pipe_message.message, sizeof(pipe_message.message), format, arguments);

	if (_event_log != NULL && insert_strings[0] != NULL) {
		ReportEvent(_event_log, type, 0, event_id, NULL, 1, 0, insert_strings, NULL);
	}

	if (_named_pipe_connected) {
		pipe_message.length = sizeof(pipe_message);
		pipe_message.flags = 0;
		pipe_message.timestamp = (uint64_t)timestamp->tv_sec * 1000000 + timestamp->tv_usec;
		pipe_message.level = level;
		pipe_message.line = line;

		string_copy(pipe_message.source, sizeof(pipe_message.source), source->name);

		log_send_pipe_message(&pipe_message);
	}
}
