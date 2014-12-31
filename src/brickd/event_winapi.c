/*
 * brickd
 * Copyright (C) 2012-2014 Matthias Bolte <matthias@tinkerforge.com>
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
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

#include <daemonlib/array.h>
#include <daemonlib/event.h>
#include <daemonlib/log.h>
#include <daemonlib/macros.h>
#include <daemonlib/pipe.h>
#include <daemonlib/threads.h>
#include <daemonlib/utils.h>

static LogSource _log_source = LOG_SOURCE_INITIALIZER;

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

static SocketSet *_socket_read_set = NULL;
static SocketSet *_socket_write_set = NULL;
static SocketSet *_socket_error_set = NULL;
static Pipe _stop_pipe;
static bool _usb_poll_running;
static bool _usb_poll_stuck;
static int _usb_poll_suspend_pipe[2]; // libusb pipe
static Pipe _usb_poll_ready_pipe;
static Semaphore _usb_poll_resume;
static Semaphore _usb_poll_suspend;
static Array _usb_poll_pollfds;
static int _usb_poll_pollfds_ready;
static Thread _usb_poll_thread;

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

static void event_poll_usb_events(void *opaque) {
	Array *event_sources = opaque;
	int count;
	struct usbi_pollfd *pollfd;
	EventSource *event_source;
	int i;
	int k;
	int ready;
	uint8_t byte = 0;

	log_debug("Started USB poll thread");

	for (;;) {
		semaphore_acquire(&_usb_poll_resume);

		log_debug("Resumed USB poll thread");

		if (!_usb_poll_running) {
			goto cleanup;
		}

		_usb_poll_pollfds_ready = 0;

		// update pollfd array
		count = 0;

		for (i = 0; i < event_sources->count; ++i) {
			event_source = array_get(event_sources, i);

			if (event_source->type == EVENT_SOURCE_TYPE_USB) {
				++count;
			}
		}

		if (count == 0) {
			goto suspend;
		}

		++count; // add the suspend pipe

		if (array_resize(&_usb_poll_pollfds, count, NULL) < 0) {
			log_error("Could not resize USB pollfd array: %s (%d)",
			          get_errno_name(errno), errno);

			goto cleanup;
		}

		pollfd = array_get(&_usb_poll_pollfds, 0);

		pollfd->fd = _usb_poll_suspend_pipe[0];
		pollfd->events = USBI_POLLIN;
		pollfd->revents = 0;

		for (i = 0, k = 1; i < event_sources->count; ++i) {
			event_source = array_get(event_sources, i);

			if (event_source->type != EVENT_SOURCE_TYPE_USB) {
				continue;
			}

			pollfd = array_get(&_usb_poll_pollfds, k);

			pollfd->fd = event_source->handle;
			pollfd->events = (short)event_source->events;
			pollfd->revents = 0;

			++k;
		}

		// start to poll
		log_debug("Starting to poll on %d %s event source(s)",
		          _usb_poll_pollfds.count - 1,
		          event_get_source_type_name(EVENT_SOURCE_TYPE_USB, false));

	retry:
		ready = usbi_poll((struct usbi_pollfd *)_usb_poll_pollfds.bytes,
		                  _usb_poll_pollfds.count, -1);

		if (ready < 0) {
			if (errno_interrupted()) {
				log_debug("Poll got interrupted, retrying");

				goto retry;
			}

			log_error("Could not poll on %s event source(s): %s (%d)",
			          event_get_source_type_name(EVENT_SOURCE_TYPE_USB, false),
			          get_errno_name(errno), errno);

			goto suspend;
		}

		if (ready == 0) {
			goto suspend;
		}

		// handle poll result
		pollfd = array_get(&_usb_poll_pollfds, 0);

		if (pollfd->revents != 0) {
			log_debug("Received suspend signal");

			--ready; // remove the suspend pipe
		}

		if (ready == 0) {
			goto suspend;
		}

		log_debug("Poll returned %d %s event source(s) as ready", ready,
		          event_get_source_type_name(EVENT_SOURCE_TYPE_USB, false));

		_usb_poll_pollfds_ready = ready;

		if (pipe_write(&_usb_poll_ready_pipe, &byte, sizeof(byte)) < 0) {
			log_error("Could not write to USB ready pipe: %s (%d)",
			          get_errno_name(errno), errno);

			goto cleanup;
		}

	suspend:
		log_debug("Suspending USB poll thread");

		semaphore_release(&_usb_poll_suspend);
	}

cleanup:
	log_debug("Stopped USB poll thread");

	semaphore_release(&_usb_poll_suspend);

	_usb_poll_running = false;
}

static void event_forward_usb_events(void *opaque) {
	Array *event_sources = opaque;
	uint8_t byte;
	int handled = 0;
	int event_source_count;
	int i;
	int k;
	EventSource *event_source;
	struct usbi_pollfd *pollfd;

	(void)opaque;

	if (pipe_read(&_usb_poll_ready_pipe, &byte, sizeof(byte)) < 0) {
		log_error("Could not read from USB ready pipe: %s (%d)",
		          get_errno_name(errno), errno);

		return;
	}

	if (_usb_poll_pollfds.count == 0) {
		return;
	}

	// cache event source count here to avoid looking at new event
	// sources that got added during the event handling
	event_source_count = event_sources->count;

	// this loop assumes that the USB subset of the event source array and the
	// pollfd array can be matched by index. this means that the first N USB
	// items of the event source array (with N = items in pollfd array - 1) are
	// not removed or replaced during the iteration over the pollfd array.
	// because of this event_remove_source only marks event sources as removed,
	// the actual removal is done later by event_cleanup_sources
	for (i = 0, k = 1; i < event_source_count && k < _usb_poll_pollfds.count && _usb_poll_pollfds_ready > handled; ++i) {
		event_source = array_get(event_sources, i);

		if (event_source->type != EVENT_SOURCE_TYPE_USB) {
			continue;
		}

		pollfd = array_get(&_usb_poll_pollfds, k);

		if ((int)event_source->handle != pollfd->fd) {
			continue;
		}

		++k;

		if (pollfd->revents == 0) {
			continue;
		}

		event_handle_source(event_source, pollfd->revents);

		++handled;
	}

	if (_usb_poll_pollfds_ready == handled) {
		log_debug("Handled all ready %s event sources",
		          event_get_source_type_name(EVENT_SOURCE_TYPE_USB, false));
	} else {
		log_warn("Handled only %d of %d ready %s event source(s)",
		         handled, _usb_poll_pollfds_ready,
		         event_get_source_type_name(EVENT_SOURCE_TYPE_USB, false));
	}
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

	// create stop pipe
	if (pipe_create(&_stop_pipe, 0) < 0) {
		log_error("Could not create stop pipe: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 4;

	if (event_add_source(_stop_pipe.read_end, EVENT_SOURCE_TYPE_GENERIC,
	                     EVENT_READ, NULL, NULL) < 0) {
		goto cleanup;
	}

	phase = 5;

	// create USB poll thread
	_usb_poll_running = false;
	_usb_poll_stuck = false;

	if (usbi_pipe(_usb_poll_suspend_pipe) < 0) {
		log_error("Could not create USB suspend pipe");

		goto cleanup;
	}

	phase = 6;

	if (pipe_create(&_usb_poll_ready_pipe, 0) < 0) {
		log_error("Could not create USB ready pipe: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 7;

	if (array_create(&_usb_poll_pollfds, 32, sizeof(struct usbi_pollfd), true) < 0) {
		log_error("Could not create USB pollfd array: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 8;

	if (semaphore_create(&_usb_poll_resume) < 0) {
		log_error("Could not create USB resume semaphore: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 9;

	if (semaphore_create(&_usb_poll_suspend) < 0) {
		log_error("Could not create USB suspend semaphore: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 10;

cleanup:
	switch (phase) { // no breaks, all cases fall through intentionally
	case 9:
		semaphore_destroy(&_usb_poll_suspend);

	case 8:
		semaphore_destroy(&_usb_poll_resume);

	case 7:
		pipe_destroy(&_usb_poll_ready_pipe);

	case 6:
		usbi_close(_usb_poll_suspend_pipe[0]);
		usbi_close(_usb_poll_suspend_pipe[1]);

	case 5:
		event_remove_source(_stop_pipe.read_end, EVENT_SOURCE_TYPE_GENERIC);

	case 4:
		pipe_destroy(&_stop_pipe);

	case 3:
		free(_socket_error_set);

	case 2:
		free(_socket_write_set);

	case 1:
		free(_socket_read_set);

	default:
		break;
	}

	return phase == 10 ? 0 : -1;
}

void event_exit_platform(void) {
	semaphore_destroy(&_usb_poll_resume);
	semaphore_destroy(&_usb_poll_suspend);

	array_destroy(&_usb_poll_pollfds, NULL);

	pipe_destroy(&_usb_poll_ready_pipe);

	usbi_close(_usb_poll_suspend_pipe[0]);
	usbi_close(_usb_poll_suspend_pipe[1]);

	event_remove_source(_stop_pipe.read_end, EVENT_SOURCE_TYPE_GENERIC);
	pipe_destroy(&_stop_pipe);

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
	uint8_t byte = 1;
	int rc;
	int event_source_count;
	uint32_t received_events;

	if (event_add_source(_usb_poll_ready_pipe.read_end,
	                     EVENT_SOURCE_TYPE_GENERIC, EVENT_READ,
	                     event_forward_usb_events, event_sources) < 0) {
		return -1;
	}

	*running = true;
	_usb_poll_running = true;

	thread_create(&_usb_poll_thread, event_poll_usb_events, event_sources);

	cleanup();
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

		if (event_reserve_socket_set(&_socket_error_set, // FIXME: this over allocates
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
		log_debug("Starting to select on %d + %d + %d %s event source(s)",
		          _socket_read_set->count, _socket_write_set->count, _socket_error_set->count,
		          event_get_source_type_name(EVENT_SOURCE_TYPE_GENERIC, false));

		semaphore_release(&_usb_poll_resume);

		fd_read_set = event_get_socket_set_as_fd_set(_socket_read_set);
		fd_write_set = event_get_socket_set_as_fd_set(_socket_write_set);
		fd_error_set = event_get_socket_set_as_fd_set(_socket_error_set);

		ready = select(0, fd_read_set, fd_write_set, fd_error_set, NULL);

		if (_usb_poll_running) {
			log_debug("Sending suspend signal to USB poll thread");

			if (usbi_write(_usb_poll_suspend_pipe[1], &byte, 1) < 0) {
				log_error("Could not write to USB suspend pipe");

				_usb_poll_stuck = true;

				goto cleanup;
			}

			semaphore_acquire(&_usb_poll_suspend);

			if (usbi_read(_usb_poll_suspend_pipe[0], &byte, 1) < 0) {
				log_error("Could not read from USB suspend pipe");

				_usb_poll_stuck = true;

				goto cleanup;
			}
		}

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
		log_debug("Select returned %d %s event source(s) as ready", ready,
		          event_get_source_type_name(EVENT_SOURCE_TYPE_GENERIC, false));

		handled = 0;

		// cache event source count here to avoid looking at new event
		// sources that got added during the event handling
		event_source_count = event_sources->count;

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
			log_debug("Handled all ready %s event sources",
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

	if (_usb_poll_running && !_usb_poll_stuck) {
		_usb_poll_running = false;

		log_debug("Stopping USB poll thread");

		if (usbi_write(_usb_poll_suspend_pipe[1], &byte, 1) < 0) {
			log_error("Could not write to USB suspend pipe");
		} else {
			semaphore_release(&_usb_poll_resume);
			thread_join(&_usb_poll_thread);
		}
	}

	thread_destroy(&_usb_poll_thread);

	event_remove_source(_usb_poll_ready_pipe.read_end, EVENT_SOURCE_TYPE_GENERIC);

	return result;
}

int event_stop_platform(void) {
	uint8_t byte = 0;

	if (pipe_write(&_stop_pipe, &byte, sizeof(byte)) < 0) {
		log_error("Could not write to stop pipe: %s (%d)",
		          get_errno_name(errno), errno);

		return -1;
	}

	return 0;
}
