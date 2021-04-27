/*
 * brickd
 * Copyright (C) 2013-2014, 2021 Matthias Bolte <matthias@tinkerforge.com>
 *
 * fixes_mingw.c: Fixes for problems with the MinGW headers and libs
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

#ifdef __MINGW32__

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <windows.h>

#include "fixes_mingw.h"

typedef void (WINAPI *GETSYSTEMTIMEPRECISEASFILETIME)(LPFILETIME);

static GETSYSTEMTIMEPRECISEASFILETIME ptr_GetSystemTimePreciseAsFileTime = NULL;

void fixes_init(void) {
	// GetSystemTimePreciseAsFileTime was added in Windows 8
	ptr_GetSystemTimePreciseAsFileTime =
	  (GETSYSTEMTIMEPRECISEASFILETIME)(void *)GetProcAddress(GetModuleHandleA("kernel32"),
	                                                         "GetSystemTimePreciseAsFileTime");
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

	memcpy(result, temp, sizeof(*result));

	return result;
}

// difference between Unix epoch and January 1, 1601 in 100-nanoseconds
#define DELTA_EPOCH 116444736000000000ULL

// MinGW's gettimeofday doesn't provide the highest available resolution,
// implement gettimeofday based on GetSystemTime(Precise)AsFileTime
int fixed_gettimeofday(struct timeval *tv, struct timezone *tz) {
	FILETIME ft;
	uint64_t t;

	(void)tz;

	if (tv != NULL) {
		if (ptr_GetSystemTimePreciseAsFileTime != NULL) {
			ptr_GetSystemTimePreciseAsFileTime(&ft);
		} else {
			GetSystemTimeAsFileTime(&ft);
		}

		t = ((uint64_t)ft.dwHighDateTime << 32) | (uint64_t)ft.dwLowDateTime;
		t = (t - DELTA_EPOCH) / 10; // 100-nanoseconds to microseconds

		tv->tv_sec = (long)(t / 1000000UL);
		tv->tv_usec = (long)(t % 1000000UL);
	}

	return 0;
}

#undef putenv // undefine to avoid calling fixed_putenv() from fixed_putenv()

// MinGW's putenv might require to call putenv("NAME=") to remove NAME
// instead of putenv("NAME")
int fixed_putenv(char *string) {
	char *buffer;
	int length;
	int rc;

	if (strchr(string, '=') != NULL) {
		return putenv(string);
	}

	rc = putenv(string);

	if (rc >= 0) {
		// no fix necessary
		return rc;
	}

	length = strlen(string);
	buffer = malloc(length + 2);

	if (buffer == NULL) {
		errno = ENOMEM;

		return -1;
	}

	strcpy(buffer, string);

	buffer[length + 0] = '=';
	buffer[length + 1] = '\0';

	rc = putenv(buffer);

	free(buffer);

	return rc;
}

#endif // __MINGW32__
