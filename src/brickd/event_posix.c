/*
 * brickd
 * Copyright (C) 2012 Matthias Bolte <matthias@tinkerforge.com>
 *
 * event_posix.c: Poll based event loop
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
#include <poll.h>
#include <signal.h>

#include "event.h"

#include "log.h"
#include "pipe.h"
#include "utils.h"

#define LOG_CATEGORY LOG_CATEGORY_EVENT

static Array _pollfds = ARRAY_INITIALIZER;
static EventHandle _signal_pipe[2] = { INVALID_EVENT_HANDLE,
                                       INVALID_EVENT_HANDLE };

static void event_handle_signal(void *opaque) {
	int rc;
	int signal_number;

	(void)opaque;

	// FIXME: handle partial reads?
	do {
		rc = pipe_read(_signal_pipe[0], &signal_number, sizeof(int));
	} while (rc < 0 && errno_interrupted());

	if (rc < 0) {
		log_error("Could not read from signal pipe: %s (%d)",
		          get_errno_name(errno), errno);

		return;
	}

	if (signal_number == SIGINT) {
		log_info("Received SIGINT");
	} else if (signal_number == SIGTERM) {
		log_info("Received SIGTERM");
	} else {
		log_warn("Received unexpected signal %d", signal_number);

		return;
	}

	event_stop();
}

static void event_forward_signal(int signal_number) {
	int rc;

	// FIXME: handle partial writes?
	do {
		rc = pipe_write(_signal_pipe[1], &signal_number, sizeof(int));
	} while (rc < 0 && errno_interrupted());
}

int event_init_platform(void) {
	if (array_create(&_pollfds, 32, sizeof(struct pollfd)) < 0) {
		log_error("Could not create pollfd array: %s (%d)",
		          get_errno_name(errno), errno);

		return -1;
	}

	if (pipe_create(_signal_pipe) < 0) {
		// FIXME: free array
		log_error("Could not create signal pipe: %s (%d)",
		          get_errno_name(errno), errno);

		return -1;
	}

	if (event_add_source(_signal_pipe[0], EVENT_READ,
	                     event_handle_signal, NULL) < 0) {
		// FIXME: free array, destroy pipe
		return -1;
	}

	if (signal(SIGINT, event_forward_signal) == SIG_ERR) {
		// FIXME: free array, destroy pipe
		log_error("Could install signal handler for SIGINT: %s (%d)",
		          get_errno_name(errno), errno);

		return -1;
	}

	if (signal(SIGTERM, event_forward_signal) == SIG_ERR) {
		// FIXME: free array, destroy pipe
		log_error("Could install signal handler for SIGTERM: %s (%d)",
		          get_errno_name(errno), errno);

		return -1;
	}

	return 0;
}

void event_exit_platform(void) {
	// FIXME Close _signal_pipe

	array_destroy(&_pollfds, NULL);
	pipe_destroy(_signal_pipe);
}

int event_run_platform(Array *event_sources, int *running) {
	int i;
	EventSource *event_source;
	struct pollfd *pollfd;
	int ready;

	*running = 1;

	while (*running) {
		if (array_resize(&_pollfds, event_sources->count, NULL) < 0) {
			log_error("Could not resize pollfd array: %s (%d)",
			          get_errno_name(errno), errno);

			return -1;
		}

		for (i = 0; i < event_sources->count; i++) {
			event_source = array_get(event_sources, i);
			pollfd = array_get(&_pollfds, i);

			pollfd->fd = event_source->handle;
			pollfd->events = event_source->events;
			pollfd->revents = 0;
		}

		log_debug("Starting to poll on %d event source(s)", _pollfds.count);

		ready = poll((struct pollfd *)_pollfds.bytes, _pollfds.count, -1);

		if (ready < 0) {
			if (errno_interrupted()) {
				log_debug("Poll got interrupted");
				continue;
			}

			*running = 0;

			log_error("Count not poll on event source(s): %s (%d)",
			          get_errno_name(errno), errno);

			return -1;
		}

		log_debug("Poll returned %d event source(s) as ready", ready);

		for (i = 0; i < _pollfds.count && ready > 0; ++i) {
			pollfd = array_get(&_pollfds, i);

			if (pollfd->revents == 0) {
				continue;
			}

			--ready;

			event_source = array_get(event_sources, i);

			if (event_source->removed) {
				log_debug("Ignoring event source (handle: %d, received events: %d) marked as removed at index %d",
				          event_source->handle, pollfd->revents, i);
			} else {
				log_debug("Handling event source (handle: %d, received events: %d) at index %d",
				          event_source->handle, pollfd->revents, i);

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
