/*
 * brickd
 * Copyright (C) 2012-2013 Matthias Bolte <matthias@tinkerforge.com>
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

#include "log.h"
#include "network.h"
#include "socket.h"
#include "usb.h"

#define LOG_CATEGORY LOG_CATEGORY_NETWORK

#define MAX_PENDING_REQUESTS 256

static const char *_unknown_peer_name = "<unknown>";

static void client_handle_receive(void *opaque) {
	Client *client = opaque;
	const char *message = NULL;
	int length;
	PacketHeader *pending_request;

	length = socket_receive(client->socket,
	                        (uint8_t *)&client->packet + client->packet_used,
	                        sizeof(Packet) - client->packet_used);

	if (length < 0) {
		if (errno_interrupted()) {
			log_debug("Receiving from client (socket: %d, peer: %s), got interrupted",
			          client->socket, client->peer);
		} else {
			log_error("Could not receive from client (socket: %d, peer: %s), disconnecting it: %s (%d)",
			          client->socket, client->peer, get_errno_name(errno), errno);

			network_client_disconnected(client);
		}

		return;
	}

	if (length == 0) {
		log_info("Client (socket: %d, peer: %s) disconnected by peer",
		         client->socket, client->peer);

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

		if (!packet_header_is_valid_request(&client->packet.header, &message)) {
			log_warn("Got invalid request (U: %u, L: %u, F: %u, S: %u, R: %u) from client (socket: %d, peer: %s): %s",
			         client->packet.header.uid,
			         client->packet.header.length,
			         client->packet.header.function_id,
			         client->packet.header.sequence_number,
			         client->packet.header.response_expected,
			         client->socket, client->peer,
			         message);

			if (length < (int)sizeof(PacketHeader)) {
				// skip the complete header if length was too small
				length = sizeof(PacketHeader);
			}
		} else {
			log_debug("Got request (U: %u, L: %u, F: %u, S: %u, R: %u) from client (socket: %d, peer: %s)",
			          client->packet.header.uid,
			          client->packet.header.length,
			          client->packet.header.function_id,
			          client->packet.header.sequence_number,
			          client->packet.header.response_expected,
			          client->socket, client->peer);

			if (client->packet.header.response_expected) {
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

					return;
				}

				memcpy(pending_request, &client->packet.header, sizeof(PacketHeader));

				log_debug("Added pending request (U: %u, L: %u, F: %u, S: %u) for client (socket: %d, peer: %s)",
				          pending_request->uid,
				          pending_request->length,
				          pending_request->function_id,
				          pending_request->sequence_number,
				          client->socket, client->peer);
			}

			usb_dispatch_packet(&client->packet);
		}

		memmove(&client->packet, (uint8_t *)&client->packet + length,
		        client->packet_used - length);

		client->packet_used -= length;
	}
}

int client_create(Client *client, EventHandle socket,
                  struct sockaddr_in *address, socklen_t length) {
	log_debug("Creating client from socket (handle: %d)", socket);

	client->socket = socket;
	client->packet_used = 0;

	// create pending request array
	if (array_create(&client->pending_requests, 32,
	                 sizeof(PacketHeader), 1) < 0) {
		log_error("Could not create pending request array: %s (%d)",
		          get_errno_name(errno), errno);

		return -1;
	}

	// get peer name
	client->peer = resolve_address(address, length);

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
	event_remove_source(client->socket, EVENT_SOURCE_TYPE_GENERIC);
	socket_destroy(client->socket);

	if (client->peer != _unknown_peer_name) {
		free(client->peer);
	}

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
			log_error("Could not send response to client (socket: %d, peer: %s): %s (%d)",
			          client->socket, client->peer, get_errno_name(errno), errno);

			goto cleanup;
		}

		if (force) {
			log_debug("Forced to sent response to client (socket: %d, peer: %s)",
			          client->socket, client->peer);
		} else {
			log_debug("Sent response to client (socket: %d, peer: %s)",
			          client->socket, client->peer);
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
