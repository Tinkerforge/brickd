/*
 * brickd
 * Copyright (C) 2012 Matthias Bolte <matthias@tinkerforge.com>
 *
 * event.h: Event specific functions
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

#ifndef BRICKD_EVENT_H
#define BRICKD_EVENT_H

#ifdef _WIN32
	#include <winsock2.h>
#else
	#include <poll.h>
#endif

#ifdef _WIN32
typedef SOCKET EventHandle;
#else
typedef int EventHandle;
#endif

#ifdef _WIN32
	#define INVALID_EVENT_HANDLE INVALID_SOCKET
#else
	#define INVALID_EVENT_HANDLE (-1)
#endif

typedef void (*EventFunction)(void *opaque);

typedef enum {
#ifdef _WIN32
	EVENT_READ = 1 << 0,
	EVENT_WRITE = 1 << 2
#else
	EVENT_READ = POLLIN,
	EVENT_WRITE = POLLOUT
#endif
} Event;

typedef enum {
	EVENT_SOURCE_TYPE_GENERIC = 0,
	EVENT_SOURCE_TYPE_USB
} EventSourceType;

typedef enum {
	EVENT_SOURCE_STATE_NORMAL = 0,
	EVENT_SOURCE_STATE_ADDED,
	EVENT_SOURCE_STATE_REMOVED,
	EVENT_SOURCE_STATE_READDED
} EventSourceState;

typedef struct {
	EventHandle handle;
	EventSourceType type;
	int events;
	EventSourceState state;
	EventFunction function;
	void *opaque;
} EventSource;

const char *event_get_source_type_name(EventSourceType type, int upper);

int event_init(void);
void event_exit(void);

int event_add_source(EventHandle handle, EventSourceType type,
                     int events, EventFunction function, void *opaque);
int event_remove_source(EventHandle handle, EventSourceType type);
void event_cleanup_sources(void);

int event_run(void);
void event_stop(void);

#endif // BRICKD_EVENT_H
