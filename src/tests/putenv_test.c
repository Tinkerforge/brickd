/*
 * brickd
 * Copyright (C) 2014 Matthias Bolte <matthias@tinkerforge.com>
 *
 * putenv_test.c: Tests for the WDK putenv replacment
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int test1(void) {
	const char *s;

	if (putenv("foobar=blubb") < 0) {
		printf("test1: putenv 1 failed\n");

		return -1;
	}

	s = getenv("foobar");

	if (s == NULL) {
		printf("test1: getenv 1 failed\n");

		return -1;
	}

	if (strcmp(s, "blubb") != 0) {
		printf("test1: value mismatch\n");

		return -1;
	}

	if (putenv("foobar") < 0) {
		printf("test1: putenv 2 failed\n");

		return -1;
	}

	s = getenv("foobar");

	if (s != NULL) {
		printf("test1: getenv 2 failed\n");

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
