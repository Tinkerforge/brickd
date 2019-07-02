/*
 * brickd
 * Copyright (C) 2016 Ishraq Ibne Ashraf <ishraq@tinkerforge.com>
 * Copyright (C) 2017-2019 Matthias Bolte <matthias@tinkerforge.com>
 *
 * mesh.c: Mesh specific functions
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
#include <daemonlib/config.h>
#include <daemonlib/event.h>
#include <daemonlib/log.h>

#include "mesh.h"

#include "mesh_stack.h"

Array mesh_stacks;

static LogSource _log_source = LOG_SOURCE_INITIALIZER;
static Array _server_sockets;

static void mesh_destroy_server_socket(Socket *server_socket) {
	event_remove_source(server_socket->handle, EVENT_SOURCE_TYPE_GENERIC);
	socket_destroy(server_socket);
}

int mesh_init(void) {
	log_debug("Initializing mesh subsystem");

	if (array_create(&mesh_stacks, MAX_MESH_STACKS, sizeof(MeshStack), false) < 0) {
		log_error("Failed to create mesh stack array: %s (%d)",
		          get_errno_name(errno), errno);

		return -1;
	}

	if (mesh_start_listening() < 0) {
		log_error("Failed to open mesh listen socket");

		array_destroy(&mesh_stacks, (ItemDestroyFunction)mesh_stack_destroy);

		return -1;
	}

	return 0;
}

void mesh_exit(void) {
	log_debug("Shutting down mesh subsystem");

	array_destroy(&_server_sockets, (ItemDestroyFunction)mesh_destroy_server_socket);
	array_destroy(&mesh_stacks, (ItemDestroyFunction)mesh_stack_destroy);
}

void mesh_handle_accept(void *opaque) {
	Socket *server_socket = opaque;
	Socket *client_socket;
	struct sockaddr_storage address;
	socklen_t length = sizeof(address);
	char hostname[NI_MAXHOST];
	char port[NI_MAXSERV];
	char buffer[NI_MAXHOST + NI_MAXSERV + 4]; // 4 == strlen("[]:") + 1
	char *name = "<unknown>";

	(void)opaque;

	log_info("New connection on mesh port");

	// Accept new mesh client socket.
	client_socket = socket_accept(server_socket, (struct sockaddr *)&address, &length);

	if (client_socket == NULL) {
		if (!errno_interrupted()) {
			log_error("Failed to accept new mesh client connection: %s (%d)",
			          get_errno_name(errno), errno);
		}

		return;
	}

	if (socket_address_to_hostname((struct sockaddr *)&address,
	                               length,
	                               hostname,
	                               sizeof(hostname),
	                               port,
	                               sizeof(port)) < 0) {
		log_warn("Could not get hostname and port of mesh client (socket: %d): %s (%d)",
		         client_socket->handle, get_errno_name(errno), errno);
	} else {
		snprintf(buffer, sizeof(buffer), "%s:%s", hostname, port);

		name = buffer;
	}

	/*
	 * Allocate and initialise a new mesh stack. Note that in this stage the stack
	 * is not added to brickd's central list of stacks yet.
	 */
	if (mesh_stack_create(name, client_socket) < 0) {
		log_error("Could not create new mesh stack");
	} else {
		log_info("New mesh stack created");
	}
}

int mesh_start_listening(void) {
	const char *address = config_get_option_value("listen.address")->string;
	uint16_t port = (uint16_t)config_get_option_value("listen.mesh_gateway_port")->integer;
	bool dual_stack = config_get_option_value("listen.dual_stack")->boolean;
	int i;
	Socket *server_socket;

	// create server socket array. the Socket struct is not relocatable, because a
	// pointer to it is passed as opaque parameter to accept function
	if (array_create(&_server_sockets, 8, sizeof(Socket), false) < 0) {
		log_error("Could not create plain server socket array: %s (%d)",
		          get_errno_name(errno), errno);

		return -1;
	}

	socket_open_server(&_server_sockets, address, port, dual_stack, socket_create_allocated);

	for (i = 0; i < _server_sockets.count; ++i) {
		server_socket = array_get(&_server_sockets, i);

		if (event_add_source(server_socket->handle, EVENT_SOURCE_TYPE_GENERIC, "mesh-server",
		                     EVENT_READ, mesh_handle_accept, server_socket) < 0) {
			break;
		}
	}

	if (i < _server_sockets.count) {
		for (--i; i >= 0; --i) {
			server_socket = array_get(&_server_sockets, i);

			event_remove_source(server_socket->handle, EVENT_SOURCE_TYPE_GENERIC);
		}

		for (i = 0; i < _server_sockets.count; ++i) {
			array_remove(&_server_sockets, i, (ItemDestroyFunction)socket_destroy);
		}
	}

	return _server_sockets.count > 0 ? 0 : -1;
}

void mesh_cleanup_stacks(void) {
	int i;
	MeshStack *mesh_stack;

	// iterate backwards for simpler index handling
	for (i = mesh_stacks.count - 1; i >= 0; --i) {
		mesh_stack = array_get(&mesh_stacks, i);

		if (mesh_stack->cleanup) {
			log_debug("Removing mesh stack, %s", mesh_stack->name);
			array_remove(&mesh_stacks, i, (ItemDestroyFunction)mesh_stack_destroy);
		}
	}
}
