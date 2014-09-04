/*
 * brickd
 * Copyright (C) 2012-2014 Matthias Bolte <matthias@tinkerforge.com>
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

#include "network.h"
#include "stack.h"

#define LOG_CATEGORY LOG_CATEGORY_HARDWARE

int stack_create(Stack *stack, const char *name,
                 StackDispatchRequestFunction dispatch_request) {
	string_copy(stack->name, name, sizeof(stack->name));

	stack->dispatch_request = dispatch_request;

	if (array_create(&stack->recipients, 32, sizeof(Recipient), true) < 0) {
		log_error("Could not create recipient array: %s (%d)",
		          get_errno_name(errno), errno);

		return -1;
	}

	return 0;
}

void stack_destroy(Stack *stack) {
	array_destroy(&stack->recipients, NULL);
}

int stack_add_recipient(Stack *stack, uint32_t uid /* always little endian */, int opaque) {
	int i;
	Recipient *recipient;
	char base58[BASE58_MAX_LENGTH];

	for (i = 0; i < stack->recipients.count; ++i) {
		recipient = array_get(&stack->recipients, i);

		if (recipient->uid == uid) {
			recipient->opaque = opaque;

			return 0;
		}
	}

	recipient = array_append(&stack->recipients);

	if (recipient == NULL) {
		log_error("Could not append %s to recipient array: %s (%d)",
		          base58_encode(base58, uint32_from_le(uid)),
		          get_errno_name(errno), errno);

		return -1;
	}

	recipient->uid = uid;
	recipient->opaque = opaque;

	return 0;
}

Recipient *stack_get_recipient(Stack *stack, uint32_t uid /* always little endian */) {
	int i;
	Recipient *recipient;

	for (i = 0; i < stack->recipients.count; ++i) {
		recipient = array_get(&stack->recipients, i);

		if (recipient->uid == uid) {
			return recipient;
		}
	}

	return NULL;
}

// returns -1 on error, 0 if the request was not dispatched and 1 if it was dispatch
int stack_dispatch_request(Stack *stack, Packet *request, bool force) {
	Recipient *recipient = NULL;

	if (!force) {
		recipient = stack_get_recipient(stack, request->header.uid);

		if (recipient == NULL) {
			return 0;
		}
	}

	if (stack->dispatch_request(stack, request, recipient) < 0) {
		return -1;
	}

	if (force) {
		log_debug("Forced to sent request to %s", stack->name);
	} else {
		log_debug("Sent request to %s", stack->name);
	}

	return 1;
}

void stack_announce_disconnect(Stack *stack) {
	int i;
	Recipient *recipient;
	EnumerateCallback enumerate_callback;

	log_debug("Disconnecting stack '%s'", stack->name);

	for (i = 0; i < stack->recipients.count; ++i) {
		recipient = array_get(&stack->recipients, i);

		memset(&enumerate_callback, 0, sizeof(enumerate_callback));

		enumerate_callback.header.uid = recipient->uid;
		enumerate_callback.header.length = sizeof(enumerate_callback);
		enumerate_callback.header.function_id = CALLBACK_ENUMERATE;
		packet_header_set_sequence_number(&enumerate_callback.header, 0);
		packet_header_set_response_expected(&enumerate_callback.header, true);

		base58_encode(enumerate_callback.uid, uint32_from_le(recipient->uid));
		enumerate_callback.enumeration_type = ENUMERATION_TYPE_DISCONNECTED;

		log_debug("Sending enumerate-disconnected callback (uid: %s)",
		          enumerate_callback.uid);

		network_dispatch_response((Packet *)&enumerate_callback);
	}
}
