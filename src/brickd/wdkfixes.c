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

#include <windows.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "wdkfixes.h"

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

// implement localtime_r based on _localtime32_s
struct tm *localtime_r(const time_t *timep, struct tm *result) {
	if (_localtime32_s(result, timep) == 0) {
		return result;
	} else {
		return NULL;
	}
}

#endif // BRICKD_WDK_BUILD
