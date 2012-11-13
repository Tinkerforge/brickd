/*
 * brickd
 * Copyright (C) 2012 Matthias Bolte <matthias@tinkerforge.com>
 *
 * wdkfixes.c: Fixes for problems in the Windows Driver Kit headers and libs
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

#ifdef BRICKD_WDK_BUILD

// stop system header from defining _wstat
#define _WSTAT_DEFINED

#include <errno.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <windows.h>

#include "wdkfixes.h"

// difference between Unix epoch and January 1, 1601 in 100-nanoseconds
#define DELTA_EPOCH 116444736000000000ULL

// getpid is missing in WDK's msvcrt.lib, only in libcmt.lib
int __cdecl getpid(void) {
	return GetCurrentProcessId();
}

// _getpid is missing in WDK's msvcrt.lib, only in libcmt.lib
int __cdecl _getpid(void) {
	return GetCurrentProcessId();
}

// _wstat32 is missing in WDK's msvcrt.lib, only in libcmt.lib
int __cdecl _wstat(const wchar_t *path, struct _stat *buffer);

int __cdecl _wstat32(const wchar_t *path, struct _stat *buffer) {
	return _wstat(path, buffer);
}

// implement localtime_r based on localtime
struct tm *localtime_r(const time_t *timep, struct tm *result) {
	struct tm *temp;

	// localtime is thread-safe, it uses thread local storage for its
	// return value on Windows
	temp = localtime(timep);

	if (temp == NULL) {
		return NULL;
	}

	memcpy(result, temp, sizeof(struct tm));

	return result;
}

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

#endif // BRICKD_WDK_BUILD
