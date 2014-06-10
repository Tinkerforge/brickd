/*
 * brickd
 * Copyright (C) 2014 Matthias Bolte <matthias@tinkerforge.com>
 *
 * redapid.c: RED Brick API Daemon interface
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
#include <sys/un.h>

#include <daemonlib/event.h>
#include <daemonlib/log.h>
#include <daemonlib/queue.h>
#include <daemonlib/socket.h>

#include "redapid.h"

#include "gadget.h"
#include "hardware.h"
#include "network.h"
#include "stack.h"

#define LOG_CATEGORY LOG_CATEGORY_OTHER

#define MAX_QUEUED_WRITES 512

typedef struct {
	Stack base;

	Socket socket;
	int disconnected;
	Packet response;
	int response_used;
	int response_header_checked;
	Queue write_queue;
} REDBrickAPIDaemon;

static REDBrickAPIDaemon _redapid;
static int _redapid_connected = 0;

static int redapid_connect(void);
static void redapid_disconnect(void);

static void redapid_handle_write(void *opaque) {
	Packet *response;
	char packet_signature[PACKET_MAX_SIGNATURE_LENGTH];

	(void)opaque;

	if (_redapid.write_queue.count == 0) {
		return;
	}

	response = queue_peek(&_redapid.write_queue);

	if (socket_send(&_redapid.socket, response, response->header.length) < 0) {
		log_error("Could not send queued response (%s) to RED Brick API Daemon, disconnecting redapid: %s (%d)",
		          packet_get_request_signature(packet_signature, response),
		          get_errno_name(errno), errno);

		redapid_disconnect();
		redapid_connect();

		return;
	}

	queue_pop(&_redapid.write_queue, NULL);

	log_debug("Sent queued response (%s) to RED Brick API Daemon, %d response(s) left in write queue",
	          packet_get_request_signature(packet_signature, response),
	          _redapid.write_queue.count);

	if (_redapid.write_queue.count == 0) {
		// last queued response handled, deregister for write events
		event_remove_source(_redapid.socket.base.handle, EVENT_SOURCE_TYPE_GENERIC, EVENT_WRITE);
	}
}

static void redapid_handle_read(void *opaque) {
	int length;
	const char *message = NULL;
	char packet_signature[PACKET_MAX_SIGNATURE_LENGTH];

	(void)opaque;

	length = socket_receive(&_redapid.socket, (uint8_t *)&_redapid.response + _redapid.response_used,
	                        sizeof(Packet) - _redapid.response_used);

	if (length == 0) {
		log_info("RED Brick API Daemon disconnected by peer");

		redapid_disconnect();

		// FIXME: reconnect?

		return;
	}

	if (length < 0) {
		if (length == IO_CONTINUE) {
			// no actual data received
		} else if (errno_interrupted()) {
			log_debug("Receiving from RED Brick API Daemon was interrupted, retrying");
		} else if (errno_would_block()) {
			log_debug("Receiving from RED Brick API Daemon would block, retrying");
		} else {
			log_error("Could not receive from RED Brick API Daemon, disconnecting from redapid: %s (%d)",
			          get_errno_name(errno), errno);

			redapid_disconnect();
			redapid_connect();
		}

		return;
	}

	_redapid.response_used += length;

	while (!_redapid.disconnected && _redapid.response_used > 0) {
		if (_redapid.response_used < (int)sizeof(PacketHeader)) {
			// wait for complete header
			break;
		}

		if (!_redapid.response_header_checked) {
			if (!packet_header_is_valid_response(&_redapid.response.header, &message)) {
				log_error("Got invalid response (%s) from RED Brick API Daemon, disconnecting from redapid: %s",
				          packet_get_response_signature(packet_signature, &_redapid.response),
				          message);

				redapid_disconnect();
				redapid_connect();

				return;
			}

			_redapid.response_header_checked = 1;
		}

		length = _redapid.response.header.length;

		if (_redapid.response_used < length) {
			// wait for complete packet
			break;
		}

		if (packet_header_get_sequence_number(&_redapid.response.header) == 0) {
			log_debug("Got %scallback (%s) from RED Brick API Daemon",
			          packet_get_callback_type(&_redapid.response),
			          packet_get_callback_signature(packet_signature, &_redapid.response));
		} else {
			log_debug("Got response (%s) from RED Brick API Daemon",
			          packet_get_response_signature(packet_signature, &_redapid.response));
		}

		network_dispatch_response(&_redapid.response);

		memmove(&_redapid.response, (uint8_t *)&_redapid.response + length,
		        _redapid.response_used - length);

		_redapid.response_used -= length;
		_redapid.response_header_checked = 0;
	}
}

static int redapid_push_request_to_write_queue(Packet *request) {
	Packet *queued_request;
	char packet_signature[PACKET_MAX_SIGNATURE_LENGTH];

	log_debug("RED Brick API Daemon is not ready to receive, pushing request to write queue (count: %d +1)",
	          _redapid.write_queue.count);

	if (_redapid.write_queue.count >= MAX_QUEUED_WRITES) {
		log_warn("Write queue for RED Brick API Daemon is full, dropping %d queued request(s)",
		         _redapid.write_queue.count - MAX_QUEUED_WRITES + 1);

		while (_redapid.write_queue.count >= MAX_QUEUED_WRITES) {
			queue_pop(&_redapid.write_queue, NULL);
		}
	}

	queued_request = queue_push(&_redapid.write_queue);

	if (queued_request == NULL) {
		log_error("Could not push request (%s) to write queue for RED Brick API Daemon, discarding request: %s (%d)",
		          packet_get_request_signature(packet_signature, request),
		          get_errno_name(errno), errno);

		return -1;
	}

	memcpy(queued_request, request, request->header.length);

	if (_redapid.write_queue.count == 1) {
		// first queued request, register for write events
		if (event_add_source(_redapid.socket.base.handle, EVENT_SOURCE_TYPE_GENERIC,
		                     EVENT_WRITE, redapid_handle_write, NULL) < 0) {
			// FIXME: how to handle this error?
			return -1;
		}
	}

	return 0;
}

static int redapid_dispatch_request(REDBrickAPIDaemon *redapid, Packet *request) {
	int enqueued = 0;
	char packet_signature[PACKET_MAX_SIGNATURE_LENGTH];

	(void)redapid;

	if (!_redapid_connected) {
		log_debug("Not connected to RED Brick API Daemon, ignoring request");

		return 0;
	}

	if (_redapid.write_queue.count > 0) {
		if (redapid_push_request_to_write_queue(request) < 0) {
			return -1;
		}

		enqueued = 1;
	} else {
		if (socket_send(&_redapid.socket, request, request->header.length) < 0) {
			if (!errno_would_block()) {
				log_error("Could not send request (%s) to RED Brick API Daemon, disconnecting from redapid: %s (%d)",
				          packet_get_request_signature(packet_signature, request),
				          get_errno_name(errno), errno);

				redapid_disconnect();
				redapid_connect();

				return -1;
			}

			if (redapid_push_request_to_write_queue(request) < 0) {
				return -1;
			}

			enqueued = 1;
		}
	}

	log_debug("%s response to RED Brick API Daemon",
	          enqueued ? "Enqueued" : "Sent");

	return 0;
}

static int redapid_connect(void) {
	int phase = 0;

	struct sockaddr_un address;

	_redapid.response_used = 0;
	_redapid.response_header_checked = 0;

	log_debug("Connecting to RED Brick API Daemon");

	// create write queue
	if (queue_create(&_redapid.write_queue, sizeof(Packet)) < 0) {
		log_error("Could not create write queue: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 1;

	// create socket
	if (socket_create(&_redapid.socket) < 0) {
		log_error("Could not create socket: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 2;

	if (socket_open(&_redapid.socket, AF_UNIX, SOCK_STREAM, 0) < 0) {
		log_error("Could not open UNIX domain socket: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	// connect socket
	address.sun_family = AF_UNIX;
	strcpy(address.sun_path, "/var/run/redapid.uds");

	if (socket_connect(&_redapid.socket, (struct sockaddr *)&address, sizeof(address)) < 0) {
		log_error("Could not connect UNIX domain socket to '/var/run/redapid.uds': %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	// add socket as event source
	if (event_add_source(_redapid.socket.base.handle, EVENT_SOURCE_TYPE_GENERIC,
	                     EVENT_READ, redapid_handle_read, NULL) < 0) {
		goto cleanup;
	}

	_redapid_connected = 1;

	log_info("Connected to RED Brick API Daemon");

	phase = 3;

cleanup:
	switch (phase) { // no breaks, all cases fall through intentionally
	case 2:
		socket_destroy(&_redapid.socket);

	case 1:
		queue_destroy(&_redapid.write_queue, NULL);

	default:
		break;
	}

	return phase == 3 ? 0 : -1;
}

static void redapid_disconnect(void) {
	if (_redapid.write_queue.count > 0) {
		log_warn("Disconnecting from RED Brick API Daemon while %d response(s) have not been send",
		         _redapid.write_queue.count);
	}

	event_remove_source(_redapid.socket.base.handle, EVENT_SOURCE_TYPE_GENERIC, -1);
	socket_destroy(&_redapid.socket);

	queue_destroy(&_redapid.write_queue, NULL);

	_redapid_connected = 0;
}

int redapid_init(void) {
	int phase = 0;

	log_debug("Initializing RED Brick API subsystem");

	// create base stack
	if (stack_create(&_redapid.base, "redapid",
	                 (StackDispatchRequestFunction)redapid_dispatch_request) < 0) {
		log_error("Could not create base stack for RED Brick API Daemon: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 1;

	if (redapid_connect() < 0) {
		goto cleanup;
	}

	phase = 2;

	// preseed known-UID array
	if (stack_add_uid(&_redapid.base, /*gadget_get_uid()*/123456789) < 0) {
		goto cleanup;
	}

	// add to stacks array
	if (hardware_add_stack(&_redapid.base) < 0) {
		goto cleanup;
	}

	phase = 3;

cleanup:
	switch (phase) { // no breaks, all cases fall through intentionally
	case 2:
		redapid_disconnect();

	case 1:
		stack_destroy(&_redapid.base);

	default:
		break;
	}

	return phase == 3 ? 0 : -1;
}

void redapid_exit(void) {
	log_debug("Shutting down RED Brick API subsystem");

	hardware_remove_stack(&_redapid.base);

	if (_redapid_connected) {
		redapid_disconnect();
	}

	stack_destroy(&_redapid.base);
}
