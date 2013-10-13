/*
 * brickd
 * Copyright (C) 2013 Matthias Bolte <matthias@tinkerforge.com>
 *
 * throughput_test.c: A probably meaningless throughput test
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

#include "ip_connection.h"
#include "brick_master.h"
#include "../brickd/utils.h"

/*

Windows 8:
USB 3.0: 0.175 msec per getter
USB 2.0: 0.263 msec per getter

Ubuntu 12.04:
USB 2.0: 1.999 msec per getter

*/

int main(void) {
	IPConnection ipcon;
	Master master;
	int i;
	int repeats = 10000;
	uint16_t voltage;
	uint64_t start, stop;

#ifdef _WIN32
	fixes_init();
#endif

	ipcon_create(&ipcon);
	master_create(&master, "6wwv71", &ipcon);

	if (ipcon_connect(&ipcon, "localhost", 4223) < 0) {
		printf("error 1\n");
		return EXIT_FAILURE;
	}

	start = microseconds();

	for (i = 0; i < repeats; ++i) {
		if (master_get_usb_voltage(&master, &voltage) < 0) {
			return EXIT_FAILURE;
		}
		//printf("%u\n", voltage);
	}

	stop = microseconds();

	printf("%.10f msec\n", ((stop - start) / 1000.0) / repeats);

	ipcon_destroy(&ipcon);

	return EXIT_SUCCESS;
}
