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

#ifndef _MSC_VER
	#include <sys/time.h>
#endif

#include "log.h"

#include "threads.h"

static Mutex _mutex; // protects writing to _file
static LogLevel _levels[5] = { LOG_LEVEL_INFO,
                               LOG_LEVEL_INFO,
                               LOG_LEVEL_INFO,
                               LOG_LEVEL_INFO,
                               LOG_LEVEL_INFO };
static FILE *_file = NULL;

extern void log_init_platform(void);
extern void log_exit_platform(void);
extern void log_handler_platform(LogCategory category, LogLevel level,
                                 const char *file, int line,
                                 const char *function, const char *format,
                                 va_list arguments);

// NOTE: assumes that _mutex is locked
static void log_handler(LogCategory category, LogLevel level, const char *file,
                        int line, const char *function, const char *format,
                        va_list arguments)
{
	struct timeval tv;
	time_t t;
	struct tm lt;
	char lt_str[64] = "<unknown>";
	char level_c = 'U';
	const char *category_name = "unknown";

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
	}

	// format category
	switch (category) {
	case LOG_CATEGORY_EVENT:   category_name = "event"; break;
	case LOG_CATEGORY_USB:     category_name = "usb"; break;
	case LOG_CATEGORY_NETWORK: category_name = "network"; break;
	case LOG_CATEGORY_HOTPLUG: category_name = "hotplug"; break;
	case LOG_CATEGORY_OTHER:   category_name = "other"; break;
	}

	// print prefix
	fprintf(_file, "%s.%06d <%c> <%s|%s:%d> ",
	        lt_str, (int)tv.tv_usec, level_c, category_name, file, line);

	// print message
	vfprintf(_file, format, arguments);
	fprintf(_file, "\n");
	fflush(_file);
}

void log_init(void) {
	mutex_create(&_mutex);

	_file = stderr;

	log_init_platform();
}

void log_exit(void) {
	log_exit_platform();

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

void log_message(LogCategory category, LogLevel level,
                 const char *file, int line,
                 const char *function, const char *format, ...)
{
	va_list arguments;

	va_start(arguments, format);
	mutex_lock(&_mutex);

	log_handler(category, level, file, line, function, format, arguments);
	log_handler_platform(category, level, file, line, function, format, arguments);

	mutex_unlock(&_mutex);
	va_end(arguments);
}
