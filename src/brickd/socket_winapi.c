/*
 * brickd
 * Copyright (C) 2012-2013 Matthias Bolte <matthias@tinkerforge.com>
 * Copyright (C) 2014 Olaf Lüke <olaf@tinkerforge.com>
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
#include <stdio.h>
#include <winsock2.h>
#include <ws2tcpip.h> // for IPV6_V6ONLY
#include <windows.h>

#include "socket.h"
#include "websocket.h"

#include "utils.h"

// sets errno on error
int socket_create(EventHandle *handle, int family, int type, int protocol) {
	BOOL on = TRUE;

	*handle = socket(family, type, protocol);

	if (*handle == INVALID_SOCKET) {
		errno = ERRNO_WINAPI_OFFSET + WSAGetLastError();

		return -1;
	}

	if (setsockopt(*handle, IPPROTO_TCP, TCP_NODELAY, (const char *)&on,
	               sizeof(on)) == SOCKET_ERROR) {
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
int socket_receive(EventHandle handle, SocketStorage *storage, void *buffer, int length) {
	length = recv(handle, (char *)buffer, length, 0);

	if (length == SOCKET_ERROR) {
		length = -1;
		errno = ERRNO_WINAPI_OFFSET + WSAGetLastError();
	}

	if (storage != NULL && storage->type == SOCKET_TYPE_WEBSOCKET) {
		return websocket_receive(handle, storage, buffer, length);
	}

	return length;
}

// sets errno on error
int socket_send(EventHandle handle, SocketStorage *storage, void *buffer, int length) {
	if (storage != NULL && storage->type == SOCKET_TYPE_WEBSOCKET) {
		return websocket_send(handle, storage, buffer, length);
	}

	length = send(handle, (const char *)buffer, length, 0);

	if (length == SOCKET_ERROR) {
		length = -1;
		errno = ERRNO_WINAPI_OFFSET + WSAGetLastError();
	}

	return length;
}

// sets errno on error
int socket_set_non_blocking(EventHandle handle, int non_blocking) {
	unsigned long on = non_blocking ? 1 : 0;
	int rc = ioctlsocket(handle, FIONBIO, &on);

	if (rc == SOCKET_ERROR) {
		rc = -1;
		errno = ERRNO_WINAPI_OFFSET + WSAGetLastError();
	}

	return rc;
}

// sets errno on error
int socket_set_address_reuse(EventHandle handle, int address_reuse) {
	DWORD on = address_reuse ? TRUE : FALSE;
	int rc = setsockopt(handle, SOL_SOCKET, SO_REUSEADDR, (const char *)&on, sizeof(on));

	if (rc == SOCKET_ERROR) {
		rc = -1;
		errno = ERRNO_WINAPI_OFFSET + WSAGetLastError();
	}

	return rc;
}

// sets errno on error
int socket_set_dual_stack(EventHandle handle, int dual_stack) {
	DWORD on = dual_stack ? 0 : 1;
	int rc;

	if ((DWORD)(LOBYTE(LOWORD(GetVersion()))) < 6) {
		// the IPV6_V6ONLY option is only supported on Vista or later. on
		// Windows XP dual-stack mode is not supported at all. so fail with
		// expected error if dual-stack mode should be enabled and pretend
		// that it got disabled otherwise as this is the case on Windows XP
		// anyway
		if (dual_stack) {
			errno = ERRNO_WINAPI_OFFSET + WSAENOPROTOOPT;

			return -1;
		}

		return 0;
	}

	rc = setsockopt(handle, IPPROTO_IPV6, IPV6_V6ONLY, (const char *)&on, sizeof(on));

	if (rc == SOCKET_ERROR) {
		rc = -1;
		errno = ERRNO_WINAPI_OFFSET + WSAGetLastError();
	}

	return rc;
}

// sets errno on error
struct addrinfo *socket_hostname_to_address(const char *hostname, uint16_t port) {
	char service[32];
	struct addrinfo hints;
	struct addrinfo *resolved = NULL;

	snprintf(service, sizeof(service), "%u", port);

	memset(&hints, 0, sizeof(hints));

	hints.ai_flags = AI_PASSIVE;
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	if (getaddrinfo(hostname, service, &hints, &resolved) != 0) {
		errno = ERRNO_WINAPI_OFFSET + WSAGetLastError();

		return NULL;
	}

	return resolved;
}

// sets errno on error
char *socket_address_to_hostname(struct sockaddr *address, socklen_t length) {
	char buffer[NI_MAXHOST];
	char *name;

	if (getnameinfo(address, length, buffer, NI_MAXHOST, NULL, 0, NI_NUMERICHOST) != 0) {
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
