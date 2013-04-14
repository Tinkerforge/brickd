/*
 * brickd
 * Copyright (C) 2012 Matthias Bolte <matthias@tinkerforge.com>
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

#include <windows.h>

#include "log.h"

#include "log_messages.h"
#include "utils.h"

#define LOG_CATEGORY LOG_CATEGORY_OTHER

static HANDLE _event_log = NULL;

void log_init_platform(void) {
	int rc;

	_event_log = RegisterEventSource(NULL, "Brick Daemon");

	if (_event_log == NULL) {
		rc = ERRNO_WINAPI_OFFSET + GetLastError();

		// this will go to the logfile if it is enabled via --debug
		log_error("Could not open Windows event log: %s (%d)",
		          get_errno_name(rc), rc);
	}
}

void log_exit_platform(void) {
	if (_event_log != NULL) {
		DeregisterEventSource(_event_log);
	}
}

void log_handler_platform(LogCategory category, LogLevel level,
                          const char *file, int line,
                          const char *function, const char *format,
                          va_list arguments) {
	WORD type;
	DWORD event_id;
	char message[512 + 1] = "<unknown>";
	LPCSTR insert_strings[1];

	(void)category;
	(void)file;
	(void)line;
	(void)function;

	if (_event_log == NULL) {
		return;
	}

	switch (level) {
	case LOG_LEVEL_ERROR:
		type = EVENTLOG_ERROR_TYPE;
		event_id = BRICKD_GENERIC_ERROR;
		break;

	case LOG_LEVEL_WARN:
		type = EVENTLOG_WARNING_TYPE;
		event_id = BRICKD_GENERIC_WARNING;
		break;

	default:
		// ignore all other log levels
		return;
	}

#ifdef _MSC_VER
	_vsnprintf_s(message, sizeof(message), sizeof(message) - 1, format, arguments);
#else
	vsnprintf(message, sizeof(message), format, arguments);
#endif

	insert_strings[0] = message;

	ReportEvent(_event_log, type, 0, event_id, NULL, 1, 0, insert_strings, NULL);
}
