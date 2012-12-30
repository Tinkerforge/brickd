/*
 * brickd
 * Copyright (C) 2012 Matthias Bolte <matthias@tinkerforge.com>
 *
 * log.c: Logging specific functions
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

#ifndef _WIN32
	#include <sys/time.h>
#endif

#include "log.h"

#include "threads.h"

static Mutex _mutex; // protects writing to _file and calling of _extra_handler
static LogLevel _levels[5] = { LOG_LEVEL_INFO, LOG_LEVEL_INFO, LOG_LEVEL_INFO,
                               LOG_LEVEL_INFO, LOG_LEVEL_INFO };
static FILE *_file = NULL;
static LogHandler _extra_handler = NULL;

void log_init(void) {
	mutex_create(&_mutex);

	_file = stderr;
}

void log_exit(void) {
	mutex_destroy(&_mutex);
}

void log_set_level(LogCategory category, LogLevel level) {
	_levels[category] = level;
}

LogLevel log_get_level(LogCategory category) {
	return _levels[category];
}

void log_set_file(FILE *file) {
	mutex_lock(&_mutex);

	_file = file;

	mutex_unlock(&_mutex);
}

FILE *log_get_file(void) {
	return _file;
}

void log_set_extra_handler(LogHandler handler) {
	_extra_handler = handler;
}

LogHandler log_get_extra_handler(void) {
	return _extra_handler;
}

// NOTE: assumes that _mutex is locked
static void log_file_handler(LogLevel level, const char *file, int line,
                             const char *function, const char *format,
                             va_list arguments)
{
	struct timeval tv;
	time_t t;
	struct tm lt;
	char lt_str[64] = "<unknown>";
	char level_c = 'U';

	(void)function;

	// check file
	if (_file == NULL) {
		return;
	}

	// format time
	tv.tv_sec = 0;
	tv.tv_usec = 0;

	if (gettimeofday(&tv, NULL) == 0) {
		// copy value to time_t variable because timeval.tv_sec and time_t
		// can have different sizes between different compilers and compiler
		// version and platforms. for example with WDK 7 both are 4 byte in
		// size, but with MSVC 2010 time_t is 8 byte in size but timeval.tv_sec
		// is still 4 byte in size.
		t = tv.tv_sec;

		if (localtime_r(&t, &lt) != NULL) {
			strftime(lt_str, sizeof(lt_str), "%Y-%m-%d %H:%M:%S", &lt);
		}
	}

	// format level
	switch (level) {
	case LOG_LEVEL_NONE:  level_c = 'N'; break;
	case LOG_LEVEL_ERROR: level_c = 'E'; break;
	case LOG_LEVEL_WARN:  level_c = 'W'; break;
	case LOG_LEVEL_INFO:  level_c = 'I'; break;
	case LOG_LEVEL_DEBUG: level_c = 'D'; break;
	};

	// print prefix
	fprintf(_file, "%s.%06d <%c> <%s:%d> ",
	        lt_str, (int)tv.tv_usec, level_c, file, line);

	// print message
	vfprintf(_file, format, arguments);
	fprintf(_file, "\n");
	fflush(_file);
}

void log_message(LogCategory category, LogLevel level,
                 const char *file, int line,
                 const char *function, const char *format, ...)
{
	va_list arguments;

	if (level > _levels[category]) {
		return;
	}

	va_start(arguments, format);
	mutex_lock(&_mutex);

	log_file_handler(level, file, line, function, format, arguments);

	if (_extra_handler != NULL) {
		_extra_handler(level, file, line, function, format, arguments);
	}

	mutex_unlock(&_mutex);
	va_end(arguments);
}
