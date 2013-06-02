/*
 * brickd
 * Copyright (C) 2012-2013 Matthias Bolte <matthias@tinkerforge.com>
 *
 * socket_winapi.c: WinAPI based socket implementation
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
#include <winsock2.h>

#include "socket.h"

#include "utils.h"

// sets errno on error
int socket_create(EventHandle *handle, int domain, int type, int protocol) {
	BOOL flag = 1;

	*handle = socket(domain, type, protocol);

	if (*handle == INVALID_SOCKET) {
		errno = ERRNO_WINAPI_OFFSET + WSAGetLastError();

		return -1;
	}

	if (setsockopt(*handle, IPPROTO_TCP, TCP_NODELAY, (const char *)&flag,
	               sizeof(flag)) == SOCKET_ERROR) {
		errno = ERRNO_WINAPI_OFFSET + WSAGetLastError();

		closesocket(*handle);

		return -1;
	}

	return 0;
}

void socket_destroy(EventHandle handle) {
	shutdown(handle, SD_BOTH);
	closesocket(handle);
}

// sets errno on error
int socket_bind(EventHandle handle, const struct sockaddr *address,
                socklen_t length) {
	int rc = bind(handle, address, length);

	if (rc == SOCKET_ERROR) {
		rc = -1;
		errno = ERRNO_WINAPI_OFFSET + WSAGetLastError();
	}

	return rc;
}

// sets errno on error
int socket_listen(EventHandle handle, int backlog) {
	int rc = listen(handle, backlog);

	if (rc == SOCKET_ERROR) {
		rc = -1;
		errno = ERRNO_WINAPI_OFFSET + WSAGetLastError();
	}

	return rc;
}

// sets errno on error
int socket_accept(EventHandle handle, EventHandle *accepted_handle,
                  struct sockaddr *address, socklen_t *length) {
	*accepted_handle = accept(handle, address, length);

	if (*accepted_handle == INVALID_SOCKET) {
		errno = ERRNO_WINAPI_OFFSET + WSAGetLastError();

		return -1;
	}

	return 0;
}

// sets errno on error
int socket_receive(EventHandle handle, void *buffer, int length) {
	length = recv(handle, (char *)buffer, length, 0);

	if (length == SOCKET_ERROR) {
		length = -1;
		errno = ERRNO_WINAPI_OFFSET + WSAGetLastError();
	}

	return length;
}

// sets errno on error
int socket_send(EventHandle handle, void *buffer, int length) {
	length = send(handle, (const char *)buffer, length, 0);

	if (length == SOCKET_ERROR) {
		length = -1;
		errno = ERRNO_WINAPI_OFFSET + WSAGetLastError();
	}

	return length;
}

// sets errno on error
int socket_set_non_blocking(EventHandle handle, int non_blocking) {
	unsigned long argument = non_blocking;
	int rc = ioctlsocket(handle, FIONBIO, &argument);

	if (rc == SOCKET_ERROR) {
		rc = -1;
		errno = ERRNO_WINAPI_OFFSET + WSAGetLastError();
	}

	return rc;
}

// sets errno on error
int socket_set_address_reuse(EventHandle handle, int address_reuse) {
	BOOL argument = address_reuse;
	int rc = setsockopt(handle, SOL_SOCKET, SO_REUSEADDR, (const char *)&argument,
	                    sizeof(BOOL));

	if (rc == SOCKET_ERROR) {
		rc = -1;
		errno = ERRNO_WINAPI_OFFSET + WSAGetLastError();
	}

	return rc;
}

// sets errno on error
char *resolve_address(struct sockaddr_in *address, socklen_t length) {
	char buffer[NI_MAXHOST];
	char *name;

	if (getnameinfo((struct sockaddr *)address, length, buffer, NI_MAXHOST,
	                NULL, 0, NI_NUMERICHOST) != 0) {
		errno = ERRNO_WINAPI_OFFSET + WSAGetLastError();

		return NULL;
	}

	name = strdup(buffer);

	if (name == NULL) {
		errno = ENOMEM;

		return NULL;
	}

	return name;
}
