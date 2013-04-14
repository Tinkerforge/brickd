/*
 * brickd
 * Copyright (C) 2012 Matthias Bolte <matthias@tinkerforge.com>
 *
 * log_posix.c: POSIX specific log handling
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

#include "log.h"

void log_init_platform(void) {
}

void log_exit_platform(void) {
}

void log_handler_platform(LogCategory category, LogLevel level,
                          const char *file, int line,
                          const char *function, const char *format,
                          va_list arguments) {
	(void)category;
	(void)level;
	(void)file;
	(void)line;
	(void)function;
	(void)format;
	(void)arguments;
}
