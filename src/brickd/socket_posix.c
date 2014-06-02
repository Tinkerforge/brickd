/*
 * brickd
 * Copyright (C) 2012-2014 Matthias Bolte <matthias@tinkerforge.com>
 * Copyright (C) 2014 Olaf Lüke <olaf@tinkerforge.com>
 *
 * socket_posix.c: POSIX based socket implementation
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
#include <fcntl.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "socket.h"

#include "utils.h"

// sets errno on error
static int socket_prepare(Socket *socket) {
	int no_delay = 1;
	int flags;

	// enable no-delay option
	if (setsockopt(socket->base.handle, IPPROTO_TCP, TCP_NODELAY,
	               &no_delay, sizeof(no_delay)) < 0) {
		return -1;
	}

	// enable non-blocking operation
	flags = fcntl(socket->base.handle, F_GETFL, 0);

	if (flags < 0 || fcntl(socket->base.handle, F_SETFL, flags | O_NONBLOCK) < 0) {
		return -1;
	}

	return 0;
}

// sets errno on error
int socket_open(Socket *socket_, int family, int type, int protocol) {
	int saved_errno;

	// create socket
	socket_->base.handle = socket(family, type, protocol);

	if (socket_->base.handle < 0) {
		return -1;
	}

	// prepare socket
	if (socket_prepare(socket_) < 0) {
		saved_errno = errno;

		close(socket_->base.handle);

		errno = saved_errno;

		return -1;
	}

	return 0;
}

// sets errno on error
int socket_accept_platform(Socket *socket, Socket *accepted_socket,
                           struct sockaddr *address, socklen_t *length) {
	int saved_errno;

	// accept socket
	accepted_socket->base.handle = accept(socket->base.handle, address, length);

	if (accepted_socket->base.handle < 0) {
		return -1;
	}

	// prepare socket
	if (socket_prepare(accepted_socket) < 0) {
		saved_errno = errno;

		close(accepted_socket->base.handle);

		errno = saved_errno;

		return -1;
	}

	return 0;
}

void socket_destroy(Socket *socket) {
	// check if socket is actually open, as socket_create deviates from
	// the common pattern of allocation the wrapped resource
	if (socket->base.handle != INVALID_EVENT_HANDLE) {
		shutdown(socket->base.handle, SHUT_RDWR);
		close(socket->base.handle);
	}
}

// sets errno on error
int socket_bind(Socket *socket, const struct sockaddr *address, socklen_t length) {
	return bind(socket->base.handle, address, length);
}

// sets errno on error
int socket_listen_platform(Socket *socket, int backlog) {
	return listen(socket->base.handle, backlog);
}

// sets errno on error
int socket_receive_platform(Socket *socket, void *buffer, int length) {
	return recv(socket->base.handle, buffer, length, 0);
}

// sets errno on error
int socket_send_platform(Socket *socket, void *buffer, int length) {
#ifdef MSG_NOSIGNAL
	int flags = MSG_NOSIGNAL;
#else
	int flags = 0;
#endif

	return send(socket->base.handle, buffer, length, flags);
}

// sets errno on error
int socket_set_address_reuse(Socket *socket, int address_reuse) {
	int on = address_reuse ? 1 : 0;

	return setsockopt(socket->base.handle, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
}

// sets errno on error
int socket_set_dual_stack(Socket *socket, int dual_stack) {
	int on = dual_stack ? 0 : 1;

	return setsockopt(socket->base.handle, IPPROTO_IPV6, IPV6_V6ONLY, &on, sizeof(on));
}

// sets errno on error
struct addrinfo *socket_hostname_to_address(const char *hostname, uint16_t port) {
	char service[32];
	struct addrinfo hints;
	struct addrinfo *resolved = NULL;
	int rc;

	snprintf(service, sizeof(service), "%u", port);

	memset(&hints, 0, sizeof(hints));

	hints.ai_flags = AI_PASSIVE;
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	rc = getaddrinfo(hostname, service, &hints, &resolved);

	if (rc != 0) {
#if EAI_AGAIN < 0
		// getaddrinfo error codes are negative on Linux...
		errno = ERRNO_ADDRINFO_OFFSET - rc;
#else
		// ...but positive on Mac OS X
		errno = ERRNO_ADDRINFO_OFFSET + rc;
#endif

		return NULL;
	}

	return resolved;
}

// sets errno on error
int socket_address_to_hostname(struct sockaddr *address, socklen_t address_length,
                               char *hostname, int hostname_length,
                               char *port, int port_length) {
	int rc;

	rc = getnameinfo(address, address_length,
	                 hostname, hostname_length,
	                 port, port_length,
	                 NI_NUMERICHOST | NI_NUMERICSERV);

	if (rc != 0) {
#if EAI_AGAIN < 0
		// getnameinfo error codes are negative on Linux...
		errno = ERRNO_ADDRINFO_OFFSET - rc;
#else
		// ...but positive on Mac OS X
		errno = ERRNO_ADDRINFO_OFFSET + rc;
#endif

		return -1;
	}

	return 0;
}
