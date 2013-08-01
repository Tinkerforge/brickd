/*
 * brickd
 * Copyright (C) 2012-2013 Matthias Bolte <matthias@tinkerforge.com>
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
#include <libusb.h>
#include <stddef.h>
#include <stdlib.h>

#include "event.h"

#include "log.h"
#include "network.h"
#include "pipe.h"
#include "threads.h"
#include "utils.h"

#define LOG_CATEGORY LOG_CATEGORY_EVENT

#define USBI_POLLIN 0x0001
#define USBI_POLLOUT 0x0004

struct usbi_pollfd {
	int fd;
	short events;
	short revents;
};

extern int LIBUSB_CALL usbi_pipe(int filedes[2]);
extern int LIBUSB_CALL usbi_poll(struct usbi_pollfd *fds, unsigned int nfds,
                                 int timeout);
extern int LIBUSB_CALL usbi_close(int fd);
extern ssize_t LIBUSB_CALL usbi_write(int fd, const void *buf, size_t count);
extern ssize_t LIBUSB_CALL usbi_read(int fd, void *buf, size_t count);

// mimic fd_set
typedef struct {
	int allocated;
	int count;
	SOCKET sockets[0];
} SocketSet;

typedef struct {
	int running;
	int stuck;
	int suspend_pipe[2];
	EventHandle ready_pipe[2];
	Semaphore resume;
	Semaphore suspend;
	Array pollfds;
	Thread thread;
} USBPoller;

static SocketSet *_socket_read_set = NULL;
static SocketSet *_socket_write_set = NULL;
static EventHandle _stop_pipe[2] = { INVALID_EVENT_HANDLE,
                                     INVALID_EVENT_HANDLE };
static USBPoller _usb_poller;

static int event_reserve_socket_set(SocketSet **socket_set, int size) {
	SocketSet *bytes;

	if (*socket_set != NULL && (*socket_set)->allocated >= size) {
		return 0;
	}

	size = GROW_ALLOCATION(size);

	if (*socket_set != NULL) {
		bytes = realloc(*socket_set, sizeof(SocketSet) + sizeof(SOCKET) * size);
	} else {
		bytes = calloc(1, sizeof(SocketSet) + sizeof(SOCKET) * size);
	}

	if (bytes == NULL) {
		errno = ENOMEM;

		return -1;
	}

	*socket_set = bytes;
	(*socket_set)->allocated = size;

	return 0;
}

static fd_set *event_get_socket_set_as_fd_set(SocketSet *socket_set) {
	return (fd_set *)((uint8_t *)socket_set + offsetof(SocketSet, count));
}

static void event_poll_usb_events(void *opaque) {
	Array *event_sources = opaque;
	int count;
	struct usbi_pollfd *pollfd;
	EventSource *event_source;
	int i;
	int k;
	int ready;

	log_debug("Started USB poll thread");

	while (1) {
		semaphore_acquire(&_usb_poller.resume);

		log_debug("Resumed USB poll thread");

		if (!_usb_poller.running) {
			goto cleanup;
		}

		// update pollfd array
		count = 0;

		for (i = 0; i < event_sources->count; i++) {
			event_source = array_get(event_sources, i);

			if (event_source->type == EVENT_SOURCE_TYPE_USB) {
				++count;
			}
		}

		if (count == 0) {
			goto suspend;
		}

		++count; // add the suspend pipe

		if (array_resize(&_usb_poller.pollfds, count, NULL) < 0) {
			log_error("Could not resize USB pollfd array: %s (%d)",
			          get_errno_name(errno), errno);

			goto cleanup;
		}

		pollfd = array_get(&_usb_poller.pollfds, 0);

		pollfd->fd = _usb_poller.suspend_pipe[0];
		pollfd->events = USBI_POLLIN;
		pollfd->revents = 0;

		for (i = 0, k = 1; i < event_sources->count; i++) {
			event_source = array_get(event_sources, i);

			if (event_source->type != EVENT_SOURCE_TYPE_USB) {
				continue;
			}

			pollfd = array_get(&_usb_poller.pollfds, k);

			pollfd->fd = event_source->handle;
			pollfd->events = (short)event_source->events;
			pollfd->revents = 0;

			++k;
		}

		// start to poll
		log_debug("Starting to poll on %d %s event source(s)",
		          _usb_poller.pollfds.count - 1,
		          event_get_source_type_name(EVENT_SOURCE_TYPE_USB, 0));

	retry:
		ready = usbi_poll((struct usbi_pollfd *)_usb_poller.pollfds.bytes,
		                  _usb_poller.pollfds.count, -1);

		if (ready < 0) {
			if (errno_interrupted()) {
				log_debug("Poll got interrupted, retrying");

				goto retry;
			}

			log_error("Could not poll on %s event source(s): %s (%d)",
			          event_get_source_type_name(EVENT_SOURCE_TYPE_USB, 0),
			          get_errno_name(errno), errno);

			goto suspend;
		}

		if (ready == 0) {
			goto suspend;
		}

		// handle poll result
		pollfd = array_get(&_usb_poller.pollfds, 0);

		if (pollfd->revents != 0) {
			log_debug("Received suspend signal");

			--ready; // remove the suspend pipe
		}

		if (ready == 0) {
			goto suspend;
		}

		log_debug("Poll returned %d %s event source(s) as ready",
		          ready,
		          event_get_source_type_name(EVENT_SOURCE_TYPE_USB, 0));

		if (pipe_write(_usb_poller.ready_pipe[1], &ready, sizeof(ready)) < 0) {
			log_error("Could not write to USB ready pipe: %s (%d)",
			          get_errno_name(errno), errno);

			goto cleanup;
		}

	suspend:
		log_debug("Suspending USB poll thread");

		semaphore_release(&_usb_poller.suspend);
	}

cleanup:
	log_debug("Stopped USB poll thread");

	semaphore_release(&_usb_poller.suspend);

	_usb_poller.running = 0;
}

static void event_forward_usb_events(void *opaque) {
	Array *event_sources = opaque;
	int ready;
	int handled = 0;
	int i;
	int k;
	EventSource *event_source;
	struct usbi_pollfd *pollfd;

	(void)opaque;

	if (pipe_read(_usb_poller.ready_pipe[0], &ready, sizeof(ready)) < 0) {
		log_error("Could not read from USB ready pipe: %s (%d)",
		          get_errno_name(errno), errno);

		return;
	}

	for (i = 0, k = 1; i < event_sources->count && k < _usb_poller.pollfds.count && ready > handled; ++i) {
		event_source = array_get(event_sources, i);

		if (event_source->type != EVENT_SOURCE_TYPE_USB) {
			continue;
		}

		pollfd = array_get(&_usb_poller.pollfds, k);

		if ((int)event_source->handle != pollfd->fd) {
			continue;
		}

		++k;

		if (pollfd->revents == 0) {
			continue;
		}

		if (event_source->state != EVENT_SOURCE_STATE_NORMAL) {
			log_debug("Ignoring %s event source (handle: %d, received events: %d) marked as removed at index %d",
			          event_get_source_type_name(event_source->type, 0),
			          event_source->handle, pollfd->revents, i);
		} else {
			log_debug("Handling %s event source (handle: %d, received events: %d) at index %d",
			          event_get_source_type_name(event_source->type, 0),
			          event_source->handle, pollfd->revents, i);

			event_source->function(event_source->opaque);
		}

		++handled;
	}

	if (ready == handled) {
		log_debug("Handled all ready %s event sources",
		          event_get_source_type_name(EVENT_SOURCE_TYPE_USB, 0));
	} else {
		log_warn("Handled only %d of %d ready %s event source(s)",
		         handled, ready,
		         event_get_source_type_name(EVENT_SOURCE_TYPE_USB, 0));
	}
}

int event_init_platform(void) {
	int phase = 0;
	int count = 32;

	// create read set
	if (event_reserve_socket_set(&_socket_read_set, count) < 0) {
		log_error("Could not create socket read set: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 1;

	// create write set
	if (event_reserve_socket_set(&_socket_write_set, count) < 0) {
		log_error("Could not create socket write set: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 2;

	// create stop pipe
	if (pipe_create(_stop_pipe) < 0) {
		log_error("Could not create stop pipe: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 3;

	if (event_add_source(_stop_pipe[0], EVENT_SOURCE_TYPE_GENERIC,
	                     EVENT_READ, NULL, NULL) < 0) {
		goto cleanup;
	}

	phase = 4;

	// create USB poller
	_usb_poller.running = 0;
	_usb_poller.stuck = 0;

	if (usbi_pipe(_usb_poller.suspend_pipe) < 0) {
		log_error("Could not create USB suspend pipe");

		goto cleanup;
	}

	phase = 5;

	if (pipe_create(_usb_poller.ready_pipe) < 0) {
		log_error("Could not create USB ready pipe: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 6;

	if (array_create(&_usb_poller.pollfds, 32, sizeof(struct usbi_pollfd), 1) < 0) {
		log_error("Could not create USB pollfd array: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 7;

	if (semaphore_create(&_usb_poller.resume) < 0) {
		log_error("Could not create USB resume semaphore: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 8;

	if (semaphore_create(&_usb_poller.suspend) < 0) {
		log_error("Could not create USB suspend semaphore: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 9;

cleanup:
	switch (phase) { // no breaks, all cases fall through intentionally
	case 8:
		semaphore_destroy(&_usb_poller.suspend);

	case 7:
		semaphore_destroy(&_usb_poller.resume);

	case 6:
		pipe_destroy(_usb_poller.ready_pipe);

	case 5:
		usbi_close(_usb_poller.suspend_pipe[0]);
		usbi_close(_usb_poller.suspend_pipe[1]);

	case 4:
		event_remove_source(_stop_pipe[0], EVENT_SOURCE_TYPE_GENERIC);

	case 3:
		pipe_destroy(_stop_pipe);

	case 2:
		free(_socket_write_set);

	case 1:
		free(_socket_read_set);

	default:
		break;
	}

	return phase == 9 ? 0 : -1;
}

void event_exit_platform(void) {
	thread_destroy(&_usb_poller.thread);

	semaphore_destroy(&_usb_poller.resume);
	semaphore_destroy(&_usb_poller.suspend);

	array_destroy(&_usb_poller.pollfds, NULL);

	pipe_destroy(_usb_poller.ready_pipe);

	usbi_close(_usb_poller.suspend_pipe[0]);
	usbi_close(_usb_poller.suspend_pipe[1]);

	event_remove_source(_stop_pipe[0], EVENT_SOURCE_TYPE_GENERIC);
	pipe_destroy(_stop_pipe);

	free(_socket_write_set);
	free(_socket_read_set);
}

int event_run_platform(Array *event_sources, int *running) {
	int result = -1;
	int i;
	EventSource *event_source;
	fd_set *fd_read_set;
	fd_set *fd_write_set;
	int ready;
	int handled;
	uint8_t byte = 1;
	int rc;
	int event_source_count;
	int received_events;

	if (event_add_source(_usb_poller.ready_pipe[0], EVENT_SOURCE_TYPE_GENERIC,
	                     EVENT_READ, event_forward_usb_events,
	                     event_sources) < 0) {
		return -1;
	}

	*running = 1;

	_usb_poller.running = 1;

	thread_create(&_usb_poller.thread, event_poll_usb_events, event_sources);

	network_cleanup_clients();
	event_cleanup_sources();

	while (*running) {
		// update SocketSet arrays
		if (event_reserve_socket_set(&_socket_read_set, // FIXME: this over allocates
		                             event_sources->count) < 0) {
			log_error("Could not resize socket read set: %s (%d)",
			          get_errno_name(errno), errno);

			goto cleanup;
		}

		if (event_reserve_socket_set(&_socket_write_set, // FIXME: this over allocates
		                             event_sources->count) < 0) {
			log_error("Could not resize socket write set: %s (%d)",
			          get_errno_name(errno), errno);

			goto cleanup;
		}

		_socket_read_set->count = 0;
		_socket_write_set->count = 0;

		for (i = 0; i < event_sources->count; i++) {
			event_source = array_get(event_sources, i);

			if (event_source->type != EVENT_SOURCE_TYPE_GENERIC) {
				continue;
			}

			if (event_source->events & EVENT_READ) {
				_socket_read_set->sockets[_socket_read_set->count++] = event_source->handle;
			}

			if (event_source->events & EVENT_WRITE) {
				_socket_write_set->sockets[_socket_write_set->count++] = event_source->handle;
			}
		}

		// start to select
		log_debug("Starting to select on %d + %d %s event source(s)",
		          _socket_read_set->count, _socket_write_set->count,
		          event_get_source_type_name(EVENT_SOURCE_TYPE_GENERIC, 0));

		semaphore_release(&_usb_poller.resume);

		fd_read_set = event_get_socket_set_as_fd_set(_socket_read_set);
		fd_write_set = event_get_socket_set_as_fd_set(_socket_write_set);

		ready = select(0, fd_read_set, fd_write_set, NULL, NULL);

		if (_usb_poller.running) {
			log_debug("Sending suspend signal to USB poll thread");

			if (usbi_write(_usb_poller.suspend_pipe[1], &byte, 1) < 0) {
				log_error("Could not write to USB suspend pipe");

				_usb_poller.stuck = 1;
				*running = 0;

				goto cleanup;
			}

			semaphore_acquire(&_usb_poller.suspend);

			if (usbi_read(_usb_poller.suspend_pipe[0], &byte, 1) < 0) {
				log_error("Could not read from USB suspend pipe");

				_usb_poller.stuck = 1;
				*running = 0;

				goto cleanup;
			}
		}

		if (ready == SOCKET_ERROR) {
			rc = ERRNO_WINAPI_OFFSET + WSAGetLastError();

			if (rc == ERRNO_WINAPI_OFFSET + WSAEINTR) {
				continue;
			}

			log_error("Could not select on %s event sources: %s (%d)",
			          event_get_source_type_name(EVENT_SOURCE_TYPE_GENERIC, 0),
			          get_errno_name(rc), rc);

			*running = 0;

			goto cleanup;
		}

		// handle select result
		log_debug("Select returned %d %s event source(s) as ready", ready,
		          event_get_source_type_name(EVENT_SOURCE_TYPE_GENERIC, 0));

		handled = 0;

		// cache event source count here to avoid looking at new event
		// sources that got added during the event handling
		event_source_count = event_sources->count;

		for (i = 0; i < event_source_count && ready > handled; ++i) {
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

			if (received_events == 0) {
				continue;
			}

			if (event_source->state != EVENT_SOURCE_STATE_NORMAL) {
				log_debug("Ignoring %s event source (handle: %d, received events: %d) marked as removed at index %d",
				          event_get_source_type_name(event_source->type, 0),
				          event_source->handle, received_events, i);
			} else {
				log_debug("Handling %s event source (handle: %d, received events: %d) at index %d",
				          event_get_source_type_name(event_source->type, 0),
				          event_source->handle, received_events, i);

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
			log_debug("Handled all ready %s event sources",
			          event_get_source_type_name(EVENT_SOURCE_TYPE_GENERIC, 0));
		} else {
			log_warn("Handled only %d of %d ready %s event source(s)",
			         handled, ready,
			         event_get_source_type_name(EVENT_SOURCE_TYPE_GENERIC, 0));
		}

		// now remove clients and event sources that got marked as
		// disconnected/removed during the event handling
		network_cleanup_clients();
		event_cleanup_sources();
	}

	result = 0;

cleanup:
	if (_usb_poller.running && !_usb_poller.stuck) {
		_usb_poller.running = 0;

		log_debug("Stopping USB poll thread");

		if (usbi_write(_usb_poller.suspend_pipe[1], &byte, 1) < 0) {
			log_error("Could not write to USB suspend pipe");
		} else {
			semaphore_release(&_usb_poller.resume);
			thread_join(&_usb_poller.thread);
		}
	}

	event_remove_source(_usb_poller.ready_pipe[0], EVENT_SOURCE_TYPE_GENERIC);

	return result;
}

int event_stop_platform(void) {
	uint8_t byte = 0;

	if (pipe_write(_stop_pipe[1], &byte, sizeof(byte)) < 0) {
		log_error("Could not write to stop pipe: %s (%d)",
		          get_errno_name(errno), errno);

		return -1;
	}

	return 0;
}
