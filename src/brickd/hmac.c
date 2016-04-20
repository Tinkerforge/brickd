/*
 * brickd
 * Copyright (C) 2012-2014, 2016 Matthias Bolte <matthias@tinkerforge.com>
 * Copyright (C) 2014 Olaf Lüke <olaf@tinkerforge.com>
 *
 * hmac.c: HMAC functions
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
#ifndef _WIN32
	#include <sys/types.h>
	#include <sys/stat.h>
	#include <fcntl.h>
	#include <unistd.h>
	#include <time.h>
#endif
#include <stdlib.h>
#ifndef _MSC_VER
	#include <sys/time.h>
#endif
#ifdef _WIN32
	#include <windows.h>
	#include <wincrypt.h>
	#include <process.h>
#endif

#include "hmac.h"

#ifndef _WIN32

static int read_uint32_non_blocking(const char *filename, uint32_t *value) {
	int fd = open(filename, O_NONBLOCK);
	int rc;

	if (fd < 0) {
		return -1;
	}

	rc = read(fd, value, sizeof(uint32_t));

	close(fd);

	return rc != sizeof(uint32_t) ? -1 : 0;
}

#endif

// this function is not meant to be called often,
// this function is meant to provide a good random seed value
uint32_t get_random_uint32(void) {
	uint32_t r;
	struct timeval tv;
	uint32_t seconds;
	uint32_t microseconds;
#ifdef BRICKD_UWP_BUILD
	if (BCryptGenRandom(NULL, (UCHAR *)&r, sizeof(r),
	                    BCRYPT_USE_SYSTEM_PREFERRED_RNG) != /*STATUS_SUCCESS*/0) {
		goto fallback;
	}
#elif defined(_WIN32)
	HCRYPTPROV hprovider;

	if (!CryptAcquireContextA(&hprovider, NULL, NULL, PROV_RSA_FULL,
	                          CRYPT_VERIFYCONTEXT | CRYPT_SILENT)) {
		goto fallback;
	}

	if (!CryptGenRandom(hprovider, sizeof(r), (BYTE *)&r)) {
		CryptReleaseContext(hprovider, 0);

		goto fallback;
	}

	CryptReleaseContext(hprovider, 0);
#else
	// try /dev/urandom first, if not available or a read would
	// block then fall back to /dev/random
	if (read_uint32_non_blocking("/dev/urandom", &r) < 0) {
		if (read_uint32_non_blocking("/dev/random", &r) < 0) {
			goto fallback;
		}
	}
#endif

	return r;

fallback:
	// if no other random source is available fall back to the current time
	if (gettimeofday(&tv, NULL) < 0) {
		seconds = (uint32_t)time(NULL);
		microseconds = 0;
	} else {
		seconds = tv.tv_sec;
		microseconds = tv.tv_usec;
	}

	return (seconds << 26 | seconds >> 6) + microseconds + getpid(); // overflow is intended
}

void hmac_sha1(uint8_t *secret, int secret_length,
               uint8_t *data, int data_length,
               uint8_t digest[SHA1_DIGEST_LENGTH]) {
	SHA1 sha1;
	uint8_t secret_digest[SHA1_DIGEST_LENGTH];
	uint8_t inner_digest[SHA1_DIGEST_LENGTH];
	uint8_t ipad[SHA1_BLOCK_LENGTH];
	uint8_t opad[SHA1_BLOCK_LENGTH];
	int i;

	if (secret_length > SHA1_BLOCK_LENGTH) {
		sha1_init(&sha1);
		sha1_update(&sha1, secret, secret_length);
		sha1_final(&sha1, secret_digest);

		secret = secret_digest;
		secret_length = SHA1_DIGEST_LENGTH;
	}

	// inner digest
	for (i = 0; i < secret_length; ++i) {
		ipad[i] = secret[i] ^ 0x36;
	}

	for (i = secret_length; i < SHA1_BLOCK_LENGTH; ++i) {
		ipad[i] = 0x36;
	}

	sha1_init(&sha1);
	sha1_update(&sha1, ipad, SHA1_BLOCK_LENGTH);
	sha1_update(&sha1, data, data_length);
	sha1_final(&sha1, inner_digest);

	// outer digest
	for (i = 0; i < secret_length; ++i) {
		opad[i] = secret[i] ^ 0x5C;
	}

	for (i = secret_length; i < SHA1_BLOCK_LENGTH; ++i) {
		opad[i] = 0x5C;
	}

	sha1_init(&sha1);
	sha1_update(&sha1, opad, SHA1_BLOCK_LENGTH);
	sha1_update(&sha1, inner_digest, SHA1_DIGEST_LENGTH);
	sha1_final(&sha1, digest);
}
