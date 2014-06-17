/*
 * brickd
 * Copyright (C) 2012-2014 Matthias Bolte <matthias@tinkerforge.com>
 *
 * fixes_msvc.c: Fixes for problems with the MSVC/WDK headers and libs
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

#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <time.h>
#include <windows.h>

#include <daemonlib/utils.h>

#include "fixes_msvc.h"

typedef void (WINAPI *GETSYSTEMTIMEPRECISEASFILETIME)(LPFILETIME);
typedef int (*PUTENV)(const char *);
typedef errno_t (*PUTENV_S)(const char *, const char *);

static GETSYSTEMTIMEPRECISEASFILETIME ptr_GetSystemTimePreciseAsFileTime = NULL;
static PUTENV_S ptr_putenv_s = NULL;
static PUTENV ptr_putenv = NULL;

void fixes_init(void) {
	HMODULE hmodule = NULL;

	ptr_GetSystemTimePreciseAsFileTime =
	  (GETSYSTEMTIMEPRECISEASFILETIME)GetProcAddress(GetModuleHandleA("kernel32"),
	                                                 "GetSystemTimePreciseAsFileTime");

	// _putenv_s is not avialable on Windows XP by default, so find _putenv_s
	// and _putenv at runtime. as brickd might not be linked to msvcrt.dll
	// (could be msvcrtXY.dll) GetModuleHandle cannot be used with "msvcrt"
	// as module name. use GetModuleHandleEx with the address of the getenv
	// function instead. disable warning C4054 for this call, otherwise MSVC
	// will complain about a function to data pointer cast here.
#pragma warning(disable: 4054)
	GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
	                   GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
	                   (LPCSTR)getenv, &hmodule);
#pragma warning(default: 4054)

	ptr_putenv_s = (PUTENV_S)GetProcAddress(hmodule, "_putenv_s");
	ptr_putenv = (PUTENV)GetProcAddress(hmodule, "_putenv");
}

#ifdef BRICKD_WDK_BUILD

// implement localtime_r based on localtime, WDK is missing localtime_s
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

// implement gettimeofday based on GetSystemTime(Precise)AsFileTime
int gettimeofday(struct timeval *tv, struct timezone *tz) {
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

// implement putenv based on _putenv_s
static int fixed_putenv_a(char *string) {
	char *value = strchr(string, '=');
	char *buffer;
	errno_t rc;

	if (value == NULL) {
		rc = ptr_putenv_s(string, "");

		if (rc != 0) {
			errno = rc;

			return -1;
		}
	} else {
		buffer = strdup(string);

		if (buffer == NULL) {
			errno = ENOMEM;

			return -1;
		}

		value = strchr(buffer, '=');
		*value++ = '\0';

		rc = ptr_putenv_s(buffer, value);

		if (rc != 0) {
			errno = rc;

			free(buffer);

			return -1;
		}

		free(buffer);
	}

	return 0;
}

// implement putenv based on _putenv which requires to call
// _putenv("NAME=") to remove NAME from the environment
static int fixed_putenv_b(char *string) {
	char *buffer;
	int length;
	int rc;

	if (strchr(string, '=') != NULL) {
		return ptr_putenv(string);
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

	rc = ptr_putenv(buffer);

	free(buffer);

	return rc;
}

// implement putenv with "NAME" semantic (instead of "NAME=")
// to remove NAME from the environment
int fixed_putenv(char *string) {
	if (ptr_putenv_s != NULL) {
		return fixed_putenv_a(string);
	} else if (ptr_putenv != NULL) {
		return fixed_putenv_b(string);
	} else {
		errno = ENOSYS;

		return -1;
	}
}

#endif // _MSC_VER
