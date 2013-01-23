/*
 * brickd
 * Copyright (C) 2012-2013 Matthias Bolte <matthias@tinkerforge.com>
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

#include "event.h"

int socket_create(EventHandle *handle, int domain, int type, int protocol);
void socket_destroy(EventHandle handle);

int socket_bind(EventHandle handle, const struct sockaddr *address,
                socklen_t length);
int socket_listen(EventHandle handle, int backlog);
int socket_accept(EventHandle handle, EventHandle *accepted_handle,
                  struct sockaddr *address, socklen_t *length);

int socket_receive(EventHandle handle, void *buffer, int length);
int socket_send(EventHandle handle, void *buffer, int length);

int socket_set_non_blocking(EventHandle handle, int non_blocking);
int socket_set_address_reuse(EventHandle handle, int address_reuse);

char *resolve_address(struct sockaddr_in *address, socklen_t length);

#endif // BRICKD_SOCKET_H
