/*
 * brickd
 * Copyright (C) 2012-2013 Matthias Bolte <matthias@tinkerforge.com>
 *
 * msvcfixes.c: Fixes for problems with the MSVC/WDK headers and libs
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

#ifdef _MSC_VER

#include <stdint.h>
#include <time.h>
#include <windows.h>

#include "msvcfixes.h"

#ifdef BRICKD_WDK_BUILD

// implement localtime_r based on localtime, the WDK is missing localtime_s
struct tm *localtime_r(const time_t *timep, struct tm *result) {
	struct tm *temp;

	// localtime is thread-safe, it uses thread local storage for its
	// return value on Windows
	temp = localtime(timep);

	if (temp == NULL) {
		return NULL;
	}

	memcpy(result, temp, sizeof(*result));

	return result;
}

#else // BRICKD_WDK_BUILD

// implement localtime_r based on localtime_s
struct tm *localtime_r(const time_t *timep, struct tm *result) {
	// localtime_s is thread-safe, it uses thread local storage for its
	// return value on Windows
	if (localtime_s(result, timep) == 0) {
		return result;
	} else {
		return NULL;
	}
}

#endif // BRICKD_WDK_BUILD

// difference between Unix epoch and January 1, 1601 in 100-nanoseconds
#define DELTA_EPOCH 116444736000000000ULL

// implement gettimeofday based on GetSystemTimeAsFileTime
int gettimeofday(struct timeval *tv, struct timezone *tz) {
	FILETIME ft;
	uint64_t t;

	(void)tz;

	if (tv != NULL) {
		GetSystemTimeAsFileTime(&ft);

		t = ((uint64_t)ft.dwHighDateTime << 32) | (uint64_t)ft.dwLowDateTime;
		t = (t - DELTA_EPOCH) / 10; // 100-nanoseconds to microseconds

		tv->tv_sec = (long)(t / 1000000UL);
		tv->tv_usec = (long)(t % 1000000UL);
	}

	return 0;
}

#endif // _MSC_VER
