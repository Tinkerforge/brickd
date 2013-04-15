/*
 * brickd
 * Copyright (C) 2012 Matthias Bolte <matthias@tinkerforge.com>
 *
 * log.h: Logging specific functions
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

#ifndef BRICKD_LOG_H
#define BRICKD_LOG_H

#include <stdarg.h>
#include <stdio.h>

#include "utils.h"

typedef enum {
	LOG_CATEGORY_EVENT = 0,
	LOG_CATEGORY_USB,
	LOG_CATEGORY_NETWORK,
	LOG_CATEGORY_HOTPLUG,
	LOG_CATEGORY_OTHER
} LogCategory;

typedef enum {
	LOG_LEVEL_NONE = 0,
	LOG_LEVEL_ERROR,
	LOG_LEVEL_WARN,
	LOG_LEVEL_INFO,
	LOG_LEVEL_DEBUG
} LogLevel;

typedef void (*LogHandler)(LogLevel level, const char *file, int line,
                           const char *function, const char *format,
                           va_list arguments);

#ifdef BRICKD_LOG_ENABLED
	#ifdef _MSC_VER
		#define log_message_checked(level, ...) \
			do { \
				if ((level) <= log_get_level(LOG_CATEGORY)) { \
					log_message(LOG_CATEGORY, level, __FILE__, \
					            __LINE__, __FUNCTION__, __VA_ARGS__); \
				} \
			__pragma(warning(push)) \
			__pragma(warning(disable:4127)) \
			} while (0) \
			__pragma(warning(pop))
	#else
		#define log_message_checked(level, ...) \
			do { \
				if ((level) <= log_get_level(LOG_CATEGORY)) { \
					log_message(LOG_CATEGORY, level, __FILE__, \
					            __LINE__, __FUNCTION__, __VA_ARGS__); \
				} \
			} while (0)
	#endif

	#define log_error(...) log_message_checked(LOG_LEVEL_ERROR, __VA_ARGS__)
	#define log_warn(...)  log_message_checked(LOG_LEVEL_WARN, __VA_ARGS__)
	#define log_info(...)  log_message_checked(LOG_LEVEL_INFO, __VA_ARGS__)
	#define log_debug(...) log_message_checked(LOG_LEVEL_DEBUG, __VA_ARGS__)
#else
	#define log_error(...) ((void)0)
	#define log_warn(...)  ((void)0)
	#define log_info(...)  ((void)0)
	#define log_debug(...) ((void)0)
#endif

void log_init(void);
void log_exit(void);

void log_set_level(LogCategory category, LogLevel level);
LogLevel log_get_level(LogCategory category);

void log_set_file(FILE *file);
FILE *log_get_file(void);

void log_message(LogCategory category, LogLevel level,
                 const char *file, int line, const char *function,
                 const char *format, ...) ATTRIBUTE_FMT_PRINTF(6, 7);

#endif // BRICKD_LOG_H
