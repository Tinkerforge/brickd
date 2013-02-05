/*
 * brickd
 * Copyright (C) 2013 Matthias Bolte <matthias@tinkerforge.com>
 *
 * mingwfixes.c: Fixes for problems with the MinGW headers and libs
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

#include <string.h>

#include "mingwfixes.h"

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

#endif // __MINGW32__
