/*
 * brickd
 * Copyright (C) 2013-2014 Matthias Bolte <matthias@tinkerforge.com>
 *
 * array_test.c: Tests for the Array type
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

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <daemonlib/array.h>

int test1(bool relocatable) {
	Array array;

	if (array_create(&array, 0, sizeof(uint32_t), relocatable) < 0) {
		printf("test1: array_create failed\n");

		return -1;
	}

	*(uint32_t *)array_append(&array) = 5;
	*(uint32_t *)array_append(&array) = 100000042;
	*(uint32_t *)array_append(&array) = 69321;
	*(uint32_t *)array_append(&array) = 17;

	if (array.count != 4) {
		printf("test1: unexpected array.count\n");

		return -1;
	}

	if (*(uint32_t *)array_get(&array, 0) != 5) {
		printf("test1: unexpected result from array_get\n");

		return -1;
	}

	if (*(uint32_t *)array_get(&array, 1) != 100000042) {
		printf("test1: unexpected result from array_get\n");

		return -1;
	}

	if (*(uint32_t *)array_get(&array, 2) != 69321) {
		printf("test1: unexpected result from array_get\n");

		return -1;
	}

	if (*(uint32_t *)array_get(&array, 3) != 17) {
		printf("test1: unexpected result from array_get\n");

		return -1;
	}

	array_remove(&array, 1, NULL);

	if (*(uint32_t *)array_get(&array, 0) != 5) {
		printf("test1: unexpected result from array_get\n");

		return -1;
	}

	if (*(uint32_t *)array_get(&array, 1) != 69321) {
		printf("test1: unexpected result from array_get\n");

		return -1;
	}

	array_destroy(&array, NULL);

	return 0;
}

#define TEST2_ARRAY_SIZE 23

int test2(void) {
	Array array;
	void *references[TEST2_ARRAY_SIZE];
	int i;
	void *value;

	if (array_create(&array, 0, 17, false) < 0) {
		printf("test2: array_create failed\n");

		return -1;
	}

	for (i = 0; i < TEST2_ARRAY_SIZE; ++i) {
		references[i] = array_append(&array);
	}

	if (array.count != TEST2_ARRAY_SIZE) {
		printf("test2: unexpected array.count\n");

		return -1;
	}

	for (i = 0; i < TEST2_ARRAY_SIZE; ++i) {
		value = array_get(&array, 0);

		if (value != references[i]) {
			printf("test2: unexpected result from array_get\n");

			return -1;
		}

		array_remove(&array, 0, NULL);
	}

	array_destroy(&array, NULL);

	return 0;
}

int main(void) {
#ifdef _WIN32
	fixes_init();
#endif

	if (test1(false) < 0 || test1(true) < 0) {
		return EXIT_FAILURE;
	}

	if (test2() < 0) {
		return EXIT_FAILURE;
	}

	printf("success\n");

	return EXIT_SUCCESS;
}
