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

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "stack.h"

#include "log.h"
#include "utils.h"

#define LOG_CATEGORY LOG_CATEGORY_HARDWARE

int stack_create(Stack *stack, const char *name,
                 DispatchPacketFunction dispatch_packet) {
	string_copy(stack->name, name, sizeof(stack->name));

	stack->dispatch_packet = dispatch_packet;

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

	for (i = 0; i < stack->uids.count; ++i) {
		known_uid = *(uint32_t *)array_get(&stack->uids, i);

		if (known_uid == uid) {
			return 0;
		}
	}

	new_uid = array_append(&stack->uids);

	if (new_uid == NULL) {
		log_error("Could not append to UID array: %s (%d)",
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

// returns -1 on error, 0 if the packet was not dispatched and 1 if it was dispatch
int stack_dispatch_packet(Stack *stack, Packet *packet, int force) {
	int rc = 0;

	if (force || stack_knows_uid(stack, packet->header.uid)) {
		rc = stack->dispatch_packet(stack, packet);

		if (rc == 1) {
			if (force) {
				log_debug("Forced to sent request to %s", stack->name);
			} else {
				log_debug("Sent request to %s", stack->name);
			}
		}
	}

	return rc;
}
