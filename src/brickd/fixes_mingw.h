/*
 * brickd
 * Copyright (C) 2013-2014, 2021 Matthias Bolte <matthias@tinkerforge.com>
 *
 * fixes_mingw.h: Fixes for problems with the MinGW headers and libs
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

#ifndef BRICKD_FIXES_MINGW_H
#define BRICKD_FIXES_MINGW_H

#ifdef __MINGW32__

#include <sys/time.h> // ensure gettimeofday() is declared before fixed_gettimeofday()
#include <stdlib.h> // ensure putenv() is declared before fixed_putenv()
#include <time.h>

void fixes_init(void);

#ifndef PROCESS_QUERY_LIMITED_INFORMATION
	#define PROCESS_QUERY_LIMITED_INFORMATION 0x1000
#endif

#ifndef IPV6_V6ONLY
	#define IPV6_V6ONLY 27
#endif

struct tm *localtime_r(const time_t *timep, struct tm *result);

// replace gettimeofday with fixed_gettimeofday
int fixed_gettimeofday(struct timeval *tv, struct timezone *tz);
#define gettimeofday fixed_gettimeofday

// replace putenv with fixed_putenv
int fixed_putenv(char *string);
#define putenv fixed_putenv

#endif // __MINGW32__

#endif // BRICKD_FIXES_MINGW_H
