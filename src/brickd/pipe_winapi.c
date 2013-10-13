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

/*
 * pipes are used to inject events into the select based event loop. this
 * implementation uses a pair of sockets to create a pipe, because select
 * can only be used with sockets on Windows.
 */

#include <errno.h>
#include <winsock2.h>

#include "pipe.h"

#include "utils.h"

// sets errno on error
// FIXME: maybe use IPv6 if available
int pipe_create(Pipe *pipe) {
	SOCKET listener;
	struct sockaddr_in address;
	int length = sizeof(address);
	int rc;

	pipe->read_end = INVALID_SOCKET;
	pipe->write_end = INVALID_SOCKET;

	listener = socket(AF_INET, SOCK_STREAM, 0);

	if (listener == INVALID_SOCKET) {
		goto error;
	}

	memset(&address, 0, length);

	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
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

	pipe->read_end = socket(AF_INET, SOCK_STREAM, 0);

	if (pipe->read_end == INVALID_SOCKET) {
		goto error;
	}

	rc = connect(pipe->read_end, (const struct sockaddr *)&address, length);

	if (rc == SOCKET_ERROR) {
		goto error;
	}

	pipe->write_end = accept(listener, NULL, NULL);

	if (pipe->write_end == INVALID_SOCKET) {
		goto error;
	}

	closesocket(listener);

	return 0;

error:
	rc = WSAGetLastError();

	closesocket(listener);
	closesocket(pipe->read_end);
	closesocket(pipe->write_end);

	errno = ERRNO_WINAPI_OFFSET + rc;

	return -1;
}

void pipe_destroy(Pipe *pipe) {
	closesocket(pipe->read_end);
	closesocket(pipe->write_end);
}

// sets errno on error
int pipe_read(Pipe *pipe, void *buffer, int length) {
	// FIXME: handle partial read and interruption
	length = recv(pipe->read_end, (char *)buffer, length, 0);

	if (length == SOCKET_ERROR) {
		errno = ERRNO_WINAPI_OFFSET + WSAGetLastError();
	}

	return length;
}

// sets errno on error
int pipe_write(Pipe *pipe, void *buffer, int length) {
	// FIXME: handle partial write and interruption
	length = send(pipe->write_end, (const char *)buffer, length, 0);

	if (length == SOCKET_ERROR) {
		errno = ERRNO_WINAPI_OFFSET + WSAGetLastError();
	}

	return length;
}
