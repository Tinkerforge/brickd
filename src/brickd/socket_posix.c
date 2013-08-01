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
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "socket.h"

#include "utils.h"

// sets errno on error
int socket_create(EventHandle *handle, int domain, int type, int protocol) {
	int flag = 1;
	int saved_errno;

	*handle = socket(domain, type, protocol);

	if (*handle < 0) {
		return -1;
	}

	if (setsockopt(*handle, IPPROTO_TCP, TCP_NODELAY, &flag,
	               sizeof(flag)) < 0) {
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
	return setsockopt(handle, SOL_SOCKET, SO_REUSEADDR,
	                  &address_reuse, sizeof(address_reuse));
}

// sets errno on error
char *resolve_address(struct sockaddr_in *address, socklen_t length) {
	int rc;
	char buffer[NI_MAXHOST];
	char *name;

	rc = getnameinfo((struct sockaddr *)address, length, buffer, NI_MAXHOST,
	                 NULL, 0, NI_NUMERICHOST);

	if (rc != 0) {
#if EAI_AGAIN < 0
		errno = ERRNO_ADDRINFO_OFFSET - rc;
#else
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
