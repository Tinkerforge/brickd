/*
 * brickd
 * Copyright (C) 2013 Matthias Bolte <matthias@tinkerforge.com>
 *
 * queue_test.c: Tests for the Queue type
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

#include <daemonlib/queue.h>

int test1(void) {
	Queue queue;

	if (queue_create(&queue, sizeof(uint32_t))) {
		printf("test1: queue_create failed\n");

		return -1;
	}

	*(uint32_t *)queue_push(&queue) = 5;
	*(uint32_t *)queue_push(&queue) = 100000042;
	*(uint32_t *)queue_push(&queue) = 69321;
	*(uint32_t *)queue_push(&queue) = 17;

	if (queue.count != 4) {
		printf("test1: unexpected queue.count\n");

		return -1;
	}

	if (*(uint32_t *)queue_peek(&queue) != 5) {
		printf("test1: unexpected result from queue_peek\n");

		return -1;
	}

	queue_pop(&queue, NULL);

	if (*(uint32_t *)queue_peek(&queue) != 100000042) {
		printf("test1: unexpected result from queue_peek\n");

		return -1;
	}

	*(uint32_t *)queue_push(&queue) = 23;

	queue_pop(&queue, NULL);

	if (*(uint32_t *)queue_peek(&queue) != 69321) {
		printf("test1: unexpected result from queue_peek\n");

		return -1;
	}

	queue_pop(&queue, NULL);

	if (*(uint32_t *)queue_peek(&queue) != 17) {
		printf("test1: unexpected result from queue_peek\n");

		return -1;
	}

	queue_pop(&queue, NULL);

	if (*(uint32_t *)queue_peek(&queue) != 23) {
		printf("test1: unexpected result from queue_peek\n");

		return -1;
	}

	queue_pop(&queue, NULL);

	if (queue_peek(&queue) != NULL) {
		printf("test1: unexpected result from queue_peek\n");

		return -1;
	}

	queue_destroy(&queue, NULL);

	return 0;
}

#define TEST2_QUEUE_SIZE 23

int test2(void) {
	Queue queue;
	void *references[TEST2_QUEUE_SIZE];
	int i;
	void *value;

	if (queue_create(&queue, 17)) {
		printf("test2: queue_create failed\n");

		return -1;
	}

	for (i = 0; i < TEST2_QUEUE_SIZE; ++i) {
		references[i] = queue_push(&queue);
	}

	if (queue.count != TEST2_QUEUE_SIZE) {
		printf("test2: unexpected queue.count\n");

		return -1;
	}

	for (i = 0; i < TEST2_QUEUE_SIZE; ++i) {
		value = queue_peek(&queue);

		if (value != references[i]) {
			printf("test2: unexpected result from queue_peek\n");

			return -1;
		}

		queue_pop(&queue, NULL);
	}

	queue_destroy(&queue, NULL);

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
