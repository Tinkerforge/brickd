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

#include "array.h"
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
	struct sockaddr_storage address;
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

	if (client_create(client, client_socket, (struct sockaddr *)&address, length) < 0) {
		array_remove(&_clients, _clients.count - 1, NULL);
		socket_destroy(client_socket);

		return;
	}

	log_info("Added new client (socket: %d, peer: %s)",
	         client->socket, client->peer);
}

static const char *network_get_address_family_name(int family, int report_dual_stack) {
	switch (family) {
	case AF_INET:
		return "IPv4";

	case AF_INET6:
		if (report_dual_stack && config_get_listen_dual_stack()) {
			return "IPv6 dual-stack";
		} else {
			return "IPv6";
		}

	default:
		return "<unknown>";
	}
}

int network_init(void) {
	int phase = 0;
	const char *listen_address = config_get_listen_address();
	struct addrinfo *resolved_address = NULL;

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

	// resolve listen address
	// FIXME: bind to all returned addresses, instead of just the first one.
	//        requires special handling if IPv4 and IPv6 addresses are returned
	//        and dual-stack mode is enabled
	resolved_address = socket_hostname_to_address(listen_address, _port);

	if (resolved_address == NULL) {
		log_error("Could not resolve listen address '%s' (port: %u): %s (%d)",
		          listen_address, _port, get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 2;

	// create socket
	if (socket_create(&_server_socket, resolved_address->ai_family,
	                  resolved_address->ai_socktype, resolved_address->ai_protocol) < 0) {
		log_error("Could not create %s server socket: %s (%d)",
		          network_get_address_family_name(resolved_address->ai_family, 0),
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 3;

	if (resolved_address->ai_family == AF_INET6) {
		if (socket_set_dual_stack(_server_socket, config_get_listen_dual_stack()) < 0) {
			log_error("Could not %s dual-stack mode for IPv6 server socket: %s (%d)",
			          config_get_listen_dual_stack() ? "enable" : "disable",
			          get_errno_name(errno), errno);

			goto cleanup;
		}
	}

#ifndef _WIN32
	// on Unix the SO_REUSEADDR socket option allows to rebind sockets in
	// CLOSE-WAIT state. this is a desired effect. on Windows SO_REUSEADDR
	// allows to rebind sockets in any state. this is dangerous. therefore,
	// don't set SO_REUSEADDR on Windows. sockets can be rebound in CLOSE-WAIT
	// state on Windows by default.
	if (socket_set_address_reuse(_server_socket, 1) < 0) {
		log_error("Could not enable address-reuse mode for server socket: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}
#endif

	// bind socket and start to listen
	if (socket_bind(_server_socket, resolved_address->ai_addr,
	                resolved_address->ai_addrlen) < 0) {
		log_error("Could not bind %s server socket to '%s' on port %u: %s (%d)",
		          network_get_address_family_name(resolved_address->ai_family, 1),
		          listen_address, _port, get_errno_name(errno), errno);

		goto cleanup;
	}

	if (socket_listen(_server_socket, 10) < 0) {
		log_error("Could not listen to %s server socket bound to '%s' on port %u: %s (%d)",
		          network_get_address_family_name(resolved_address->ai_family, 1),
		          listen_address, _port, get_errno_name(errno), errno);

		goto cleanup;
	}

	log_debug("Started listening to '%s' (%s) on port %u",
	          listen_address,
	          network_get_address_family_name(resolved_address->ai_family, 1),
	          _port);

	if (socket_set_non_blocking(_server_socket, 1) < 0) {
		log_error("Could not enable non-blocking mode for server socket: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	if (event_add_source(_server_socket, EVENT_SOURCE_TYPE_GENERIC, EVENT_READ,
	                     network_handle_accept, NULL) < 0) {
		goto cleanup;
	}

	phase = 4;

cleanup:
	switch (phase) { // no breaks, all cases fall through intentionally
	case 3:
		socket_destroy(_server_socket);

	case 2:
		freeaddrinfo(resolved_address);

	case 1:
		array_destroy(&_clients, (FreeFunction)client_destroy);

	default:
		break;
	}

	return phase == 4 ? 0 : -1;
}

void network_exit(void) {
	log_debug("Shutting down network subsystem");

	network_cleanup_clients();

	array_destroy(&_clients, (FreeFunction)client_destroy);

	event_remove_source(_server_socket, EVENT_SOURCE_TYPE_GENERIC);

	socket_destroy(_server_socket);
}

void network_client_disconnected(Client *client) {
	int i;
	Client *item;

	for (i = 0; i < _clients.count; ++i) {
		item = array_get(&_clients, i);

		if (item->socket == client->socket) {
			array_remove(&_clients, i, (FreeFunction)client_destroy);
			return;
		}
	}

	log_error("Client (socket: %d, peer: %s) not found in client array",
	          client->socket, client->peer);
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
