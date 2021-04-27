/*
 * brickd
 * Copyright (C) 2016, 2019-2021 Matthias Bolte <matthias@tinkerforge.com>
 *
 * log_uwp.c: Universal Windows Platform debugger output handling
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

#include <daemonlib\log.h>

static bool _debugger_present = false;

void log_init_platform(IO *output) {
	(void)output;

	_debugger_present = IsDebuggerPresent();
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

	return _debugger_present ? LOG_INCLUSION_SECONDARY : LOG_INCLUSION_NONE;
}

// NOTE: assumes that _output_mutex (in daemonlib/log.c) is locked
void log_output_platform(struct timeval *timestamp, LogLevel level,
                         LogSource *source, LogDebugGroup debug_group,
                         const char *function, int line, const char *message) {
	char buffer[1024] = "<unknown>\r\n";

	log_format(buffer, sizeof(buffer), timestamp, level, source, debug_group,
	           function, line, message);

	OutputDebugStringA(buffer);
}
