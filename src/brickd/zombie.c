/*
 * brickd
 * Copyright (C) 2014 Matthias Bolte <matthias@tinkerforge.com>
 *
 * zombie.c: Zombie client specific functions
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

#include <daemonlib/log.h>

#include "zombie.h"

#include "client.h"

static LogSource _log_source = LOG_SOURCE_INITIALIZER;

static uint32_t _next_id = 0;

static void zombie_handle_timeout(void *opaque) {
	Zombie *zombie = opaque;

	zombie->finished = true;
}

int zombie_create(Zombie *zombie, Client *client) {
	Node *pending_request_client_node;
	PendingRequest *pending_request;

	zombie->id = _next_id++;
	zombie->finished = false;
	zombie->pending_request_count = client->pending_request_count;

	log_debug("Creating zombie (id: %u) from client ("CLIENT_SIGNATURE_FORMAT") for %d pending request(s)",
	          zombie->id, client_expand_signature(client), zombie->pending_request_count);

	// create single shot timer with a delay of 1sec
	if (timer_create_(&zombie->timer, zombie_handle_timeout, zombie) < 0) {
		log_error("Could not create zombie timer: %s (%d)",
		          get_errno_name(errno), errno);

		return -1;
	}

	if (timer_configure(&zombie->timer, 1000000, 0) < 0) {
		log_error("Could not start zombie timer: %s (%d)",
		          get_errno_name(errno), errno);

		timer_destroy(&zombie->timer);

		return -1;
	}

	// insert new sentinal and remove old one to take over the list
	node_reset(&zombie->pending_request_sentinel);
	node_insert_after(&client->pending_request_sentinel, &zombie->pending_request_sentinel);
	node_remove(&client->pending_request_sentinel);

	client->pending_request_count = 0;

	// set all client pointers to NULL to indicate zombie status
	pending_request_client_node = zombie->pending_request_sentinel.next;

	while (pending_request_client_node != &zombie->pending_request_sentinel) {
		pending_request = containerof(pending_request_client_node, PendingRequest, client_node);

		pending_request->client = NULL;
		pending_request->zombie = zombie;

		pending_request_client_node = pending_request_client_node->next;
	}

	return 0;
}

void zombie_destroy(Zombie *zombie) {
	PendingRequest *pending_request;

	if (zombie->pending_request_count > 0) {
		log_warn("Destroying zombie (id: %u) while %d request(s) are still pending",
		         zombie->id, zombie->pending_request_count);

		while (zombie->pending_request_sentinel.next != &zombie->pending_request_sentinel) {
			pending_request = containerof(zombie->pending_request_sentinel.next, PendingRequest, client_node);

			pending_request_remove_and_free(pending_request);
		}
	}

	timer_destroy(&zombie->timer);
}

void zombie_dispatch_response(Zombie *zombie, PendingRequest *pending_request) {
	pending_request_remove_and_free(pending_request);

	if (zombie->pending_request_count == 0) {
		zombie->finished = true;

		log_debug("Zombie (id: %u) finished", zombie->id);

		if (timer_configure(&zombie->timer, 0, 0) < 0) {
			log_error("Could not stop zombie timer: %s (%d)",
			          get_errno_name(errno), errno);

			return;
		}
	}
}
