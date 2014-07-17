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

#include "hardware.h"
#include "network.h"
#include "red_usb_gadget.h"
#include "stack.h"

#define LOG_CATEGORY LOG_CATEGORY_RED_BRICK

#define UDS_FILENAME "/var/run/redapid.uds"

typedef struct {
	Stack base;

	Socket socket;
	Packet response;
	int response_used;
	int response_header_checked;
	Writer request_writer;
} REDBrickAPIDaemon;

static REDBrickAPIDaemon _redapid;
static int _redapid_connected = 0;

static int redapid_connect(void);
static void redapid_disconnect(void);

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
			log_error("Could not receive from RED Brick API Daemon, disconnecting redapid: %s (%d)",
			          get_errno_name(errno), errno);

			redapid_disconnect();
			redapid_connect();
		}

		return;
	}

	_redapid.response_used += length;

	while (_redapid_connected && _redapid.response_used > 0) {
		if (_redapid.response_used < (int)sizeof(PacketHeader)) {
			// wait for complete header
			break;
		}

		if (!_redapid.response_header_checked) {
			if (!packet_header_is_valid_response(&_redapid.response.header, &message)) {
				log_error("Got invalid response (%s) from RED Brick API Daemon, disconnecting redapid: %s",
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

		stack_add_uid(&_redapid.base, _redapid.response.header.uid);

		network_dispatch_response(&_redapid.response);

		memmove(&_redapid.response, (uint8_t *)&_redapid.response + length,
		        _redapid.response_used - length);

		_redapid.response_used -= length;
		_redapid.response_header_checked = 0;
	}
}

static int redapid_dispatch_request(REDBrickAPIDaemon *redapid, Packet *request) {
	char base58[BASE58_MAX_LENGTH];
	uint32_t uid; // always little endian
	EnumerateCallback enumerate_callback;
	int enqueued = 0;

	(void)redapid;

	if (!_redapid_connected) {
		log_debug("Not connected to RED Brick API Daemon, ignoring request");

		return 0;
	}

	if (request->header.function_id == FUNCTION_ENUMERATE) {
		uid = red_usb_gadget_get_uid();

		log_debug("Got enumerate request, sending enumerate-avialable callback for RED Brick [%s]",
		          base58_encode(base58, uint32_from_le(uid)));

		// respond with enumerate-connected callback
		memset(&enumerate_callback, 0, sizeof(enumerate_callback));

		enumerate_callback.header.uid = uid;
		enumerate_callback.header.length = sizeof(enumerate_callback);
		enumerate_callback.header.function_id = CALLBACK_ENUMERATE;
		packet_header_set_sequence_number(&enumerate_callback.header, 0);
		packet_header_set_response_expected(&enumerate_callback.header, 1);

		base58_encode(enumerate_callback.uid, uint32_from_le(uid));
		enumerate_callback.connected_uid[0] = '0';
		enumerate_callback.position = '0';
		enumerate_callback.hardware_version[0] = 1;
		enumerate_callback.hardware_version[1] = 0;
		enumerate_callback.hardware_version[2] = 0;
		enumerate_callback.firmware_version[0] = 2;
		enumerate_callback.firmware_version[1] = 0;
		enumerate_callback.firmware_version[2] = 0;
		enumerate_callback.device_identifier = uint16_to_le(RED_BRICK_DEVICE_IDENTIFIER);
		enumerate_callback.enumeration_type = ENUMERATION_TYPE_AVAILABLE;

		network_dispatch_response((Packet *)&enumerate_callback);
	} else {
		// forward to redapid
		enqueued = writer_write(&_redapid.request_writer, request);

		if (enqueued < 0) {
			return -1;
		}

		log_debug("%s request to RED Brick API Daemon",
		          enqueued ? "Enqueued" : "Sent");
	}

	return 0;
}

static char *redapid_get_recipient_signature(char *signature, int upper, void *opaque) {
	(void)upper;
	(void)opaque;

	snprintf(signature, WRITER_MAX_RECIPIENT_SIGNATURE_LENGTH,
	         "RED Brick API Daemon");

	return signature;
}

static void redapid_recipient_disconnect(void *opaque) {
	(void)opaque;

	redapid_disconnect();
	redapid_connect();
}

static int redapid_connect(void) {
	int phase = 0;

	struct sockaddr_un address;

	_redapid.response_used = 0;
	_redapid.response_header_checked = 0;

	log_debug("Connecting to RED Brick API Daemon");

	// create socket
	if (socket_create(&_redapid.socket) < 0) {
		log_error("Could not create socket: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 1;

	if (socket_open(&_redapid.socket, AF_UNIX, SOCK_STREAM, 0) < 0) {
		log_error("Could not open UNIX domain socket: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	// connect socket
	address.sun_family = AF_UNIX;
	strcpy(address.sun_path, UDS_FILENAME);

	if (socket_connect(&_redapid.socket, (struct sockaddr *)&address, sizeof(address)) < 0) {
		log_warn("Could not connect UNIX domain socket to '%s': %s (%d)",
		         UDS_FILENAME, get_errno_name(errno), errno);

		goto cleanup;
	}

	// add socket as event source
	if (event_add_source(_redapid.socket.base.handle, EVENT_SOURCE_TYPE_GENERIC,
	                     EVENT_READ, redapid_handle_read, NULL) < 0) {
		goto cleanup;
	}

	phase = 2;

	// create request writer
	if (writer_create(&_redapid.request_writer, &_redapid.socket.base,
	                  "request", packet_get_request_signature,
	                  "redapid", redapid_get_recipient_signature,
	                  redapid_recipient_disconnect, NULL) < 0) {
		log_error("Could not create request writer: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	_redapid_connected = 1;

	log_info("Connected to RED Brick API Daemon");

	phase = 3;

cleanup:
	switch (phase) { // no breaks, all cases fall through intentionally
	case 2:
		event_remove_source(_redapid.socket.base.handle, EVENT_SOURCE_TYPE_GENERIC);

	case 1:
		socket_destroy(&_redapid.socket);

	default:
		break;
	}

	return phase == 3 ? 0 : -1;
}

static void redapid_disconnect(void) {
	writer_destroy(&_redapid.request_writer);

	event_remove_source(_redapid.socket.base.handle, EVENT_SOURCE_TYPE_GENERIC);
	socket_destroy(&_redapid.socket);

	_redapid_connected = 0;
}

int redapid_init(void) {
	log_debug("Initializing RED Brick API subsystem");

	// create base stack
	if (stack_create(&_redapid.base, "redapid",
	                 (StackDispatchRequestFunction)redapid_dispatch_request) < 0) {
		log_error("Could not create base stack for RED Brick API Daemon: %s (%d)",
		          get_errno_name(errno), errno);

		return -1;
	}

	redapid_connect();

	// add to stacks array
	if (hardware_add_stack(&_redapid.base) < 0) {
		if (_redapid_connected) {
			redapid_disconnect();
		}

		stack_destroy(&_redapid.base);

		return -1;
	}

	return  0;
}

void redapid_exit(void) {
	log_debug("Shutting down RED Brick API subsystem");

	hardware_remove_stack(&_redapid.base);

	if (_redapid_connected) {
		redapid_disconnect();
	}

	stack_destroy(&_redapid.base);
}
