/*
 * brickd
 * Copyright (C) 2012-2014 Matthias Bolte <matthias@tinkerforge.com>
 * Copyright (C) 2014 Olaf Lüke <olaf@tinkerforge.com>
 *
 * socket.h: Socket specific functions
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

#ifndef BRICKD_SOCKET_H
#define BRICKD_SOCKET_H

#ifdef _WIN32
	#include <ws2tcpip.h> // for socklen_t
#else
	#include <netinet/in.h>
#endif
#include <stdint.h>

#include "event.h"

#define SOCKET_CONTINUE -2

typedef struct Socket_ Socket;

typedef Socket *(*SocketAllocateFunction)(void);
typedef int (*SocketReceiveEpilogFunction)(Socket *socket, void *buffer, int length);
typedef int (*SocketSendOverrideFunction)(Socket *socket, void *buffer, int length);

struct Socket_ {
	EventHandle handle;
	const char *type;
	SocketAllocateFunction allocate;
	SocketReceiveEpilogFunction receive_epilog;
	SocketSendOverrideFunction send_override;
};

Socket *socket_allocate(void);

int socket_create(Socket *socket, int family, int type, int protocol);
void socket_destroy(Socket *socket);

int socket_bind(Socket *socket, const struct sockaddr *address, socklen_t length);
int socket_listen(Socket *socket, int backlog);
Socket *socket_accept(Socket *socket, struct sockaddr *address, socklen_t *length);

int socket_receive(Socket *socket, void *buffer, int length);
int socket_send(Socket *socket, void *buffer, int length);

int socket_set_non_blocking(Socket *socket, int non_blocking);
int socket_set_address_reuse(Socket *socket, int address_reuse);
int socket_set_dual_stack(Socket *socket, int dual_stack);

struct addrinfo *socket_hostname_to_address(const char *hostname, uint16_t port);
char *socket_address_to_hostname(struct sockaddr *address, socklen_t length);

#endif // BRICKD_SOCKET_H
