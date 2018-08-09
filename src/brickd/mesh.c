/*
 * brickd
 * Copyright (C) 2016 Ishraq Ibne Ashraf <ishraq@tinkerforge.com>
 * Copyright (C) 2017-2018 Matthias Bolte <matthias@tinkerforge.com>
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

static Socket mesh_listen_socket;
static LogSource _log_source = LOG_SOURCE_INITIALIZER;

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

	event_remove_source(mesh_listen_socket.handle, EVENT_SOURCE_TYPE_GENERIC);
	socket_destroy(&mesh_listen_socket);

	array_destroy(&mesh_stacks, (ItemDestroyFunction)mesh_stack_destroy);
}

void mesh_handle_accept(void *opaque) {
	char port[NI_MAXSERV];
	char *name = "<unknown>";
	char hostname[NI_MAXHOST];
	Socket *mesh_client_socket;
	// Socket that is created to the root node of a mesh network.
	struct sockaddr_storage address;
	socklen_t length = sizeof(address);
	char buffer[NI_MAXHOST + NI_MAXSERV + 4];

	(void)opaque;

	log_info("New connection on mesh port");

	// Accept new mesh client socket.
	mesh_client_socket = socket_accept(&mesh_listen_socket,
	                                   (struct sockaddr *)&address,
	                                   &length);

	if (mesh_client_socket == NULL) {
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
		         mesh_client_socket->handle, get_errno_name(errno), errno);
	} else {
		snprintf(buffer, sizeof(buffer), "%s:%s", hostname, port);

		name = buffer;
	}

	/*
	 * Allocate and initialise a new mesh stack. Note that in this stage the stack
	 * is not added to brickd's central list of stacks yet.
	 */
	if (mesh_stack_create(name, mesh_client_socket) < 0) {
		log_error("Could not create new mesh stack");
	} else {
		log_info("New mesh stack created");
	}
}

int mesh_start_listening(void) {
	const char *address = config_get_option_value("listen.address")->string;
	uint16_t port = (uint16_t)config_get_option_value("listen.mesh_gateway_port")->integer;
	bool dual_stack = config_get_option_value("listen.dual_stack")->boolean;

	if (socket_open_server(&mesh_listen_socket, address, port, dual_stack,
	                       socket_create_allocated) < 0) {
		return -1;
	}

	if (event_add_source(mesh_listen_socket.handle, EVENT_SOURCE_TYPE_GENERIC,
	                     "mesh-listen", EVENT_READ, mesh_handle_accept, NULL) < 0) {
		socket_destroy(&mesh_listen_socket);

		return -1;
	}

	return 0;
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
