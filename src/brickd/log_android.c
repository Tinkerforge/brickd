/*
 * brickd
 * Copyright (C) 2018-2021 Matthias Bolte <matthias@tinkerforge.com>
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

bool android_debugger_connected = false;

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

uint32_t log_check_inclusion_platform(LogLevel level, LogSource *source,
                                      LogDebugGroup debug_group, int line) {
	(void)level;
	(void)source;
	(void)debug_group;
	(void)line;

	return android_debugger_connected ? LOG_INCLUSION_SECONDARY : LOG_INCLUSION_NONE;
}

// NOTE: assumes that _output_mutex (in daemonlib/log.c) is locked
void log_output_platform(struct timeval *timestamp, LogLevel level,
                         LogSource *source, LogDebugGroup debug_group,
                         const char *function, int line, const char *message) {
	android_LogPriority priority;
	char buffer[1024] = "<unknown>\n";

	(void)timestamp;

	switch (level) {
	case LOG_LEVEL_ERROR: priority = ANDROID_LOG_ERROR;   break;
	case LOG_LEVEL_WARN:  priority = ANDROID_LOG_WARN;    break;
	case LOG_LEVEL_INFO:  priority = ANDROID_LOG_INFO;    break;
	case LOG_LEVEL_DEBUG: priority = ANDROID_LOG_DEBUG;   break;
	default:              priority = ANDROID_LOG_UNKNOWN; break;
	}

	log_format(buffer, sizeof(buffer), NULL, LOG_LEVEL_NONE, source, debug_group,
	           function, line, message);

	// FIXME: the timestamp that Android will record for this log message
	//        will be off, because the actual log writing done here is detached
	//        from the log_<level> calls.
	__android_log_write(priority, "brickd", buffer);
}
