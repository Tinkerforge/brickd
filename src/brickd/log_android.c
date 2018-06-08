/*
 * brickd
 * Copyright (C) 2018 Matthias Bolte <matthias@tinkerforge.com>
 *
 * log_android.c: Android log handling
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

#include <string.h>

#include <android/log.h>

#include <daemonlib/log.h>

void log_init_platform(IO *output) {
	(void)output;
}

void log_exit_platform(void) {
}

void log_set_output_platform(IO *output) {
	(void)output;
}

void log_apply_color_platform(LogLevel level, bool begin) {
	(void)level;
	(void)begin;
}

bool log_is_included_platform(LogLevel level, LogSource *source,
                              LogDebugGroup debug_group) {
	(void)level;
	(void)source;
	(void)debug_group;

	return true; // FIXME
}

// NOTE: assumes that _mutex (in log.c) is locked
void log_write_platform(struct timeval *timestamp, LogLevel level,
                        LogSource *source, LogDebugGroup debug_group,
                        const char *function, int line,
                        const char *format, va_list arguments) {
	android_LogPriority priority;
	char buffer[1024] = "<unknown>";
	char *debug_group_name = "";
	char line_str[16] = "<unknown>";
	int offset;

	(void)timestamp;

	switch (level) {
	case LOG_LEVEL_ERROR: priority = ANDROID_LOG_ERROR;   break;
	case LOG_LEVEL_WARN:  priority = ANDROID_LOG_WARN;    break;
	case LOG_LEVEL_INFO:  priority = ANDROID_LOG_INFO;    break;
	case LOG_LEVEL_DEBUG: priority = ANDROID_LOG_DEBUG;   break;
	default:              priority = ANDROID_LOG_UNKNOWN; break;
	}

	// format debug group
	switch (debug_group) {
	case LOG_DEBUG_GROUP_EVENT:  debug_group_name = "event|";  break;
	case LOG_DEBUG_GROUP_PACKET: debug_group_name = "packet|"; break;
	case LOG_DEBUG_GROUP_OBJECT: debug_group_name = "object|"; break;
	case LOG_DEBUG_GROUP_LIBUSB:                               break;
	default:                                                   break;
	}

	// format line
	snprintf(line_str, sizeof(line_str), "%d", line);

	// format prefix
	snprintf(buffer, sizeof(buffer), "<%s%s:%s> ",
	         debug_group_name, source->name, line >= 0 ? line_str : function);

	offset = strlen(buffer); // FIXME: avoid strlen call

	// format message
	vsnprintf(buffer + offset, MAX(sizeof(buffer) - offset, 0), format, arguments);

	offset = strlen(buffer); // FIXME: avoid strlen call

	// format newline
	snprintf(buffer + offset, MAX(sizeof(buffer) - offset, 0), LOG_NEWLINE);

	__android_log_write(priority, "brickd", buffer);
}
