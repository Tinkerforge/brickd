/*
 * brickd
 * Copyright (C) 2012-2014, 2016-2017 Matthias Bolte <matthias@tinkerforge.com>
 * Copyright (C) 2014 Olaf Lüke <olaf@tinkerforge.com>
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
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
	#include <netdb.h>
	#include <unistd.h>
#endif

#include <daemonlib/array.h>
#include <daemonlib/base58.h>
#include <daemonlib/config.h>
#include <daemonlib/event.h>
#include <daemonlib/log.h>
#include <daemonlib/node.h>
#include <daemonlib/packet.h>
#include <daemonlib/socket.h>
#include <daemonlib/utils.h>

#include "network.h"

#include "hmac.h"
#include "websocket.h"
#include "zombie.h"

static LogSource _log_source = LOG_SOURCE_INITIALIZER;

static Array _clients;
static Array _zombies;
static Socket _plain_server_socket;
static bool _plain_server_socket_open = false;
static Socket _websocket_server_socket;
static bool _websocket_server_socket_open = false;
static uint32_t _next_authentication_nonce = 0;
static Node _pending_request_sentinel;

static void network_handle_accept(void *opaque) {
	Socket *server_socket = opaque;
	Socket *client_socket;
	struct sockaddr_storage address;
	socklen_t length = sizeof(address);
	char hostname[NI_MAXHOST];
	char port[NI_MAXSERV];
	char buffer[NI_MAXHOST + NI_MAXSERV + 4]; // 4 == strlen("[]:") + 1
	char *name = "<unknown>";
	Client *client;

	// accept new client socket
	client_socket = socket_accept(server_socket, (struct sockaddr *)&address, &length);

	if (client_socket == NULL) {
		if (!errno_interrupted()) {
			log_error("Could not accept new client socket: %s (%d)",
			          get_errno_name(errno), errno);
		}

		return;
	}

	if (socket_address_to_hostname((struct sockaddr *)&address, length,
	                               hostname, sizeof(hostname),
	                               port, sizeof(port)) < 0) {
		log_warn("Could not get hostname and port of client (socket: %d): %s (%d)",
		         client_socket->handle, get_errno_name(errno), errno);
	} else {
		if (address.ss_family == AF_INET6) {
			snprintf(buffer, sizeof(buffer), "[%s]:%s", hostname, port);
		} else {
			snprintf(buffer, sizeof(buffer), "%s:%s", hostname, port);
		}

		name = buffer;
	}

	// create new client
	client = network_create_client(name, &client_socket->base);

	if (client == NULL) {
		socket_destroy(client_socket);
		free(client_socket);

		return;
	}

#ifdef BRICKD_WITH_RED_BRICK
	client_send_red_brick_enumerate(client, ENUMERATION_TYPE_CONNECTED);
#endif
}

static int network_open_server_socket(Socket *socket, uint16_t port,
                                      SocketCreateAllocatedFunction create_allocated) {
	const char *address = config_get_option_value("listen.address")->string;
	bool dual_stack = config_get_option_value("listen.dual_stack")->boolean;

	if (socket_open_server(socket, address, port, dual_stack, create_allocated) < 0) {
		return -1;
	}

	if (event_add_source(socket->handle, EVENT_SOURCE_TYPE_GENERIC,
	                     EVENT_READ, network_handle_accept, socket) < 0) {
		socket_destroy(socket);

		return -1;
	}

	return 0;
}

// drop all pending requests for the given UID from the global list
static void network_drop_pending_requests(uint32_t uid) {
	Node *pending_request_global_node = _pending_request_sentinel.next;
	Node *pending_request_global_node_next;
	PendingRequest *pending_request;
	char base58[BASE58_MAX_LENGTH];
	int count = 0;

	while (pending_request_global_node != &_pending_request_sentinel) {
		pending_request = containerof(pending_request_global_node,
		                              PendingRequest, global_node);
		pending_request_global_node_next = pending_request_global_node->next;

		if (pending_request->header.uid == uid) {
			pending_request_remove_and_free(pending_request);

			++count;
		}

		pending_request_global_node = pending_request_global_node_next;
	}

	if (count > 0) {
		log_warn("Dropped %d pending request(s) (uid: %s)",
		         count, base58_encode(base58, uint32_from_le(uid)));
	}
}

int network_init(void) {
	uint16_t plain_port = (uint16_t)config_get_option_value("listen.plain_port")->integer;
	uint16_t websocket_port = (uint16_t)config_get_option_value("listen.websocket_port")->integer;

	log_debug("Initializing network subsystem");

	node_reset(&_pending_request_sentinel);

	if (config_get_option_value("authentication.secret")->string != NULL) {
		log_info("Authentication is enabled");

		_next_authentication_nonce = get_random_uint32();
	}

	// create client array. the Client struct is not relocatable, because a
	// pointer to it is passed as opaque parameter to the event subsystem
	if (array_create(&_clients, 32, sizeof(Client), false) < 0) {
		log_error("Could not create client array: %s (%d)",
		          get_errno_name(errno), errno);

		return -1;
	}

	// create zombie array. the Zombie struct is not relocatable, because a
	// pointer to it is passed as opaque parameter to its timer object
	if (array_create(&_zombies, 32, sizeof(Zombie), false) < 0) {
		log_error("Could not create zombie array: %s (%d)",
		          get_errno_name(errno), errno);

		array_destroy(&_clients, (ItemDestroyFunction)client_destroy);

		return -1;
	}

	if (network_open_server_socket(&_plain_server_socket, plain_port,
	                               socket_create_allocated) >= 0) {
		_plain_server_socket_open = true;
	}

	if (websocket_port != 0) {
		if (config_get_option_value("authentication.secret")->string == NULL) {
			log_warn("WebSocket support is enabled without authentication");
		}

		if (network_open_server_socket(&_websocket_server_socket, websocket_port,
		                               websocket_create_allocated) >= 0) {
			_websocket_server_socket_open = true;
		}
	}

	if (!_plain_server_socket_open && !_websocket_server_socket_open) {
		log_error("Could not open any socket to listen to");

		array_destroy(&_zombies, (ItemDestroyFunction)zombie_destroy);
		array_destroy(&_clients, (ItemDestroyFunction)client_destroy);

		return -1;
	}

	return 0;
}

void network_exit(void) {
	log_debug("Shutting down network subsystem");

	array_destroy(&_clients, (ItemDestroyFunction)client_destroy); // might call network_create_zombie
	array_destroy(&_zombies, (ItemDestroyFunction)zombie_destroy);

	if (_plain_server_socket_open) {
		event_remove_source(_plain_server_socket.handle, EVENT_SOURCE_TYPE_GENERIC);
		socket_destroy(&_plain_server_socket);
	}

	if (_websocket_server_socket_open) {
		event_remove_source(_websocket_server_socket.handle, EVENT_SOURCE_TYPE_GENERIC);
		socket_destroy(&_websocket_server_socket);
	}
}

Client *network_create_client(const char *name, IO *io) {
	Client *client;

	// append to client array
	client = array_append(&_clients);

	if (client == NULL) {
		log_error("Could not append to client array: %s (%d)",
		          get_errno_name(errno), errno);

		return NULL;
	}

	// create new client that takes ownership of the I/O object
	if (client_create(client, name, io, _next_authentication_nonce++, NULL) < 0) {
		array_remove(&_clients, _clients.count - 1, NULL);

		return NULL;
	}

	log_info("Added new client ("CLIENT_SIGNATURE_FORMAT")",
	         client_expand_signature(client));

	return client;
}

int network_create_zombie(Client *client) {
	Zombie *zombie;

	// append to zombie array
	zombie = array_append(&_zombies);

	if (zombie == NULL) {
		log_error("Could not append to zombie array: %s (%d)",
		          get_errno_name(errno), errno);

		return -1;
	}

	// create new zombie that takes ownership of the pending requests
	if (zombie_create(zombie, client) < 0) {
		array_remove(&_zombies, _zombies.count - 1, NULL);

		return -1;
	}

	log_debug("Added new zombie (id: %u)", zombie->id);

	return 0;
}

// remove clients that got marked as disconnected and finished zombies
void network_cleanup_clients_and_zombies(void) {
	int i;
	Client *client;
	Zombie *zombie;

	// iterate backwards for simpler index handling
	for (i = _clients.count - 1; i >= 0; --i) {
		client = array_get(&_clients, i);

		if (client->disconnected) {
			log_debug("Removing disconnected client ("CLIENT_SIGNATURE_FORMAT")",
			          client_expand_signature(client));

			array_remove(&_clients, i, (ItemDestroyFunction)client_destroy);
		}
	}

	// iterate backwards for simpler index handling
	for (i = _zombies.count - 1; i >= 0; --i) {
		zombie = array_get(&_zombies, i);

		if (zombie->finished) {
			log_debug("Removing finished zombie (id: %u)", zombie->id);

			array_remove(&_zombies, i, (ItemDestroyFunction)zombie_destroy);
		}
	}
}

void network_client_expects_response(Client *client, Packet *request) {
	PendingRequest *pending_request;
	char packet_signature[PACKET_MAX_SIGNATURE_LENGTH];

	if (client->pending_request_count >= CLIENT_MAX_PENDING_REQUESTS) {
		log_warn("Pending requests list for client ("CLIENT_SIGNATURE_FORMAT") is full, dropping %d pending request(s)",
		         client_expand_signature(client),
		         client->pending_request_count - CLIENT_MAX_PENDING_REQUESTS + 1);

		while (client->pending_request_count >= CLIENT_MAX_PENDING_REQUESTS) {
			pending_request = containerof(client->pending_request_sentinel.next,
			                              PendingRequest, client_node);

			pending_request_remove_and_free(pending_request);
		}
	}

	pending_request = calloc(1, sizeof(PendingRequest));

	if (pending_request == NULL) {
		log_error("Could not allocate pending request: %s (%d)",
		          get_errno_name(ENOMEM), ENOMEM);

		return;
	}

	node_insert_before(&_pending_request_sentinel, &pending_request->global_node);
	node_insert_before(&client->pending_request_sentinel, &pending_request->client_node);

	++client->pending_request_count;

	pending_request->client = client;
	pending_request->zombie = NULL;

	memcpy(&pending_request->header, &request->header, sizeof(PacketHeader));

	log_packet_debug("Added pending request (%s) for client ("CLIENT_SIGNATURE_FORMAT")",
	                 packet_get_request_signature(packet_signature, request),
	                 client_expand_signature(client));
}

void network_dispatch_response(Packet *response) {
	EnumerateCallback *enumerate_callback;
	char packet_signature[PACKET_MAX_SIGNATURE_LENGTH];
	int i;
	Client *client;
	Node *pending_request_global_node;
	PendingRequest *pending_request;

	packet_add_trace(response);

	if (packet_header_get_sequence_number(&response->header) == 0) {
		if (response->header.function_id == CALLBACK_ENUMERATE) {
			enumerate_callback = (EnumerateCallback *)response;

			if (enumerate_callback->enumeration_type == ENUMERATION_TYPE_CONNECTED ||
			    enumerate_callback->enumeration_type == ENUMERATION_TYPE_DISCONNECTED) {
				network_drop_pending_requests(response->header.uid);
			}
		}

		if (_clients.count == 0) {
			log_packet_debug("No clients connected, dropping %s (%s)",
			                 packet_get_response_type(response),
			                 packet_get_response_signature(packet_signature, response));

			return;
		}

		log_packet_debug("Broadcasting %s (%s) to %d client(s)",
		                 packet_get_response_type(response),
		                 packet_get_response_signature(packet_signature, response),
		                 _clients.count);

		packet_add_trace(response);

		for (i = 0; i < _clients.count; ++i) {
			client = array_get(&_clients, i);

			client_dispatch_response(client, NULL, response, true, false);
		}
	} else if (_clients.count + _zombies.count > 0) {
		log_packet_debug("Dispatching response (%s) to %d client(s) and %d zombies(s)",
		                 packet_get_response_signature(packet_signature, response),
		                 _clients.count, _zombies.count);

		pending_request_global_node = _pending_request_sentinel.next;

		while (pending_request_global_node != &_pending_request_sentinel) {
			pending_request = containerof(pending_request_global_node,
			                              PendingRequest, global_node);

			if (packet_is_matching_response(response, &pending_request->header)) {
				if (pending_request->client != NULL) {
					packet_add_trace(response);
					client_dispatch_response(pending_request->client, pending_request,
					                         response, false, false);
				} else {
					packet_add_trace(response);
					zombie_dispatch_response(pending_request->zombie, pending_request,
					                         response);
				}

				return;
			}

			pending_request_global_node = pending_request_global_node->next;
		}

		log_warn("Broadcasting response (%s) because no client/zombie has a matching pending request",
		         packet_get_response_signature(packet_signature, response));

		packet_add_trace(response);

		for (i = 0; i < _clients.count; ++i) {
			client = array_get(&_clients, i);

			client_dispatch_response(client, NULL, response, true, false);
		}
	} else {
		log_packet_debug("No clients/zombies connected, dropping response (%s)",
		                 packet_get_response_signature(packet_signature, response));

		packet_add_trace(response);
	}
}

#ifdef BRICKD_WITH_RED_BRICK

void network_announce_red_brick_disconnect(void) {
	int i;
	Client *client;

	log_debug("Broadcasting enumerate-disconnected callback for RED Brick to %d client(s)",
	          _clients.count);

	for (i = 0; i < _clients.count; ++i) {
		client = array_get(&_clients, i);

		client_send_red_brick_enumerate(client, ENUMERATION_TYPE_DISCONNECTED);
	}
}

#endif
