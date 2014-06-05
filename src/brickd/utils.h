/*
 * brickd
 * Copyright (C) 2012-2014 Matthias Bolte <matthias@tinkerforge.com>
 * Copyright (C) 2014 Olaf Lüke <olaf@tinkerforge.com>
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

#include "sha1.h"

#define ERRNO_WINAPI_OFFSET 71000000
#define ERRNO_ADDRINFO_OFFSET 72000000

#ifndef ABS
	#define ABS(a) (((a) < 0) ? (-(a)) : (a))
#endif
#ifndef MIN
	#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
	#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

typedef void (*FreeFunction)(void *item);

int errno_interrupted(void);
int errno_would_block(void);

const char *get_errno_name(int error_code);

#define GROW_ALLOCATION(size) ((((size) - 1) / 16 + 1) * 16)

void string_copy(char *destination, const char *source, int size);
void string_append(char *destination, const char *source, int size);

#define BASE58_MAX_LENGTH 8

char *base58_encode(char *base58, uint32_t value);
int base58_decode(uint32_t *value, const char *base58);

uint32_t uint32_from_le(uint32_t value);

void millisleep(uint32_t milliseconds);

uint64_t microseconds(void);

uint32_t get_random_uint32(void);

void hmac_sha1(uint8_t *secret, int secret_length,
               uint8_t *data, int data_length,
               uint8_t digest[SHA1_DIGEST_LENGTH]);

#if !defined _GNU_SOURCE && !defined __APPLE__
char *strcasestr(char *haystack, char *needle);
#endif

#endif // BRICKD_UTILS_H
