/*
 * endiandetect
 * Copyright (C) 2013 Matthias Bolte <matthias@tinkerforge.com>
 *
 * endiandetect.c: Utility programm for endian detection
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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

// undefine potential defines from /usr/include/endian.h
#undef LITTLE_ENDIAN
#undef BIG_ENDIAN

#define LITTLE_ENDIAN 0x03020100ul
#define BIG_ENDIAN    0x00010203ul

static const union {
	uint8_t bytes[4];
	uint32_t value;
} native_endian = {
	{ 0, 1, 2, 3 }
};

int main(void) {
	printf("#ifndef BRICKD_ENDIAN_H\n");
	printf("#define BRICKD_ENDIAN_H\n");
	printf("\n");
	printf("#define BYTE_ORDER_IS_LITTLE_ENDIAN ");

	if (native_endian.value == LITTLE_ENDIAN) {
		printf("1\n");
	} else {
		printf("0\n");
	}

	printf("\n");
	printf("#endif // BRICKD_ENDIAN_H\n");

	return EXIT_SUCCESS;
}
