/*
 * brickd
 * Copyright (C) 2016 Ishraq Ibne Ashraf <ishraq@tinkerforge.com>
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
static bool is_mesh_listen_socket_open = false;
static LogSource _log_source = LOG_SOURCE_INITIALIZER;

static const char *network_get_address_family_name(int family, bool report_dual_stack) {
	switch (family) {
		case AF_INET:
			return "IPv4";

		case AF_INET6:
			if (report_dual_stack && config_get_option_value("listen.dual_stack")->boolean) {
				return "IPv6 dual-stack";
			} else {
				return "IPv6";
			}

		default:
			return "<unknown>";
	}
}

int mesh_init(void) {
  uint16_t mesh_listen_port = \
		(uint16_t)config_get_option_value("listen.mesh_port")->integer;

  if (mesh_listen_port == 0) {
    log_info("Mesh support is disabled");

    return 0;
  }

  log_info("Initializing mesh subsystem");

  if (mesh_start_listening(mesh_listen_port,
                           socket_create_allocated) >= 0) {
		is_mesh_listen_socket_open = true;
	}

	if (!is_mesh_listen_socket_open) {
		log_error("Failed to open mesh listen socket");

		return -1;
	}

	// Create mesh stack array.
	if (array_create(&mesh_stacks, MAX_MESH_STACKS, sizeof(MeshStack), false) < 0) {
		log_error("Failed to create mesh stack array: %s (%d)",
              get_errno_name(errno), errno);

		return -1;
	}

	return 0;
}

void mesh_exit(void) {
  log_info("Shutting down mesh subsystem");

	// Cleanup mesh listen socket.
	if (is_mesh_listen_socket_open) {
		event_remove_source(mesh_listen_socket.base.handle,
												EVENT_SOURCE_TYPE_GENERIC);

		socket_destroy(&mesh_listen_socket);
	}

	// Mesh stack related cleanup is done in mesh_stack_destroy().
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
		         mesh_client_socket->base.handle, get_errno_name(errno), errno);
	}
  else {
		snprintf(buffer, sizeof(buffer), "%s:%s", hostname, port);

		name = buffer;
	}

	/*
	 * Allocate and initialise a new mesh stack. Note that in this stage the stack
	 * is not added to brickd's central list of stacks yet.
	 */
	if (mesh_stack_create(name, mesh_client_socket) < 0) {
		log_error("Could not create new mesh stack");
	}
	else {
		log_info("New mesh stack created");
	}
}

int mesh_start_listening(uint16_t mesh_listen_socket_port,
                         SocketCreateAllocatedFunction create_allocated) {
	int phase = 0;
	struct addrinfo *resolved_address = NULL;
	const char *address = config_get_option_value("listen.address")->string;

	log_info("Opening mesh listen socket (P: %u)", mesh_listen_socket_port);

	resolved_address = socket_hostname_to_address(address, mesh_listen_socket_port);

	// Currently no IPv6 support for mesh.
	if(resolved_address->ai_family == AF_INET6) {
		log_error("Mesh gateway does not support IPv6");

		goto cleanup;
	}

	if (resolved_address == NULL) {
		log_error("Could not resolve mesh listen address '%s' (P: %u): %s (%d)",
							address,
							mesh_listen_socket_port,
							get_errno_name(errno),
							errno);

		goto cleanup;
	}

	phase = 1;

	// Create socket.
	if (socket_create(&mesh_listen_socket) < 0) {
		log_error("Failed to create mesh listen socket: %s (%d)",
							get_errno_name(errno),
							errno);

		goto cleanup;
	}

	phase = 2;

	if (socket_open(&mesh_listen_socket,
									resolved_address->ai_family,
									resolved_address->ai_socktype,
									resolved_address->ai_protocol) < 0) {
		log_error("Failed to open %s mesh listen socket: %s (%d)",
		          network_get_address_family_name(resolved_address->ai_family, false),
		          get_errno_name(errno),
							errno);

		goto cleanup;
	}

	#ifndef _WIN32
		/*
		 * On Unix the SO_REUSEADDR socket option allows to rebind sockets in
		 * CLOSE-WAIT state. this is a desired effect. On Windows SO_REUSEADDR
		 * allows to rebind sockets in any state. This is dangerous. Therefore,
		 * don't set SO_REUSEADDR on Windows. Sockets can be rebound in CLOSE-WAIT
		 * state on Windows by default.
		 */
		if (socket_set_address_reuse(&mesh_listen_socket, true) < 0) {
			log_error("Failed to enable address-reuse mode for mesh listen socket: %s (%d)",
			          get_errno_name(errno),
								errno);

			goto cleanup;
		}
	#endif

	// Bind socket and start to listen.
	if (socket_bind(&mesh_listen_socket,
									resolved_address->ai_addr,
									resolved_address->ai_addrlen) < 0) {
		log_error("Failed to bind %s mesh listen socket to (A: %s, P: %u): %s (%d)",
		          network_get_address_family_name(resolved_address->ai_family, true),
		          address,
							mesh_listen_socket_port,
							get_errno_name(errno),
							errno);

		goto cleanup;
	}

	if (socket_listen(&mesh_listen_socket, 10, create_allocated) < 0) {
		log_error("Failed to listen to %s mesh socket (A: %s, P: %u): %s (%d)",
		          network_get_address_family_name(resolved_address->ai_family, true),
		          address,
							mesh_listen_socket_port,
							get_errno_name(errno),
							errno);

		goto cleanup;
	}

	log_info("Mesh gateway started listening on (A: %s, P: %u, F: %s)",
	          address,
						mesh_listen_socket_port,
	          network_get_address_family_name(resolved_address->ai_family, true));

	if(event_add_source(mesh_listen_socket.base.handle,
											EVENT_SOURCE_TYPE_GENERIC,
											EVENT_READ,
											mesh_handle_accept,
											NULL) < 0) {
		log_error("Failed to add read event for mesh listen socket");

		goto cleanup;
	}

	phase = 3;

	freeaddrinfo(resolved_address);

cleanup:
	switch(phase) { // No breaks, all cases fall through intentionally.
		case 2:
			socket_destroy(&mesh_listen_socket);

		case 1:
			freeaddrinfo(resolved_address);

		default:
			break;
	}

	return phase == 3 ? 0 : -1;
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
