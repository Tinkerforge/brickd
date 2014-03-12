/*
 * brickd
 * Copyright (C) 2014 Matthias Bolte <matthias@tinkerforge.com>
 *
 * sha1_test.c: Tests for the SHA1 implementation
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

#include "../brickd/sha1.h"

// http://www.di-mgt.com.au/sha_testvectors.html

int test1(void) {
	SHA1 sha1;
	uint8_t digest[SHA1_DIGEST_LENGTH];
	uint8_t expected_digest[SHA1_DIGEST_LENGTH] = {
		0xA9, 0x99, 0x3E, 0x36, 0x47, 0x06, 0x81, 0x6A, 0xBA, 0x3E, 0x25, 0x71, 0x78, 0x50, 0xC2, 0x6C, 0x9C, 0xD0, 0xD8, 0x9D
	};

	sha1_init(&sha1);
	sha1_update(&sha1, (uint8_t *)"abc", 3);
	sha1_final(&sha1, digest);

	if (memcmp(digest, expected_digest, SHA1_DIGEST_LENGTH) != 0) {
		printf("test1: digest mismatch\n");

		return -1;
	}

	return 0;
}

int test2(void) {
	SHA1 sha1;
	uint8_t digest[SHA1_DIGEST_LENGTH];
	uint8_t expected_digest[SHA1_DIGEST_LENGTH] = {
		0x84, 0x98, 0x3E, 0x44, 0x1C, 0x3B, 0xD2, 0x6E, 0xBA, 0xAE, 0x4A, 0xA1, 0xF9, 0x51, 0x29, 0xE5, 0xE5, 0x46, 0x70, 0xF1
	};

	sha1_init(&sha1);
	sha1_update(&sha1, (uint8_t *)"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56);
	sha1_final(&sha1, digest);

	if (memcmp(digest, expected_digest, SHA1_DIGEST_LENGTH) != 0) {
		printf("test2: digest mismatch\n");

		return -1;
	}

	return 0;
}

int test3(void) {
	SHA1 sha1;
	uint8_t digest[SHA1_DIGEST_LENGTH];
	uint8_t expected_digest[SHA1_DIGEST_LENGTH] = {
		0xA4, 0x9B, 0x24, 0x46, 0xA0, 0x2C, 0x64, 0x5B, 0xF4, 0x19, 0xF9, 0x95, 0xB6, 0x70, 0x91, 0x25, 0x3A, 0x04, 0xA2, 0x59
	};

	sha1_init(&sha1);
	sha1_update(&sha1, (uint8_t *)"abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu", 112);
	sha1_final(&sha1, digest);

	if (memcmp(digest, expected_digest, SHA1_DIGEST_LENGTH) != 0) {
		printf("test3: digest mismatch\n");

		return -1;
	}

	return 0;
}

int test4(void) {
	SHA1 sha1;
	int i;
	uint8_t digest[SHA1_DIGEST_LENGTH];
	uint8_t expected_digest[SHA1_DIGEST_LENGTH] = {
		0x34, 0xAA, 0x97, 0x3C, 0xD4, 0xC4, 0xDA, 0xA4, 0xF6, 0x1E, 0xEB, 0x2B, 0xDB, 0xAD, 0x27, 0x31, 0x65, 0x34, 0x01, 0x6F
	};

	sha1_init(&sha1);

	for (i = 0; i < 1000000; ++i) {
		sha1_update(&sha1, (uint8_t *)"a", 1);
	}

	sha1_final(&sha1, digest);

	if (memcmp(digest, expected_digest, SHA1_DIGEST_LENGTH) != 0) {
		printf("test4: digest mismatch\n");

		return -1;
	}

	return 0;
}

int test5(void) {
	SHA1 sha1;
	uint8_t digest[SHA1_DIGEST_LENGTH];
	uint8_t expected_digest[SHA1_DIGEST_LENGTH] = {
		0xDA, 0x39, 0xA3, 0xEE, 0x5E, 0x6B, 0x4B, 0x0D, 0x32, 0x55, 0xBF, 0xEF, 0x95, 0x60, 0x18, 0x90, 0xAF, 0xD8, 0x07, 0x09
	};

	sha1_init(&sha1);
	sha1_update(&sha1, (uint8_t *)"", 0);
	sha1_final(&sha1, digest);

	if (memcmp(digest, expected_digest, SHA1_DIGEST_LENGTH) != 0) {
		printf("test5: digest mismatch\n");

		return -1;
	}

	return 0;
}

int test6(void) {
	SHA1 sha1;
	int i;
	uint8_t digest[SHA1_DIGEST_LENGTH];
	uint8_t expected_digest[SHA1_DIGEST_LENGTH] = {
		0x77, 0x89, 0xF0, 0xC9, 0xEF, 0x7B, 0xFC, 0x40, 0xD9, 0x33, 0x11, 0x14, 0x3D, 0xFB, 0xE6, 0x9E, 0x20, 0x17, 0xF5, 0x92
	};

	sha1_init(&sha1);

	for (i = 0; i < 16777216; ++i) {
		sha1_update(&sha1, (uint8_t *)"abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmno", 64);
	}

	sha1_final(&sha1, digest);

	if (memcmp(digest, expected_digest, SHA1_DIGEST_LENGTH) != 0) {
		printf("test6: digest mismatch\n");

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

	if (test3() < 0) {
		return EXIT_FAILURE;
	}

	if (test4() < 0) {
		return EXIT_FAILURE;
	}

	if (test5() < 0) {
		return EXIT_FAILURE;
	}

	if (test6() < 0) {
		return EXIT_FAILURE;
	}

	printf("success\n");

	return EXIT_SUCCESS;
}
