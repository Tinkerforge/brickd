/*
 * brickd
 * Copyright (C) 2021 Matthias Bolte <matthias@tinkerforge.com>
 *
 * fifo_test.c: Tests for the FIFO type
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
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include <daemonlib/fifo.h>
#include <daemonlib/threads.h>

#define TEST1_BUFFER_SIZE (256 * 1024)

typedef struct {
	FIFO *fifo;
	uint8_t *output_buffer;
	int output_buffer_used;
} Test1;

static void test1_reader(void *opaque) {
	Test1 *test1 = opaque;
	int length;

	while (true) {
		length = fifo_read(test1->fifo, test1->output_buffer + test1->output_buffer_used,
		                   TEST1_BUFFER_SIZE - test1->output_buffer_used, 0);

		if (length < 0) {
			printf("test1: fifo_read failed: %d\n", errno);

			return;
		}

		if (length == 0) {
			return;
		}

		test1->output_buffer_used += length;
	}
}

int test1(void) {
	int fd;
	uint16_t input_length[TEST1_BUFFER_SIZE];
	uint8_t input_buffer[TEST1_BUFFER_SIZE];
	uint8_t output_buffer[TEST1_BUFFER_SIZE];
	Test1 test1;
	Thread thread;
	uint8_t fifo_buffer[512];
	FIFO fifo;
	int i;
	int length;
	int input_buffer_used = 0;

	fd = open("/dev/urandom", O_RDONLY);

	if (fd < 0) {
		printf("test1: could not open /dev/urandom\n");

		return -1;
	}

	if (read(fd, input_length, sizeof(input_length)) != sizeof(input_length)) {
		printf("test1: could not read from /dev/urandom\n");

		return -1;
	}

	if (read(fd, input_buffer, sizeof(input_buffer)) != sizeof(input_buffer)) {
		printf("test1: could not read from /dev/urandom\n");

		return -1;
	}

	fifo_create(&fifo, fifo_buffer, sizeof(fifo_buffer));

	test1.output_buffer = output_buffer;
	test1.output_buffer_used = 0;
	test1.fifo = &fifo;

	thread_create(&thread, test1_reader, &test1);

	for (i = 0; i < (int)sizeof(input_length); ++i) {
		length = input_length[i] % 1024;

		if (length == 0) {
			continue;
		}

		if (input_buffer_used + length > TEST1_BUFFER_SIZE) {
			break;
		}

		length = fifo_write(&fifo, input_buffer + input_buffer_used, length, 0);

		if (length < 0) {
			printf("test1: fifo_write failed: %d\n", errno);

			return -1;
		}

		input_buffer_used += length;
	}

	fifo_shutdown(&fifo);

	thread_join(&thread);
	thread_destroy(&thread);

	if (test1.output_buffer_used != input_buffer_used) {
		printf("test1: buffer usage mismatch: %d != %d\n", test1.output_buffer_used, input_buffer_used);

		return -1;
	}

	if (memcmp(input_buffer, output_buffer, input_buffer_used) != 0) {
		printf("test1: buffer content mismatch\n");

		return -1;
	}

	fifo_destroy(&fifo);

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
