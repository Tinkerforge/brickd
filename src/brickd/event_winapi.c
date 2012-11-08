/*
 * brickd
 * Copyright (C) 2012 Matthias Bolte <matthias@tinkerforge.com>
 *
 * event_winapi.c: Select based event loop
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

#include "event.h"

#include "log.h"
#include "utils.h"

#define LOG_CATEGORY LOG_CATEGORY_EVENT

typedef struct {
	int count;
	SOCKET sockets[0];
} SocketSet;

static int _socket_read_set_allocated = 0;
static SocketSet *_socket_read_set = NULL;
static int _socket_write_set_allocated = 0;
static SocketSet *_socket_write_set = NULL;

int event_init_platform(void) {
	int count = 32;

	// create read set
	_socket_read_set = calloc(1, sizeof(SocketSet) + sizeof(SOCKET) * count);

	if (_socket_read_set == NULL) {
		log_error("Could not create socket read set: %s (%d)",
		          get_errno_name(ENOMEM), ENOMEM);

		return -1;
	}

	_socket_read_set_allocated = count;

	// create write set
	_socket_write_set = calloc(1, sizeof(SocketSet) + sizeof(SOCKET) * count);

	if (_socket_write_set == NULL) {
		log_error("Could not create socket write set: %s (%d)",
		          get_errno_name(ENOMEM), ENOMEM);

		return -1;
	}

	_socket_write_set_allocated = count;

	return 0;
}

void event_exit_platform(void) {
	free(_socket_read_set);
	free(_socket_write_set);
}

int event_run_platform(Array *event_sources, int *running) {
	SocketSet *socket_set;
	int i;
	EventSource *event_source;
	int ready;
	int rc;
	int event_source_count;
	int received_events;

	*running = 1;

	while (*running) {
		// FIXME: this over allocates
		if (event_sources->count > _socket_read_set_allocated) {
			// FIXME: use better growth pattern
			socket_set = realloc(_socket_read_set, sizeof(SocketSet) + sizeof(SOCKET) * event_sources->count);

			if (socket_set == NULL) {
				log_error("Could not resize socket read set: %s (%d)",
				          get_errno_name(ENOMEM), ENOMEM);

				return -1;
			}

			_socket_read_set_allocated = event_sources->count;
			_socket_read_set = socket_set;
		}

		// FIXME: this over allocates
		if (event_sources->count > _socket_write_set_allocated) {
			// FIXME: use better growth pattern
			socket_set = realloc(_socket_write_set, sizeof(SocketSet) + sizeof(SOCKET) * event_sources->count);

			if (socket_set == NULL) {
				log_error("Could not resize socket write set: %s (%d)",
				          get_errno_name(ENOMEM), ENOMEM);

				return -1;
			}

			_socket_write_set_allocated = event_sources->count;
			_socket_write_set = socket_set;
		}

		_socket_read_set->count = 0;
		_socket_write_set->count = 0;

		for (i = 0; i < event_sources->count; i++) {
			event_source = array_get(event_sources, i);

			if (event_source->events & EVENT_READ) {
				_socket_read_set->sockets[_socket_read_set->count++] = event_source->handle;
			}

			if (event_source->events & EVENT_WRITE) {
				_socket_write_set->sockets[_socket_write_set->count++] = event_source->handle;
			}
		}

		ready = select(0, (fd_set *)_socket_read_set, (fd_set *)_socket_write_set, NULL, NULL);

		if (ready < 0) {
			rc = WSAGetLastError();

			if (rc == WSAEINTR) {
				continue;
			}

			rc += ERRNO_WINSOCK2_OFFSET;

			*running = 0;

			log_error("Could not select event sources: %s (%d)",
			          get_errno_name(rc), rc);

			return -1;
		}

		// cache event source count here to avoid looking at new event
		// sources that got added during the event handling
		event_source_count = event_sources->count;

		for (i = 0; i < event_source_count && ready > 0; ++i) {
			event_source = array_get(event_sources, i);
			received_events = 0;

			if (FD_ISSET(event_source->handle, _socket_read_set)) {
				received_events |= EVENT_READ;
			}

			if (FD_ISSET(event_source->handle, _socket_write_set)) {
				received_events |= EVENT_WRITE;
			}

			if (received_events == 0) {
				continue;
			}

			--ready;

			if (event_source->removed) {
				log_debug("Ignoring event source (handle: %d, received events: %d) marked as removed at index %d",
				          event_source->handle, received_events, i);
			} else {
				log_debug("Handling event source (handle: %d, received events: %d) at index %d",
				          event_source->handle, received_events, i);

				event_source->function(event_source->opaque);
			}

			if (!*running) {
				break;
			}
		}

		log_debug("Handled all ready event sources");

		// now remove event sources that got marked as removed during the
		// event handling
		for (i = 0; i < event_sources->count;) {
			event_source = array_get(event_sources, i);

			if (event_source->removed) {
				array_remove(event_sources, i, NULL);

				log_debug("Removed event source (handle: %d, events: %d) at index %d",
				          event_source->handle, event_source->events, i);
			} else {
				++i;
			}
		}
	}

	return 0;
}
