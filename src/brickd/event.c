/*
 * brickd
 * Copyright (C) 2012 Matthias Bolte <matthias@tinkerforge.com>
 *
 * event.c: Event specific functions
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

#include "event.h"

#include "log.h"
#include "pipe.h"
#include "utils.h"

#define LOG_CATEGORY LOG_CATEGORY_EVENT

static Array _event_sources = ARRAY_INITIALIZER;
static int _running = 0;
static int _stop_requested = 0;

extern int event_init_platform(void);
extern void event_exit_platform(void);
extern int event_run_platform(Array *sources, int *running);
extern int event_stop_platform(void);

const char *event_get_source_type_name(EventSourceType type, int upper) {
	switch (type) {
	case EVENT_SOURCE_TYPE_GENERIC:
		return upper ? "Generic" : "generic";

	case EVENT_SOURCE_TYPE_USB:
		return "USB";

	default:
		return upper ? "<Unknown>" : "<unknown>";
	}
}

int event_init(void) {
	log_debug("Initializing event subsystem");

	if (array_create(&_event_sources, 32, sizeof(EventSource), 1) < 0) {
		log_error("Could not create event source array: %s (%d)",
		          get_errno_name(errno), errno);

		return -1;
	}

	if (event_init_platform() < 0) {
		array_destroy(&_event_sources, NULL);

		return -1;
	}

	return 0;
}

void event_exit(void) {
	log_debug("Shutting down event subsystem");

	event_exit_platform();

	event_cleanup_sources();

	if (_event_sources.count > 0) {
		log_warn("Leaking %d event sources", _event_sources.count);
	}

	array_destroy(&_event_sources, NULL);
}

int event_add_source(EventHandle handle, EventSourceType type, int events,
                     EventFunction function, void *opaque) {
	int i;
	EventSource *event_source;

	// check existing event sources
	for (i = 0; i < _event_sources.count; i++) {
		event_source = array_get(&_event_sources, i);

		if (event_source->handle == handle &&
		    event_source->type == type) {
			if (event_source->state == EVENT_SOURCE_STATE_REMOVED) {
				event_source->events = events;
				event_source->state = EVENT_SOURCE_STATE_READDED;
				event_source->function = function;
				event_source->opaque = opaque;

				log_debug("Readded %s event source (handle: %d, events: %d) at index %d",
				          event_get_source_type_name(type, 0),
				          handle, events, i);

				return 0;
			}

			log_error("%s event source (handle: %d, events: %d) already added at index %d",
			          event_get_source_type_name(type, 1),
			          event_source->handle, event_source->events, i);

			return -1;
		}
	}

	// add event source
	event_source = array_append(&_event_sources);

	if (event_source == NULL) {
		log_error("Could not append to event source array: %s (%d)",
		          get_errno_name(errno), errno);

		return -1;
	}

	event_source->handle = handle;
	event_source->type = type;
	event_source->events = events;
	event_source->state = EVENT_SOURCE_STATE_ADDED;
	event_source->function = function;
	event_source->opaque = opaque;

	log_debug("Added %s event source (handle: %d, events: %d) at index %d",
	          event_get_source_type_name(type, 0),
	          handle, events, _event_sources.count - 1);

	return 0;
}

// only mark event sources as removed here, because the event loop might be in
// the middle of iterating the event sources array when this function is called
int event_remove_source(EventHandle handle, EventSourceType type) {
	int i;
	EventSource *event_source;

	// iterate backwards to remove the last added instance of an event source
	for (i = _event_sources.count - 1; i >= 0; --i) {
		event_source = array_get(&_event_sources, i);

		if (event_source->handle == handle &&
		    event_source->type == type) {
			if (event_source->state == EVENT_SOURCE_STATE_REMOVED) {
				log_warn("%s event source (handle: %d, events: %d) already marked as removed at index %d",
				         event_get_source_type_name(event_source->type, 1),
				         event_source->handle, event_source->events, i);
			} else {
				event_source->state = EVENT_SOURCE_STATE_REMOVED;

				log_debug("Marked %s event source (handle: %d, events: %d) as removed at index %d",
				          event_get_source_type_name(event_source->type, 0),
				          event_source->handle, event_source->events, i);
			}

			return 0;
		}
	}

	log_warn("Could not mark unknown %s event source (handle: %d) as removed",
	         event_get_source_type_name(type, 0), handle);

	return -1;
}

// remove event sources that got marked as removed
void event_cleanup_sources(void) {
	int i;
	EventSource *event_source;

	// iterate backwards for simpler index handling and to be able to print
	// the correct index
	for (i = _event_sources.count - 1; i >= 0; --i) {
		event_source = array_get(&_event_sources, i);

		if (event_source->state == EVENT_SOURCE_STATE_REMOVED) {
			log_debug("Removed %s event source (handle: %d, events: %d) at index %d",
			          event_get_source_type_name(event_source->type, 0),
			          event_source->handle, event_source->events, i);

			array_remove(&_event_sources, i, NULL);
		} else {
			event_source->state = EVENT_SOURCE_STATE_NORMAL;
		}
	}
}

int event_run(void) {
	int rc;

	if (_running) {
		log_warn("Event loop already running");

		return 0;
	}

	if (_stop_requested) {
		log_debug("Not starting the event loop, stop was requested");

		return 0;
	}

	log_debug("Starting the event loop");

	rc = event_run_platform(&_event_sources, &_running);

	if (rc < 0) {
		log_error("Event loop aborted");
	} else {
		log_debug("Event loop stopped");
	}

	return rc;
}

void event_stop(void) {
	_stop_requested = 1;

	if (!_running) {
		return;
	}

	_running = 0;

	log_debug("Stopping the event loop");

	event_stop_platform();
}
