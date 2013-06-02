/*
 * brickd
 * Copyright (C) 2012-2013 Matthias Bolte <matthias@tinkerforge.com>
 *
 * pipe_winapi.c: WinAPI based pipe implementation
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

#include "pipe.h"

#include "utils.h"

// sets errno on error
int pipe_create(EventHandle handles[2]) {
	SOCKET listener;
	struct sockaddr_in address;
	int length = sizeof(address);
	int rc;

	handles[0] = INVALID_SOCKET;
	handles[1] = INVALID_SOCKET;

	listener = socket(AF_INET, SOCK_STREAM, 0);

	if (listener == INVALID_SOCKET) {
		goto error;
	}

	memset(&address, 0, length);

	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(0x7f000001); // 127.0.0.1
	address.sin_port = 0;

	rc = bind(listener, (const struct sockaddr *)&address, length);

	if (rc == SOCKET_ERROR) {
		goto error;
	}

	rc = getsockname(listener, (struct sockaddr *)&address, &length);

	if (rc == SOCKET_ERROR) {
		goto error;
	}

	if (listen(listener, 1) == SOCKET_ERROR) {
		goto error;
	}

	handles[0] = socket(AF_INET, SOCK_STREAM, 0);

	if (handles[0] == INVALID_SOCKET) {
		goto error;
	}

	rc = connect(handles[0], (const struct sockaddr *)&address, length);

	if (rc == SOCKET_ERROR) {
		goto error;
	}

	handles[1] = accept(listener, NULL, NULL);

	if (handles[1] == INVALID_SOCKET) {
		goto error;
	}

	closesocket(listener);

	return 0;

error:
	rc = WSAGetLastError();

	closesocket(listener);
	closesocket(handles[0]);
	closesocket(handles[1]);

	errno = ERRNO_WINAPI_OFFSET + rc;

	return -1;
}

void pipe_destroy(EventHandle handles[2]) {
	closesocket(handles[0]);
	closesocket(handles[1]);
}

// sets errno on error
int pipe_read(EventHandle handle, void *buffer, int length) {
	// FIXME: handle partial read and interruption
	length = recv(handle, (char *)buffer, length, 0);

	if (length == SOCKET_ERROR) {
		errno = ERRNO_WINAPI_OFFSET + WSAGetLastError();
	}

	return length;
}

// sets errno on error
int pipe_write(EventHandle handle, void *buffer, int length) {
	// FIXME: handle partial write and interruption
	length = send(handle, (const char *)buffer, length, 0);

	if (length == SOCKET_ERROR) {
		errno = ERRNO_WINAPI_OFFSET + WSAGetLastError();
	}

	return length;
}
