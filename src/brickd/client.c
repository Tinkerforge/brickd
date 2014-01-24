/*
 * brickd
 * Copyright (C) 2012-2014 Matthias Bolte <matthias@tinkerforge.com>
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
#include <stdlib.h>
#include <string.h>

#include "client.h"

#include "array.h"
#include "hardware.h"
#include "log.h"
#include "socket.h"
#include "utils.h"

#define LOG_CATEGORY LOG_CATEGORY_NETWORK

#define MAX_PENDING_REQUESTS 256

static const char *_unknown_peer_name = "<unknown>";

typedef struct {
	PacketHeader header;
#ifdef BRICKD_WITH_PROFILING
	uint64_t arrival_time; // in usec
#endif
} PendingRequest;

static void client_handle_receive(void *opaque) {
	Client *client = opaque;
	int length;
	const char *message = NULL;
	char base58[MAX_BASE58_STR_SIZE];
	PendingRequest *pending_request;

	length = socket_receive(client->socket,
	                        (uint8_t *)&client->request + client->request_used,
	                        sizeof(Packet) - client->request_used);

	if (length < 0) {
		if (errno_interrupted()) {
			log_debug("Receiving from client (socket: %d, peer: %s) was interrupted, retrying",
			          client->socket, client->peer);
		} else if (errno_would_block()) {
			log_debug("Receiving from client (socket: %d, peer: %s) would block, retrying",
			          client->socket, client->peer);
		} else {
			log_error("Could not receive from client (socket: %d, peer: %s), disconnecting it: %s (%d)",
			          client->socket, client->peer, get_errno_name(errno), errno);

			client->disconnected = 1;
		}

		return;
	}

	if (length == 0) {
		log_info("Client (socket: %d, peer: %s) disconnected by peer",
		         client->socket, client->peer);

		client->disconnected = 1;

		return;
	}

	client->request_used += length;

	while (client->request_used > 0) {
		if (client->request_used < (int)sizeof(PacketHeader)) {
			// wait for complete header
			break;
		}

		if (!client->request_header_checked) {
			if (!packet_header_is_valid_request(&client->request.header, &message)) {
				log_error("Got invalid request (U: %s, L: %u, F: %u, S: %u, R: %u) from client (socket: %d, peer: %s), disconnecting it: %s",
				          base58_encode(base58, uint32_from_le(client->request.header.uid)),
				          client->request.header.length,
				          client->request.header.function_id,
				          packet_header_get_sequence_number(&client->request.header),
				          packet_header_get_response_expected(&client->request.header),
				          client->socket, client->peer,
				          message);

				client->disconnected = 1;

				return;
			}

			client->request_header_checked = 1;
		}

		length = client->request.header.length;

		if (client->request_used < length) {
			// wait for complete packet
			break;
		}

		if (client->request.header.function_id == FUNCTION_DISCONNECT_PROBE) {
			log_debug("Got disconnect probe from client (socket: %d, peer: %s), dropping it",
			          client->socket, client->peer);
		} else {
			log_debug("Got request (U: %s, L: %u, F: %u, S: %u, R: %u) from client (socket: %d, peer: %s)",
			          base58_encode(base58, uint32_from_le(client->request.header.uid)),
			          client->request.header.length,
			          client->request.header.function_id,
			          packet_header_get_sequence_number(&client->request.header),
			          packet_header_get_response_expected(&client->request.header),
			          client->socket, client->peer);

			if (packet_header_get_response_expected(&client->request.header)) {
				if (client->pending_requests.count >= MAX_PENDING_REQUESTS) {
					log_warn("Dropping %d items from pending request array of client (socket: %d, peer: %s)",
					         client->pending_requests.count - MAX_PENDING_REQUESTS + 1,
					         client->socket, client->peer);

					while (client->pending_requests.count >= MAX_PENDING_REQUESTS) {
						array_remove(&client->pending_requests, 0, NULL);
					}
				}

				pending_request = array_append(&client->pending_requests);

				if (pending_request == NULL) {
					log_error("Could not append to pending request array: %s (%d)",
					          get_errno_name(errno), errno);
				} else {
					memcpy(&pending_request->header, &client->request.header,
					       sizeof(PacketHeader));

#ifdef BRICKD_WITH_PROFILING
					pending_request->arrival_time = microseconds();
#endif

					log_debug("Added pending request (U: %s, L: %u, F: %u, S: %u) for client (socket: %d, peer: %s)",
					          base58_encode(base58, uint32_from_le(pending_request->header.uid)),
					          pending_request->header.length,
					          pending_request->header.function_id,
					          packet_header_get_sequence_number(&pending_request->header),
					          client->socket, client->peer);
				}
			}

			hardware_dispatch_request(&client->request);
		}

		memmove(&client->request, (uint8_t *)&client->request + length,
		        client->request_used - length);

		client->request_used -= length;
		client->request_header_checked = 0;
	}
}

int client_create(Client *client, EventHandle socket,
                  struct sockaddr *address, socklen_t length) {
	log_debug("Creating client from socket (handle: %d)", socket);

	client->socket = socket;
	client->disconnected = 0;
	client->request_used = 0;
	client->request_header_checked = 0;

	// create pending request array
	if (array_create(&client->pending_requests, 32,
	                 sizeof(PendingRequest), 1) < 0) {
		log_error("Could not create pending request array: %s (%d)",
		          get_errno_name(errno), errno);

		return -1;
	}

	// get peer name
	client->peer = socket_address_to_hostname(address, length);

	if (client->peer == NULL) {
		log_warn("Could not get peer name of client (socket: %d): %s (%d)",
		         socket, get_errno_name(errno), errno);

		client->peer = (char *)_unknown_peer_name;
	}

	// add socket as event source
	if (event_add_source(client->socket, EVENT_SOURCE_TYPE_GENERIC, EVENT_READ,
	                     client_handle_receive, client) < 0) {
		if (client->peer != _unknown_peer_name) {
			free(client->peer);
		}

		array_destroy(&client->pending_requests, NULL);

		return -1;
	}

	return 0;
}

void client_destroy(Client *client) {
	if (client->pending_requests.count > 0) {
		log_warn("Destroying client (socket: %d, peer: %s) while %d request(s) are still pending",
		         client->socket, client->peer, client->pending_requests.count);
	}

	event_remove_source(client->socket, EVENT_SOURCE_TYPE_GENERIC);
	socket_destroy(client->socket);

	if (client->peer != _unknown_peer_name) {
		free(client->peer);
	}

	array_destroy(&client->pending_requests, NULL);
}

// returns -1 on error, 0 if the response was not dispatched and 1 if it was dispatch
int client_dispatch_response(Client *client, Packet *response, int force) {
	int i;
	PendingRequest *pending_request = NULL;
	int found = -1;
	int rc = -1;
#ifdef BRICKD_WITH_PROFILING
	uint64_t elapsed;
#endif

	if (client->disconnected) {
		log_debug("Ignoring disconnected client (socket: %d, peer: %s)",
		          client->socket, client->peer);

		return 0;
	}

	if (!force) {
		for (i = 0; i < client->pending_requests.count; ++i) {
			pending_request = array_get(&client->pending_requests, i);

			if (packet_is_matching_response(response, &pending_request->header)) {
				found = i;

				break;
			}
		}
	}

	if (force || found >= 0) {
		if (socket_send(client->socket, response, response->header.length) < 0) {
			log_error("Could not send response to client (socket: %d, peer: %s), disconnecting it: %s (%d)",
			          client->socket, client->peer, get_errno_name(errno), errno);

			client->disconnected = 1;

			goto cleanup;
		}

		if (force) {
			log_debug("Forced to sent response to client (socket: %d, peer: %s)",
			          client->socket, client->peer);
		} else {
#ifdef BRICKD_WITH_PROFILING
			elapsed = microseconds() - pending_request->arrival_time;

			log_debug("Sent response to client (socket: %d, peer: %s), was requested %u.%03u msec ago, %d request(s) still pending",
			          client->socket, client->peer,
			          (unsigned int)(elapsed / 1000), (unsigned int)(elapsed % 1000),
			          client->pending_requests.count - 1);
#else
			log_debug("Sent response to client (socket: %d, peer: %s), %d request(s) still pending",
			          client->socket, client->peer,
			          client->pending_requests.count - 1);
#endif
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
