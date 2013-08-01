/*
 * brickd
 * Copyright (C) 2012-2013 Matthias Bolte <matthias@tinkerforge.com>
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
#include <stdint.h>
#include <string.h>
#ifndef _WIN32
	#include <netdb.h>
	#include <unistd.h>
#endif

#include "network.h"

#include "config.h"
#include "event.h"
#include "log.h"
#include "packet.h"
#include "socket.h"
#include "utils.h"

#define LOG_CATEGORY LOG_CATEGORY_NETWORK

static uint16_t _port = 4223;
static Array _clients = ARRAY_INITIALIZER;
static EventHandle _server_socket = INVALID_EVENT_HANDLE;

static void network_handle_accept(void *opaque) {
	EventHandle client_socket;
	struct sockaddr_in address;
	socklen_t length = sizeof(address);
	Client *client;

	(void)opaque;

	// accept new client socket
	if (socket_accept(_server_socket, &client_socket,
	                  (struct sockaddr *)&address, &length) < 0) {
		if (!errno_interrupted()) {
			log_error("Could not accept new socket: %s (%d)",
			          get_errno_name(errno), errno);
		}

		return;
	}

	// enable non-blocking
	if (socket_set_non_blocking(client_socket, 1) < 0) {
		log_error("Could not enable non-blocking mode for socket (handle: %d): %s (%d)",
		          client_socket, get_errno_name(errno), errno);

		socket_destroy(client_socket);

		return;
	}

	// append to client array
	client = array_append(&_clients);

	if (client == NULL) {
		log_error("Could not append to client array: %s (%d)",
		          get_errno_name(errno), errno);

		socket_destroy(client_socket);

		return;
	}

	if (client_create(client, client_socket, &address, length) < 0) {
		array_remove(&_clients, _clients.count - 1, NULL);
		socket_destroy(client_socket);

		return;
	}

	log_info("Added new client (socket: %d, peer: %s)",
	         client->socket, client->peer);
}

int network_init(void) {
	int phase = 0;
	const char *listen_address = config_get_listen_address();
	struct hostent *entry;
	struct sockaddr_in server_address;

	log_debug("Initializing network subsystem");

	_port = config_get_listen_port();

	// the Client struct is not relocatable, because it is passed by reference
	// as opaque parameter to the event subsystem
	if (array_create(&_clients, 32, sizeof(Client), 0) < 0) {
		log_error("Could not create client array: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 1;

	if (socket_create(&_server_socket, AF_INET, SOCK_STREAM, 0) < 0) {
		log_error("Could not create server socket: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 2;

	// FIXME: use this for debugging purpose only
	if (socket_set_address_reuse(_server_socket, 1) < 0) {
		log_error("Could not enable address-reuse mode for server socket: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	memset(&server_address, 0, sizeof(server_address));

	server_address.sin_family = AF_INET;
	server_address.sin_port = htons(_port);

	entry = gethostbyname(listen_address);

	if (entry == NULL) {
		log_error("Could not resolve listen address '%s'", listen_address);

		goto cleanup;
	}

	memcpy(&server_address.sin_addr, entry->h_addr_list[0], entry->h_length);

	if (socket_bind(_server_socket, (struct sockaddr *)&server_address,
	                sizeof(server_address)) < 0) {
		log_error("Could not bind server socket to '%s' on port %u: %s (%d)",
		          listen_address, _port, get_errno_name(errno), errno);

		goto cleanup;
	}

	if (socket_listen(_server_socket, 10) < 0) {
		log_error("Could not listen to server socket bound to '%s' on port %u: %s (%d)",
		          listen_address, _port, get_errno_name(errno), errno);

		goto cleanup;
	}

	log_debug("Started listening to '%s' on port %u", listen_address, _port);

	if (socket_set_non_blocking(_server_socket, 1) < 0) {
		log_error("Could not enable non-blocking mode for server socket: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	if (event_add_source(_server_socket, EVENT_SOURCE_TYPE_GENERIC, EVENT_READ,
	                     network_handle_accept, NULL) < 0) {
		goto cleanup;
	}

	phase = 3;

cleanup:
	switch (phase) { // no breaks, all cases fall through intentionally
	case 2:
		socket_destroy(_server_socket);

	case 1:
		array_destroy(&_clients, (FreeFunction)client_destroy);

	default:
		break;
	}

	return phase == 3 ? 0 : -1;
}

void network_exit(void) {
	log_debug("Shutting down network subsystem");

	network_cleanup_clients();

	array_destroy(&_clients, (FreeFunction)client_destroy);

	event_remove_source(_server_socket, EVENT_SOURCE_TYPE_GENERIC);

	socket_destroy(_server_socket);
}

void network_client_disconnected(Client *client) {
	int i = array_find(&_clients, client);

	if (i < 0) {
		log_error("Client (socket: %d, peer: %s) not found in client array",
		          client->socket, client->peer);
	} else {
		array_remove(&_clients, i, (FreeFunction)client_destroy);
	}
}

// remove clients that got marked as disconnected
void network_cleanup_clients(void) {
	int i;
	Client *client;

	// iterate backwards for simpler index handling
	for (i = _clients.count - 1; i >= 0; --i) {
		client = array_get(&_clients, i);

		if (client->disconnected) {
			log_debug("Removing disconnected client (socket: %d, peer: %s)",
			          client->socket, client->peer);

			array_remove(&_clients, i, (FreeFunction)client_destroy);
		}
	}
}

void network_dispatch_packet(Packet *packet) {
	char base58[MAX_BASE58_STR_SIZE];
	int i;
	Client *client;
	int rc;
	int dispatched = 0;

	if (_clients.count == 0) {
		if (packet_header_get_sequence_number(&packet->header) == 0) {
			log_debug("No clients connected, dropping %scallback (U: %s, L: %u, F: %u)",
			          packet_get_callback_type(packet),
			          base58_encode(base58, uint32_from_le(packet->header.uid)),
			          packet->header.length,
			          packet->header.function_id);
		} else {
			log_debug("No clients connected, dropping response (U: %s, L: %u, F: %u, S: %u, E: %u)",
			          base58_encode(base58, uint32_from_le(packet->header.uid)),
			          packet->header.length,
			          packet->header.function_id,
			          packet_header_get_sequence_number(&packet->header),
			          packet_header_get_error_code(&packet->header));
		}

		return;
	}

	if (packet_header_get_sequence_number(&packet->header) == 0) {
		log_debug("Broadcasting %scallback (U: %s, L: %u, F: %u) to %d client(s)",
		          packet_get_callback_type(packet),
		          base58_encode(base58, uint32_from_le(packet->header.uid)),
		          packet->header.length,
		          packet->header.function_id,
		          _clients.count);

		for (i = 0; i < _clients.count; ++i) {
			client = array_get(&_clients, i);

			client_dispatch_packet(client, packet, 1);
		}
	} else {
		log_debug("Dispatching response (U: %s, L: %u, F: %u, S: %u, E: %u) to %d client(s)",
		          base58_encode(base58, uint32_from_le(packet->header.uid)),
		          packet->header.length,
		          packet->header.function_id,
		          packet_header_get_sequence_number(&packet->header),
		          packet_header_get_error_code(&packet->header),
		          _clients.count);

		for (i = 0; i < _clients.count; ++i) {
			client = array_get(&_clients, i);

			rc = client_dispatch_packet(client, packet, 0);

			if (rc < 0) {
				continue;
			} else if (rc > 0) {
				dispatched = 1;
			}
		}

		if (dispatched) {
			return;
		}

		log_warn("Broadcasting response because no client has a matching pending request");

		for (i = 0; i < _clients.count; ++i) {
			client = array_get(&_clients, i);

			client_dispatch_packet(client, packet, 1);
		}
	}
}
