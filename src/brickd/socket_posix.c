/*
 * brickd
 * Copyright (C) 2012-2013 Matthias Bolte <matthias@tinkerforge.com>
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
int socket_create(EventHandle *handle, int family, int type, int protocol) {
	int on = 1;
	int saved_errno;

	*handle = socket(family, type, protocol);

	if (*handle < 0) {
		return -1;
	}

	if (setsockopt(*handle, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on)) < 0) {
		saved_errno = errno;

		close(*handle);

		errno = saved_errno;

		return -1;
	}

	return 0;
}

void socket_destroy(EventHandle handle) {
	shutdown(handle, SHUT_RDWR);
	close(handle);
}

// sets errno on error
int socket_bind(EventHandle handle, const struct sockaddr *address,
                socklen_t length) {
	return bind(handle, address, length);
}

// sets errno on error
int socket_listen(EventHandle handle, int backlog) {
	return listen(handle, backlog);
}

// sets errno on error
int socket_accept(EventHandle handle, EventHandle *accepted_handle,
                  struct sockaddr *address, socklen_t *length) {
	*accepted_handle = accept(handle, address, length);

	return *accepted_handle < 0 ? -1 : 0;
}

// sets errno on error
int socket_receive(EventHandle handle, void *buffer, int length) {
	return recv(handle, buffer, length, 0);
}

// sets errno on error
int socket_send(EventHandle handle, void *buffer, int length) {
#ifdef MSG_NOSIGNAL
	int flags = MSG_NOSIGNAL;
#else
	int flags = 0;
#endif

	return send(handle, buffer, length, flags);
}

// sets errno on error
int socket_set_non_blocking(EventHandle handle, int non_blocking) {
	int flags = fcntl(handle, F_GETFL, 0);

	if (flags < 0) {
		return -1;
	}

	if (non_blocking) {
		flags |= O_NONBLOCK;
	} else {
		flags &= ~O_NONBLOCK;
	}

	return fcntl(handle, F_SETFL, flags);
}

// sets errno on error
int socket_set_address_reuse(EventHandle handle, int address_reuse) {
	int on = address_reuse ? 1 : 0;

	return setsockopt(handle, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
}

// sets errno on error
int socket_set_dual_stack(EventHandle handle, int dual_stack) {
	int on = dual_stack ? 0 : 1;

	return setsockopt(handle, IPPROTO_IPV6, IPV6_V6ONLY, &on, sizeof(on));
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
char *socket_address_to_hostname(struct sockaddr *address, socklen_t length) {
	char buffer[NI_MAXHOST];
	char *name;
	int rc;

	rc = getnameinfo(address, length, buffer, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);

	if (rc != 0) {
#if EAI_AGAIN < 0
		// getnameinfo error codes are negative on Linux...
		errno = ERRNO_ADDRINFO_OFFSET - rc;
#else
		// ...but positive on Mac OS X
		errno = ERRNO_ADDRINFO_OFFSET + rc;
#endif

		return NULL;
	}

	name = strdup(buffer);

	if (name == NULL) {
		errno = ENOMEM;

		return NULL;
	}

	return name;
}
