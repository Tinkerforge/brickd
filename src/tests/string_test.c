/*
 * brickd
 * Copyright (C) 2015 Matthias Bolte <matthias@tinkerforge.com>
 *
 * string_test.c: Tests for string helper functions
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

#include <daemonlib/utils.h>

int test1(void) {
	if (!string_ends_with("", "", true)) {
		printf("test1: 1 failed\n");

		return -1;
	}

	if (!string_ends_with("foobar", "", true)) {
		printf("test1: 2 failed\n");

		return -1;
	}

	if (string_ends_with("", "foobar", true)) {
		printf("test1: 3 failed\n");

		return -1;
	}

	if (string_ends_with("foo", "foobar", true)) {
		printf("test1: 4 failed\n");

		return -1;
	}

	if (!string_ends_with("blubb foobar", "foobar", true)) {
		printf("test1: 5 failed\n");

		return -1;
	}

	if (!string_ends_with("blubb foobAr", "foobar", false)) {
		printf("test1: 6 failed\n");

		return -1;
	}

	if (string_ends_with("blubb foobAr", "foobar", true)) {
		printf("test1: 7 failed\n");

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

	printf("success\n");

	return EXIT_SUCCESS;
}
