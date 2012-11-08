/*
 * brickd
 * Copyright (C) 2012 Matthias Bolte <matthias@tinkerforge.com>
 *
 * network.c: Network specific functions
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
#ifndef _WIN32
	#include <unistd.h>
#endif

#include "network.h"

#include "event.h"
#include "log.h"
#include "packet.h"
#include "socket.h"
#include "usb.h" // FIXME
#include "utils.h"

typedef struct {
	EventHandle socket;
	Packet packet;
	int packet_used;
} Client;

static Array _clients = ARRAY_INITIALIZER;
static EventHandle _server_socket = INVALID_EVENT_HANDLE;

static void network_handle_receive(void *opaque) {
	Client *client = opaque;
	int length = socket_receive(client->socket,
	                            (uint8_t *)&client->packet + client->packet_used,
	                            sizeof(Packet) - client->packet_used);
	int i;

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

		event_remove_source(client->socket); // FIXME: handle error?
		socket_destroy(client->socket);

		i = array_find(&_clients, client);

		if (i < 0) {
			log_error("Client not found in client array");
		} else {
			array_remove(&_clients, i, NULL);
		}

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

		log_debug("Got complete packet (uid: %u, length: %u, function_id: %u) from socket (handle: %d)",
		          client->packet.header.uid,
		          client->packet.header.length,
		          client->packet.header.function_id,
		          client->socket);

		usb_dispatch_packet(&client->packet);

		memmove(&client->packet, (uint8_t *)&client->packet + length, client->packet_used - length);
		client->packet_used -= length;
	}
}

static void network_handle_accept(void *opaque) {
	EventHandle client_socket;
	Client *client;

	(void)opaque;

	// accept new client socket
	if (socket_accept(_server_socket, &client_socket, NULL, NULL) < 0) {
		if (!errno_would_block() && !errno_interrupted()) {
			log_error("Could not accept client socket: %s (%d)",
			          get_errno_name(errno), errno);
		}

		// FIXME: does this work in case of blocking or interrupted
		// need to retry on interrupted and blocking should never happend
		return;
	}

	// enable non-blocking
	if (socket_set_non_blocking(client_socket, 1) < 0) {
		log_error("Could not set client socket non-blocking: %s (%d)",
		          get_errno_name(errno), errno);

		socket_destroy(client_socket);

		return;
	}

	// append to client array
	client = array_append(&_clients);

	if (client == NULL) {
		log_error("Could not append to the client array: %s (%d)",
		          get_errno_name(errno), errno);

		socket_destroy(client_socket);

		return;
	}

	client->socket = client_socket;
	client->packet_used = 0;

	// add as event source
	if (event_add_source(client->socket, EVENT_READ,
	                     network_handle_receive, client) < 0) {
		array_remove(&_clients, _clients.count - 1, NULL);
		socket_destroy(client_socket);

		return;
	}

	log_info("Added new client socket (handle: %d)", client->socket);
}

int network_init(void) {
	struct sockaddr_in server_address;

	log_debug("Initializing network subsystem");

	if (array_create(&_clients, 32, sizeof(Client)) < 0) {
		log_error("Could not create client array: %s (%d)",
		          get_errno_name(errno), errno);

		return -1;
	}

	if (socket_create(&_server_socket, AF_INET, SOCK_STREAM, 0) < 0) {
		log_error("Could not create server socket: %s (%d)",
		          get_errno_name(errno), errno);

		// FIXME: free client array
		return -1;
	}

	memset(&server_address, 0, sizeof(server_address));

	server_address.sin_family = AF_INET;
	server_address.sin_addr.s_addr = htonl(INADDR_ANY);
	server_address.sin_port = htons(4223);

	if (socket_bind(_server_socket, (struct sockaddr *)&server_address, sizeof(server_address)) < 0) {
		// FIXME: close socket
		log_error("Could not bind server socket: %s (%d)",
		          get_errno_name(errno), errno);

		// FIXME: free client array
		return -1;
	}

	if (socket_listen(_server_socket, 10) < 0) {
		// FIXME: close socket
		log_error("Could not listen to server socket: %s (%d)",
		          get_errno_name(errno), errno);

		// FIXME: free client array
		return -1;
	}

	if (socket_set_non_blocking(_server_socket, 1) < 0) {
		// FIXME: close socket
		log_error("Could not set server socket non-blocking: %s (%d)",
		          get_errno_name(errno), errno);
		// FIXME: free client array
		return -1;
	}

	if (event_add_source(_server_socket, EVENT_READ, network_handle_accept, NULL) < 0) {
		// FIXME: close socket
		// FIXME: free client array
		return -1;
	}

	return 0;
}

void network_exit(void) {
	log_debug("Shutting down network subsystem");

	// FIXME

	socket_destroy(_server_socket);
}

void network_dispatch_packet(Packet *packet) {
	int i;
	Client *client;

	log_debug("Dispatching packet (uid: %u, length: %u, function_id: %u) to %d client(s)",
	          packet->header.uid, packet->header.length, packet->header.function_id,
	          _clients.count);

	for (i = 0; i < _clients.count; ++i) {
		client = array_get(&_clients, i);

		if (socket_send(client->socket, packet, packet->header.length) < 0) {
			if (errno_would_block()) {
				// FIXME: put data into send queue and enable EVENT_WRITE on
				//        client socket via event_set_events
				continue;
			}

			log_error("Could not send to client socket (handle: %d): %s (%d)",
			          client->socket, get_errno_name(errno), errno);

			continue;
		}
	}
}
