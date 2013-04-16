/*
 * brickd
 * Copyright (C) 2012-2013 Matthias Bolte <matthias@tinkerforge.com>
 *
 * utils.h: Utility functions
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

#ifndef BRICKD_UTILS_H
#define BRICKD_UTILS_H

#include <stdint.h>

#define ERRNO_WINAPI_OFFSET 71000000
#define ERRNO_ADDRINFO_OFFSET 72000000

int errno_interrupted(void);

const char *get_errno_name(int error_code);
const char *get_libusb_error_name(int error_code);
const char *get_libusb_transfer_status_name(int transfer_status);

#define GROW_ALLOCATION(size) ((((size) - 1) / 16 + 1) * 16)

typedef void (*FreeFunction)(void *item);

typedef struct {
	int allocated;
	int count;
	int size;
	int relocatable;
	uint8_t *bytes;
} Array;

#define ARRAY_INITIALIZER { 0, 0, 0, 1, NULL }

int array_create(Array *array, int reserved, int size, int relocatable);
void array_destroy(Array *array, FreeFunction function);

int array_reserve(Array *array, int count);
int array_resize(Array *array, int count, FreeFunction function);

void *array_append(Array *array);
void array_remove(Array *array, int i, FreeFunction function);

void *array_get(Array *array, int i);
int array_find(Array *array, void *item);

#define MAX_BASE58_STR_SIZE 8

char *base58_encode(char *string, uint32_t value);

uint32_t uint32_from_le(uint32_t value);

#ifdef __GNUC__
	#ifndef __GNUC_PREREQ
		#define __GNUC_PREREQ(major, minor) \
			((((__GNUC__) << 16) + (__GNUC_MINOR__)) >= (((major) << 16) + (minor)))
	#endif
	#if __GNUC_PREREQ(4, 4)
		#define ATTRIBUTE_FMT_PRINTF(fmtpos, argpos) \
			__attribute__((__format__(__gnu_printf__, fmtpos, argpos)))
	#else
		#define ATTRIBUTE_FMT_PRINTF(fmtpos, argpos) \
			__attribute__((__format__(__printf__, fmtpos, argpos)))
	#endif
	#if __GNUC_PREREQ(4, 6)
		#define STATIC_ASSERT(condition, message) \
			_Static_assert(condition, message)
	#else
		#define STATIC_ASSERT(condition, message) // FIXME
	#endif
#else
	#define ATTRIBUTE_FMT_PRINTF(fmtpos, argpos)
	#define STATIC_ASSERT(condition, message) // FIXME
#endif

#endif // BRICKD_UTILS_H
