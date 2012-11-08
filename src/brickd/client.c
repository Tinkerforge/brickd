/*
 * brickd
 * Copyright (C) 2012 Matthias Bolte <matthias@tinkerforge.com>
 *
 * client.c: Client specific functions
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
#include <string.h>

#include "client.h"

#include "log.h"
#include "network.h"
#include "socket.h"
#include "usb.h"

#define LOG_CATEGORY LOG_CATEGORY_NETWORK

#define MAX_PENDING_REQUESTS 32

static void client_handle_receive(void *opaque) {
	Client *client = opaque;
	int length;
	PacketHeader *pending_request;

	length = socket_receive(client->socket,
	                        (uint8_t *)&client->packet + client->packet_used,
	                        sizeof(Packet) - client->packet_used);

	if (length < 0) {
		if (!errno_would_block() && !errno_interrupted()) {
			log_error("Could not receive from socket (handle: %d): %s (%d)",
			          client->socket, get_errno_name(errno), errno);
		}

		// FIXME: does this work in case of blocking or interrupted
		// need to retry on interrupted and blocking should never happend
		return;
	}

	if (length == 0) {
		log_info("Socket (handle: %d) disconnected by client", client->socket);

		network_client_disconnected(client);

		return;
	}

	client->packet_used += length;

	while (client->packet_used > 0) {
		if (client->packet_used < (int)sizeof(PacketHeader)) {
			// wait for complete header
			break;
		}

		length = client->packet.header.length;

		if (client->packet_used < length) {
			// wait for complete packet
			break;
		}

		log_debug("Got request (U: %u, L: %u, F: %u, S: %u, R: %u) from socket (handle: %d)",
		          client->packet.header.uid,
		          client->packet.header.length,
		          client->packet.header.function_id,
		          client->packet.header.sequence_number,
		          client->packet.header.response_expected,
		          client->socket);

		if (client->packet.header.response_expected) {
			if (client->pending_requests.count >= MAX_PENDING_REQUESTS) {
				log_warn("Dropping %d items from pending request array of client (socket: %d)",
				         client->pending_requests.count - MAX_PENDING_REQUESTS + 1,
				         client->socket);

				while (client->pending_requests.count >= MAX_PENDING_REQUESTS) {
					array_remove(&client->pending_requests, 0, NULL);
				}
			}

			pending_request = array_append(&client->pending_requests);

			if (pending_request == NULL) {
				log_error("Could not append to pending request array: %s (%d)",
				          get_errno_name(errno), errno);

				return;
			}

			memcpy(pending_request, &client->packet.header, sizeof(PacketHeader));

			log_debug("Added pending request (U: %u, L: %u, F: %u, S: %u) for client (socket: %d)",
			          pending_request->uid,
			          pending_request->length,
			          pending_request->function_id,
			          pending_request->sequence_number,
			          client->socket);
		}

		usb_dispatch_packet(&client->packet);

		memmove(&client->packet, (uint8_t *)&client->packet + length,
		        client->packet_used - length);

		client->packet_used -= length;
	}
}

int client_create(Client *client, EventHandle socket) {
	log_debug("Creating client from socket (handle: %d)", socket);

	client->socket = socket;
	client->packet_used = 0;

	if (array_create(&client->pending_requests, MAX_PENDING_REQUESTS,
	                 sizeof(PacketHeader)) < 0) {
		log_error("Could not create pending request array: %s (%d)",
		          get_errno_name(errno), errno);

		return -1;
	}

	if (event_add_source(client->socket, EVENT_READ,
	                     client_handle_receive, client) < 0) {
		array_destroy(&client->pending_requests, NULL);

		return -1;
	}

	return 0;
}

void client_destroy(Client *client) {
	event_remove_source(client->socket); // FIXME: handle error?
	socket_destroy(client->socket);
	array_destroy(&client->pending_requests, NULL);
}

int client_dispatch_packet(Client *client, Packet *packet, int force) {
	int i;
	Packet *pending_request;
	int found = -1;
	int rc = -1;

	if (!force) {
		for (i = 0; i < client->pending_requests.count; ++i) {
			pending_request = array_get(&client->pending_requests, i);

			if (pending_request->header.uid == packet->header.uid &&
			    pending_request->header.function_id == packet->header.function_id &&
			    pending_request->header.sequence_number == packet->header.sequence_number) {
				found = i;

				break;
			}
		}
	}

	if (force || found >= 0) {
		if (socket_send(client->socket, packet, packet->header.length) < 0) {
			log_error("Could not send response to client (socket: %d): %s (%d)",
			          client->socket, get_errno_name(errno), errno);

			goto cleanup;
		}

		if (force) {
			log_debug("Forced to sent response to client (socket: %d)", client->socket);
		} else {
			log_debug("Sent response to client (socket: %d)", client->socket);
		}
	}

	rc = 0;

cleanup:
	if (found >= 0) {
		array_remove(&client->pending_requests, found, NULL);

		if (rc == 0) {
			rc = 1;
		}
	}

	return rc;
}
