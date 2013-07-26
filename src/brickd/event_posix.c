/*
 * brickd
 * Copyright (C) 2012-2013 Matthias Bolte <matthias@tinkerforge.com>
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
#include "usb.h"

#define LOG_CATEGORY LOG_CATEGORY_EVENT

static Array _pollfds = ARRAY_INITIALIZER;
static EventHandle _signal_pipe[2] = { INVALID_EVENT_HANDLE,
                                       INVALID_EVENT_HANDLE };

static void event_handle_signal(void *opaque) {
	int signal_number;

	(void)opaque;

	if (pipe_read(_signal_pipe[0], &signal_number, sizeof(signal_number)) < 0) {
		log_error("Could not read from signal pipe: %s (%d)",
		          get_errno_name(errno), errno);

		return;
	}

	if (signal_number == SIGINT) {
		log_info("Received SIGINT");
	} else if (signal_number == SIGTERM) {
		log_info("Received SIGTERM");
	} else if (signal_number == SIGUSR1) {
		log_info("rescanning USB...");
		usb_update();
		return;
	} else {
		log_warn("Received unexpected signal %d", signal_number);

		return;
	}

	event_stop();
}

static void event_forward_signal(int signal_number) {
	pipe_write(_signal_pipe[1], &signal_number, sizeof(signal_number));
}

int event_init_platform(void) {
	int phase = 0;

	// create pollfd array
	if (array_create(&_pollfds, 32, sizeof(struct pollfd), 1) < 0) {
		log_error("Could not create pollfd array: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 1;

	// create signal pipe
	if (pipe_create(_signal_pipe) < 0) {
		log_error("Could not create signal pipe: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 2;

	if (event_add_source(_signal_pipe[0], EVENT_SOURCE_TYPE_GENERIC,
	                     EVENT_READ, event_handle_signal, NULL) < 0) {
		goto cleanup;
	}

	phase = 3;

	// setup signal handlers
	if (signal(SIGINT, event_forward_signal) == SIG_ERR) {
		log_error("Could install signal handler for SIGINT: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 4;

	if (signal(SIGTERM, event_forward_signal) == SIG_ERR) {
		log_error("Could install signal handler for SIGTERM: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}
	if (signal(SIGUSR1, event_forward_signal) == SIG_ERR) {
		log_error("Could install signal handler for SIGUSR1: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 5;

cleanup:
	switch (phase) { // no breaks, all cases fall through intentionally
	case 4:
		signal(SIGINT, SIG_DFL);

	case 3:
		event_remove_source(_signal_pipe[0], EVENT_SOURCE_TYPE_GENERIC);

	case 2:
		pipe_destroy(_signal_pipe);

	case 1:
		array_destroy(&_pollfds, NULL);

	default:
		break;
	}

	return phase == 5 ? 0 : -1;
}

void event_exit_platform(void) {
	signal(SIGINT, SIG_DFL);
	signal(SIGTERM, SIG_DFL);

	event_remove_source(_signal_pipe[0], EVENT_SOURCE_TYPE_GENERIC);
	pipe_destroy(_signal_pipe);

	array_destroy(&_pollfds, NULL);
}

int event_run_platform(Array *event_sources, int *running) {
	int i;
	EventSource *event_source;
	struct pollfd *pollfd;
	int ready;
	int handled;

	*running = 1;

	event_cleanup_sources();

	while (*running) {
		// update pollfd array
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

		// start to poll
		log_debug("Starting to poll on %d event source(s)", _pollfds.count);

		ready = poll((struct pollfd *)_pollfds.bytes, _pollfds.count, -1);

		if (ready < 0) {
			if (errno_interrupted()) {
				log_debug("Poll got interrupted");

				continue;
			}

			log_error("Count not poll on event source(s): %s (%d)",
			          get_errno_name(errno), errno);

			*running = 0;

			return -1;
		}

		// handle poll result
		log_debug("Poll returned %d event source(s) as ready", ready);

		handled = 0;

		// this loop assumes that event source array and pollfd array can be
		// matched by index. this means that the first n items of the event
		// source array (with n = items in pollfd array) are not removed
		// or replaced during the iteration over the pollfd array. because
		// of this event_remove_source only marks event sources as removed,
		// the actual removal is done after this loop
		for (i = 0; i < _pollfds.count && ready > handled; ++i) {
			pollfd = array_get(&_pollfds, i);

			if (pollfd->revents == 0) {
				continue;
			}

			event_source = array_get(event_sources, i);

			if (event_source->state != EVENT_SOURCE_STATE_NORMAL) {
				log_debug("Ignoring %s event source (handle: %d, received events: %d) in transition at index %d",
				          event_get_source_type_name(event_source->type, 0),
				          event_source->handle, pollfd->revents, i);
			} else {
				log_debug("Handling %s event source (handle: %d, received events: %d) at index %d",
				          event_get_source_type_name(event_source->type, 0),
				          event_source->handle, pollfd->revents, i);

				if (event_source->function != NULL) {
					event_source->function(event_source->opaque);
				}
			}

			++handled;

			if (!*running) {
				break;
			}
		}

		if (ready == handled) {
			log_debug("Handled all ready event sources");
		} else {
			log_warn("Handled only %d of %d ready event source(s)",
			         handled, ready);
		}

		// now remove event sources that got marked as removed during the
		// event handling
		event_cleanup_sources();
	}

	return 0;
}

int event_stop_platform(void) {
	// nothing to do, the signal pipe already interrupted the running poll
	return 0;
}
