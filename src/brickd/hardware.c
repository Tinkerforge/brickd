/*
 * brickd
 * Copyright (C) 2013 Matthias Bolte <matthias@tinkerforge.com>
 *
 * hardware.c: Hardware specific functions
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

#include "hardware.h"

#include "log.h"
#include "packet.h"
#include "stack.h"

#define LOG_CATEGORY LOG_CATEGORY_HARDWARE

static Array _stacks = ARRAY_INITIALIZER;

int hardware_init(void) {
	log_debug("Initializing hardware subsystem");

	// create stacks array
	if (array_create(&_stacks, 32, sizeof(Stack *), 1) < 0) {
		log_error("Could not create stack array: %s (%d)",
		          get_errno_name(errno), errno);

		return -1;
	}

	return 0;
}

void hardware_exit(void) {
	log_debug("Shutting down hardware subsystem");

	if (_stacks.count > 0) {
		log_warn("Still %d stack(s) active", _stacks.count);
	}

	array_destroy(&_stacks, NULL);
}

int hardware_add_stack(Stack *stack) {
	Stack **new_stack = array_append(&_stacks);

	if (new_stack == NULL) {
		log_error("Could not append to stack array: %s (%d)",
		          get_errno_name(errno), errno);

		return -1;
	}

	*new_stack = stack;

	return 0;
}

int hardware_remove_stack(Stack *stack) {
	int i;
	Stack *candidate;

	for (i = 0; i < _stacks.count; ++i) {
		candidate = *(Stack **)array_get(&_stacks, i);

		if (candidate == stack) {
			array_remove(&_stacks, i, NULL);
			return 0;
		}
	}

	log_error("Stack %s not found in stack array", stack->name);

	return -1;
}

void hardware_dispatch_packet(Packet *packet) {
	char base58[MAX_BASE58_STR_SIZE];
	int i;
	Stack *stack;
	int rc;
	int dispatched = 0;

	if (_stacks.count == 0) {
		log_debug("No stacks connected, dropping request (U: %s, L: %u, F: %u, S: %u, R: %u)",
		          base58_encode(base58, uint32_from_le(packet->header.uid)),
		          packet->header.length,
		          packet->header.function_id,
		          packet_header_get_sequence_number(&packet->header),
		          packet_header_get_response_expected(&packet->header));

		return;
	}

	if (packet->header.uid == 0) {
		log_debug("Broadcasting request (U: %s, L: %u, F: %u, S: %u, R: %u) to %d stack(s)",
		          base58_encode(base58, uint32_from_le(packet->header.uid)),
		          packet->header.length,
		          packet->header.function_id,
		          packet_header_get_sequence_number(&packet->header),
		          packet_header_get_response_expected(&packet->header),
		          _stacks.count);

		// broadcast to all stacks
		for (i = 0; i < _stacks.count; ++i) {
			stack = *(Stack **)array_get(&_stacks, i);

			stack_dispatch_packet(stack, packet, 1);
		}
	} else {
		log_debug("Dispatching request (U: %s, L: %u, F: %u, S: %u, R: %u) to %d stack(s)",
		          base58_encode(base58, uint32_from_le(packet->header.uid)),
		          packet->header.length,
		          packet->header.function_id,
		          packet_header_get_sequence_number(&packet->header),
		          packet_header_get_response_expected(&packet->header),
		          _stacks.count);

		// dispatch to all stacks, not only the first one that might claim to
		// know the UID
		for (i = 0; i < _stacks.count; ++i) {
			stack = *(Stack **)array_get(&_stacks, i);

			rc = stack_dispatch_packet(stack, packet, 0);

			if (rc < 0) {
				continue;
			} else if (rc > 0) {
				dispatched = 1;
			}
		}

		if (dispatched) {
			return;
		}

		log_debug("Broadcasting request because UID is currently unknown");

		// broadcast to all stacks, as no stack claimed to know the UID
		for (i = 0; i < _stacks.count; ++i) {
			stack = *(Stack **)array_get(&_stacks, i);

			stack_dispatch_packet(stack, packet, 1);
		}
	}
}
