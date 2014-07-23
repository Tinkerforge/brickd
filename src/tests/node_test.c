/*
 * brickd
 * Copyright (C) 2014 Matthias Bolte <matthias@tinkerforge.com>
 *
 * node_test.c: Tests for the Node type
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

#include <daemonlib/macros.h>
#include <daemonlib/utils.h>

typedef struct {
	Node node;
	int value;
} Number;

static int validate_numbers(int test, Node *sentinel, int offset, int length) {
	int i;
	Number *number;
	Node *node;

	// forward
	i = offset;
	node = sentinel->next;

	while (node != sentinel) {
		number = containerof(node, Number, node);

		if (number->value != i) {
			printf("test%d: forward number mismatch (actual: %d != expected: %d)\n",
			       test, number->value, i);

			return -1;
		}

		++i;
		node = node->next;
	}

	if (i != offset + length) {
		printf("test%d: forward length mismatch (actual: %d != expected: %d)\n",
		       test, i, offset + length);

		return -1;
	}

	// backward
	i = offset + length - 1;
	node = sentinel->prev;

	while (node != sentinel) {
		number = containerof(node, Number, node);

		if (number->value != i) {
			printf("test%d: backward number mismatch (actual: %d != expected: %d)\n",
			       test, number->value, i);

			return -1;
		}

		--i;
		node = node->prev;
	}

	if (i != offset - 1) {
		printf("test%d: backward length mismatch (actual: %d != expected: %d)\n",
		       test, i, offset - 1);

		return -1;
	}

	return 0;
}

static int test1(void) {
	int result = -1;
	Node sentinel;
	int i;
	Number *number;
	Node *node;

	node_reset(&sentinel);

	for (i = 0; i < 100000; ++i) {
		number = calloc(1, sizeof(Number));

		node_reset(&number->node);

		number->value = i;

		node_insert_before(&sentinel, &number->node);
	}

	if (validate_numbers(1, &sentinel, 0, 100000) < 0) {
		goto cleanup;
	}

	node = sentinel.next;

	node_remove(node);

	number = containerof(node, Number, node);

	free(number);

	if (validate_numbers(1, &sentinel, 1, 99999) < 0) {
		goto cleanup;
	}

	result = 0;

cleanup:
	while (sentinel.next != &sentinel) {
		number = containerof(sentinel.next, Number, node);

		node_remove(&number->node);
		free(number);
	}

	return result;
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
