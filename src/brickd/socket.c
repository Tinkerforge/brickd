/*
 * brickd
 * Copyright (C) 2014 Matthias Bolte <matthias@tinkerforge.com>
 *
 * socket.c: Socket implementation
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

#include "socket.h"

extern int socket_accept_platform(Socket *socket, Socket *accepted_socket,
                                  struct sockaddr *address, socklen_t *length);
extern int socket_listen_platform(Socket *socket, int backlog);
extern int socket_receive_platform(Socket *socket, void *buffer, int length);
extern int socket_send_platform(Socket *socket, void *buffer, int length);

// sets errno on error
int socket_create(Socket *socket) {
	int rc = io_create(&socket->base, "plain-socket",
	                   (IODestroyFunction)socket_destroy,
	                   (IOReadFunction)socket_receive,
	                   (IOWriteFunction)socket_send);

	if (rc < 0) {
		return rc;
	}

	socket->create_allocated = NULL;
	socket->receive = socket_receive_platform;
	socket->send = socket_send_platform;

	return 0;
}

// sets errno on error
Socket *socket_create_allocated(void) {
	Socket *socket = calloc(1, sizeof(Socket));

	if (socket == NULL) {
		errno = ENOMEM;

		return NULL;
	}

	if (socket_create(socket) < 0) {
		free(socket);

		return NULL;
	}

	return socket;
}

// sets errno on error
Socket *socket_accept(Socket *socket, struct sockaddr *address, socklen_t *length) {
	int rc;
	Socket *allocated_socket = socket->create_allocated();

	if (allocated_socket == NULL) {
		// because accept() is not called now the event loop will receive
		// another event on the server socket to indicate the pending
		// connection attempt. but we're currently in an OOM situation so
		// there are other things to worry about.
		errno = ENOMEM;

		return NULL;
	}

	rc = socket_accept_platform(socket, allocated_socket, address, length);

	if (rc < 0) {
		free(allocated_socket);

		return NULL;
	}

	return allocated_socket;
}
// sets errno on error
int socket_listen(Socket *socket, int backlog,
                  SocketCreateAllocatedFunction create_allocated) {
	socket->create_allocated = create_allocated;

	return socket_listen_platform(socket, backlog);
}

// sets errno on error
int socket_receive(Socket *socket, void *buffer, int length) {
	if (socket->receive == NULL) {
		errno = ENOSYS;

		return -1;
	}

	return socket->receive(socket, buffer, length);
}

// sets errno on error
int socket_send(Socket *socket, void *buffer, int length) {
	if (socket->send == NULL) {
		errno = ENOSYS;

		return -1;
	}

	return socket->send(socket, buffer, length);
}
