/*
 * brickd
 * Copyright (C) 2012-2021 Matthias Bolte <matthias@tinkerforge.com>
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
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

#include <daemonlib/array.h>
#include <daemonlib/event.h>
#include <daemonlib/log.h>
#include <daemonlib/macros.h>
#include <daemonlib/utils.h>

static LogSource _log_source = LOG_SOURCE_INITIALIZER;

// mimic fd_set, the dummy member is necessary to get the correct alignment for
// the count and sockets members when compiling for 64-bit architecture
typedef struct {
	int allocated;
	int dummy;
	int count;
	SOCKET sockets[0];
} SocketSet;

static SocketSet *_socket_read_set = NULL;
static SocketSet *_socket_write_set = NULL;
static SocketSet *_socket_error_set = NULL;

static int event_reserve_socket_set(SocketSet **socket_set, int reserve) {
	SocketSet *bytes;

	if (*socket_set != NULL && (*socket_set)->allocated >= reserve) {
		return 0;
	}

	reserve = GROW_ALLOCATION(reserve);

	if (*socket_set != NULL) {
		bytes = realloc(*socket_set, sizeof(SocketSet) + sizeof(SOCKET) * reserve);
	} else {
		bytes = calloc(1, sizeof(SocketSet) + sizeof(SOCKET) * reserve);
	}

	if (bytes == NULL) {
		errno = ENOMEM;

		return -1;
	}

	*socket_set = bytes;
	(*socket_set)->allocated = reserve;

	return 0;
}

static fd_set *event_get_socket_set_as_fd_set(SocketSet *socket_set) {
	return (fd_set *)((uint8_t *)socket_set + offsetof(SocketSet, count));
}

int event_init_platform(void) {
	int phase = 0;

	// create read set
	if (event_reserve_socket_set(&_socket_read_set, 32) < 0) {
		log_error("Could not create socket read set: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 1;

	// create write set
	if (event_reserve_socket_set(&_socket_write_set, 32) < 0) {
		log_error("Could not create socket write set: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 2;

	// create error set
	if (event_reserve_socket_set(&_socket_error_set, 32) < 0) {
		log_error("Could not create socket error set: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 3;

cleanup:
	switch (phase) { // no breaks, all cases fall through intentionally
	case 2:
		free(_socket_write_set);
		// fall through

	case 1:
		free(_socket_read_set);
		// fall through

	default:
		break;
	}

	return phase == 3 ? 0 : -1;
}

void event_exit_platform(void) {
	free(_socket_error_set);
	free(_socket_write_set);
	free(_socket_read_set);
}

int event_source_added_platform(EventSource *event_source) {
	(void)event_source;

	return 0;
}

int event_source_modified_platform(EventSource *event_source) {
	(void)event_source;

	return 0;
}

void event_source_removed_platform(EventSource *event_source) {
	(void)event_source;
}

int event_run_platform(Array *event_sources, bool *running, EventCleanupFunction cleanup) {
	int result = -1;
	int i;
	EventSource *event_source;
	fd_set *fd_read_set;
	fd_set *fd_write_set;
	fd_set *fd_error_set;
	int ready;
	int handled;
	int rc;
	int event_source_count;
	uint32_t received_events;

	*running = true;

	cleanup();
	event_cleanup_sources();

	while (*running) {
		// update SocketSet arrays
		if (event_reserve_socket_set(&_socket_read_set, // FIXME: this over-allocates
		                             event_sources->count) < 0) {
			log_error("Could not resize socket read set: %s (%d)",
			          get_errno_name(errno), errno);

			goto cleanup;
		}

		if (event_reserve_socket_set(&_socket_write_set, // FIXME: this over-allocates
		                             event_sources->count) < 0) {
			log_error("Could not resize socket write set: %s (%d)",
			          get_errno_name(errno), errno);

			goto cleanup;
		}

		if (event_reserve_socket_set(&_socket_error_set, // FIXME: this over-allocates
		                             event_sources->count) < 0) {
			log_error("Could not resize socket error set: %s (%d)",
			          get_errno_name(errno), errno);

			goto cleanup;
		}

		_socket_read_set->count = 0;
		_socket_write_set->count = 0;
		_socket_error_set->count = 0;

		for (i = 0; i < event_sources->count; ++i) {
			event_source = array_get(event_sources, i);

			if (event_source->type != EVENT_SOURCE_TYPE_GENERIC) {
				continue;
			}

			if ((event_source->events & EVENT_READ) != 0) {
				_socket_read_set->sockets[_socket_read_set->count++] = event_source->handle;
			}

			if ((event_source->events & EVENT_WRITE) != 0) {
				_socket_write_set->sockets[_socket_write_set->count++] = event_source->handle;
			}

			if ((event_source->events & EVENT_PRIO) != 0) {
				log_error("Event prio is not supported");
			}

			if ((event_source->events & EVENT_ERROR) != 0) {
				_socket_error_set->sockets[_socket_error_set->count++] = event_source->handle;
			}
		}

		// start to select
		log_event_debug("Starting to select on %d + %d + %d %s event source(s)",
		                _socket_read_set->count, _socket_write_set->count, _socket_error_set->count,
		                event_get_source_type_name(EVENT_SOURCE_TYPE_GENERIC, false));

		fd_read_set = event_get_socket_set_as_fd_set(_socket_read_set);
		fd_write_set = event_get_socket_set_as_fd_set(_socket_write_set);
		fd_error_set = event_get_socket_set_as_fd_set(_socket_error_set);

		ready = select(0, fd_read_set, fd_write_set, fd_error_set, NULL);

		if (ready == SOCKET_ERROR) {
			rc = ERRNO_WINAPI_OFFSET + WSAGetLastError();

			if (rc == ERRNO_WINAPI_OFFSET + WSAEINTR) {
				continue;
			}

			log_error("Could not select on %s event sources: %s (%d)",
			          event_get_source_type_name(EVENT_SOURCE_TYPE_GENERIC, false),
			          get_errno_name(rc), rc);

			goto cleanup;
		}

		// handle select result
		log_event_debug("Select returned %d %s event source(s) as ready",
		                ready, event_get_source_type_name(EVENT_SOURCE_TYPE_GENERIC, false));

		handled = 0;

		// cache event source count here to avoid looking at new event
		// sources that got added during the event handling
		event_source_count = event_sources->count;

		// this loop assumes that event source array and fd_sets can be matched
		// by index. this means that the first N items of the event source array
		// (with N = items in the fd_sets) are not removed or replaced during
		// the iteration over the pollfd array. because of this event_remove_source
		// only marks event sources as removed, the actual removal is done after
		// this loop by event_cleanup_sources
		for (i = 0; *running && i < event_source_count && ready > handled; ++i) {
			event_source = array_get(event_sources, i);
			received_events = 0;

			if (event_source->type != EVENT_SOURCE_TYPE_GENERIC) {
				continue;
			}

			if (FD_ISSET(event_source->handle, fd_read_set)) {
				received_events |= EVENT_READ;
			}

			if (FD_ISSET(event_source->handle, fd_write_set)) {
				received_events |= EVENT_WRITE;
			}

			if (FD_ISSET(event_source->handle, fd_error_set)) {
				received_events |= EVENT_ERROR;
			}

			if (received_events == 0) {
				continue;
			}

			event_handle_source(event_source, received_events);

			++handled;
		}

		if (ready == handled) {
			log_event_debug("Handled all ready %s event sources",
			                event_get_source_type_name(EVENT_SOURCE_TYPE_GENERIC, false));
		} else if (*running) {
			log_warn("Handled only %d of %d ready %s event source(s)",
			         handled, ready,
			         event_get_source_type_name(EVENT_SOURCE_TYPE_GENERIC, false));
		}

		// now cleanup event sources that got marked as disconnected/removed
		// during the event handling
		cleanup();
		event_cleanup_sources();
	}

	result = 0;

cleanup:
	*running = false;

	return result;
}
