/*
 * brickd
 * Copyright (C) 2012-2014 Matthias Bolte <matthias@tinkerforge.com>
 *
 * fixes_msvc.h: Fixes for problems with the MSVC/WDK headers and libs
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

#ifndef BRICKD_FIXES_MSVC_H
#define BRICKD_FIXES_MSVC_H

#ifdef _MSC_VER

#include <stdio.h> // ensure vsnprintf() is declared before redefining it
#include <stdlib.h> // ensure putenv() is declared before fixed_putenv()
#include <process.h> // for GetCurrentProcessId()
#include <time.h>
#include <winsock2.h> // for struct timeval

struct timezone {
	int tz_minuteswest;
	int tz_dsttime;
};

void fixes_init(void);

struct tm *localtime_r(const time_t *timep, struct tm *result);

int gettimeofday(struct timeval *tv, struct timezone *tz);

// replace _putenv with fixed_putenv
int fixed_putenv(char *string);
#define _putenv fixed_putenv

// replace getpid with GetCurrentProcessId
#define getpid GetCurrentProcessId

// avoid warnings from MSVC about deprecated POSIX names
#define strdup _strdup
#define getch _getch
#define putenv _putenv
#define fileno _fileno
#define read _read
#define write _write

// ensure that functions are avialable under their POSIX names
#define snprintf(buffer, count, format, ...) \
	_snprintf_s(buffer, count, _TRUNCATE, format, __VA_ARGS__)
#define vsnprintf(buffer, count, format, arguments) \
	_vsnprintf_s(buffer, count, _TRUNCATE, format, arguments)
#define strcasecmp _stricmp
#define strncasecmp _strnicmp

#endif // _MSC_VER

#endif // BRICKD_FIXES_MSVC_H
