/*
 * brickd
 * Copyright (C) 2014 Matthias Bolte <matthias@tinkerforge.com>
 *
 * base58_test.c: Tests for the Base58 de/encoder
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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../brickd/utils.h"

int test1(void) {
	char base58[BASE58_MAX_LENGTH];

	if (strcmp(base58_encode(base58, 0), "1") != 0) {
		printf("test1: unexpected result from 0\n");

		return -1;
	}

	if (strcmp(base58_encode(base58, 54544), "hdq") != 0) {
		printf("test1: unexpected result from 54544\n");

		return -1;
	}

	if (strcmp(base58_encode(base58, 4294967295), "7xwQ9g") != 0) {
		printf("test1: unexpected result from 4294967295\n");

		return -1;
	}

	return 0;
}

int test2(void) {
	uint32_t value;

	if (base58_decode(&value, "1") < 0 || value != 0) {
		printf("test2: unexpected result from '1': %s (%d)\n",
		       get_errno_name(errno), errno);

		return -1;
	}

	if (base58_decode(&value, "hdq") < 0 || value != 54544) {
		printf("test2: unexpected result from 'hdq': %s (%d)\n",
		       get_errno_name(errno), errno);

		return -1;
	}

	if (base58_decode(&value, "7xwQ9g") < 0 || value != 4294967295) {
		printf("test2: unexpected result from '7xwQ9g': %s (%d)\n",
		       get_errno_name(errno), errno);

		return -1;
	}

	return 0;
}

int main(void) {
#ifdef _WIN32
	fixes_init();
#endif

	if (test1() < 0) {
		return EXIT_FAILURE;
	}

	if (test2() < 0) {
		return EXIT_FAILURE;
	}

	printf("success\n");

	return EXIT_SUCCESS;
}
