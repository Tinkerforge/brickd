/*
 * brickd
 * Copyright (C) 2012-2013 Matthias Bolte <matthias@tinkerforge.com>
 *
 * stack.c: Stack specific functions
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

/*
 * a stack represents a Brick or a set of Bricks connected to an interface
 * (e.g. USB). the Brick Daemon acts as the proxy between the Tinkerforge
 * TCP/IP protocol used by the API bindings and other interfaces (e.g. USB).
 *
 * the Stack type is used as a generic base for specific types such as the
 * USBStack that deals with the USB communication. it keeps track of the list
 * of known UIDs for a stack and provides a generic dispatch function to send
 * requests to a stack. the interface specific implementation of the dispatch
 * function is done in the specific stack types such as the USBStack.
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <daemonlib/array.h>
#include <daemonlib/log.h>
#include <daemonlib/utils.h>

#include "stack.h"

#define LOG_CATEGORY LOG_CATEGORY_HARDWARE

int stack_create(Stack *stack, const char *name,
                 StackDispatchRequestFunction dispatch_request) {
	string_copy(stack->name, name, sizeof(stack->name));

	stack->dispatch_request = dispatch_request;

	if (array_create(&stack->uids, 32, sizeof(uint32_t), 1) < 0) {
		log_error("Could not create UID array: %s (%d)",
		          get_errno_name(errno), errno);

		return -1;
	}

	return 0;
}

void stack_destroy(Stack *stack) {
	array_destroy(&stack->uids, NULL);
}

int stack_add_uid(Stack *stack, uint32_t uid /* always little endian */) {
	int i;
	uint32_t known_uid;
	uint32_t *new_uid;
	char base58[BASE58_MAX_LENGTH];

	for (i = 0; i < stack->uids.count; ++i) {
		known_uid = *(uint32_t *)array_get(&stack->uids, i);

		if (known_uid == uid) {
			return 0;
		}
	}

	new_uid = array_append(&stack->uids);

	if (new_uid == NULL) {
		log_error("Could not append %s to UID array: %s (%d)",
		          base58_encode(base58, uint32_from_le(uid)),
		          get_errno_name(errno), errno);

		return -1;
	}

	*new_uid = uid;

	return 0;
}

int stack_knows_uid(Stack *stack, uint32_t uid /* always little endian */) {
	int i;
	uint32_t known_uid;

	for (i = 0; i < stack->uids.count; ++i) {
		known_uid = *(uint32_t *)array_get(&stack->uids, i);

		if (known_uid == uid) {
			return 1;
		}
	}

	return 0;
}

// returns -1 on error, 0 if the request was not dispatched and 1 if it was dispatch
int stack_dispatch_request(Stack *stack, Packet *request, int force) {
	if (!force && !stack_knows_uid(stack, request->header.uid)) {
		return 0;
	}

	if (stack->dispatch_request(stack, request) < 0) {
		return -1;
	}

	if (force) {
		log_debug("Forced to sent request to %s", stack->name);
	} else {
		log_debug("Sent request to %s", stack->name);
	}

	return 1;
}
