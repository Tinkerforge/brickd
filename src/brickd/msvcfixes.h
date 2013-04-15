/*
 * brickd
 * Copyright (C) 2012 Matthias Bolte <matthias@tinkerforge.com>
 *
 * msvcfixes.h: Fixes for problems with the MSVC/WDK headers and libs
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

#ifndef BRICKD_MSVCFIXES_H
#define BRICKD_MSVCFIXES_H

#ifdef _MSC_VER

#include <time.h>
#include <winsock2.h> // for struct timeval

struct timezone {
	int tz_minuteswest;
	int tz_dsttime;
};

struct tm *localtime_r(const time_t *timep, struct tm *result);

int gettimeofday(struct timeval *tv, struct timezone *tz);

#define strdup _strdup

#endif // _MSC_VER

#endif // BRICKD_MSVCFIXES_H
