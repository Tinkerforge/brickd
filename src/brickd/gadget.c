/*
 * brickd
 * Copyright (C) 2014 Matthias Bolte <matthias@tinkerforge.com>
 *
 * gadget.c: Handling for the RED Brick USB gadget interface
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
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "gadget.h"

#include "event.h"
#include "file.h"
#include "log.h"
#include "network.h"
#include "utils.h"

#define LOG_CATEGORY LOG_CATEGORY_NETWORK // FIXME: add a RED_BRICK category?

#define RED_BRICK_DEVICE_IDENTIFIER 17
#define RED_BRICK_UID_FILENAME "/proc/red_brick_uid"
#define G_RED_BRICK_STATE_FILENAME "/proc/g_red_brick_state"
#define G_RED_BRICK_DATA_FILENAME "/dev/g_red_brick_data"

typedef enum {
	GADGET_STATE_DISCONNECTED = 0,
	GADGET_STATE_CONNECTED = 1
} GadgetState;

static uint32_t _uid = 0; // always little endian
static File _state_file;
static Client *_client = NULL;

static int gadget_connect(void) {
	File *file;
	EnumerateCallback enumerate_callback;

	if (_client != NULL) {
		log_warn("RED Brick gadget is already connected");

		return 0;
	}

	// connect to /dev/g_red_brick_data
	file = calloc(1, sizeof(File));

	if (file == NULL) {
		log_error("Could not allocate file object: %s (%d)",
		          get_errno_name(ENOMEM), ENOMEM);

		return -1;
	}

	if (file_create(file, G_RED_BRICK_DATA_FILENAME, O_RDWR) < 0) {
		log_error("Could not create file object for '%s': %s (%d)",
		          G_RED_BRICK_DATA_FILENAME, get_errno_name(errno), errno);

		free(file);

		return -1;
	}

	_client = network_create_client("g_red_brick", &file->base);

	if (_client == NULL) {
		free(file);

		return -1;
	}

	_client->disconnect_on_error = 0;
	_client->authentication_state = CLIENT_AUTHENTICATION_STATE_DISABLED;

	log_debug("Connected RED Brick gadget");

	// send enumerate-connected callback
	memset(&enumerate_callback, 0, sizeof(enumerate_callback));

	enumerate_callback.header.uid = _uid;
	enumerate_callback.header.length = sizeof(enumerate_callback);
	enumerate_callback.header.function_id = CALLBACK_ENUMERATE;
	packet_header_set_sequence_number(&enumerate_callback.header, 0);
	packet_header_set_response_expected(&enumerate_callback.header, 1);

	base58_encode(enumerate_callback.uid, uint32_from_le(_uid));
	enumerate_callback.connected_uid[0] = '0';
	enumerate_callback.position = '0';
	enumerate_callback.hardware_version[0] = 1;
	enumerate_callback.hardware_version[1] = 0;
	enumerate_callback.hardware_version[2] = 0;
	enumerate_callback.firmware_version[0] = 2;
	enumerate_callback.firmware_version[1] = 0;
	enumerate_callback.firmware_version[2] = 0;
	enumerate_callback.device_identifier = uint16_to_le(RED_BRICK_DEVICE_IDENTIFIER);
	enumerate_callback.enumeration_type = ENUMERATION_TYPE_CONNECTED;

	log_debug("Sending enumerate-connected callback to '%s'",
	          G_RED_BRICK_DATA_FILENAME);

	client_dispatch_response(_client, (Packet *)&enumerate_callback, 1, 0);

	return 0;
}

static void gadget_disconnect() {
	if (_client == NULL) {
		log_warn("RED Brick gadget is already disconnected");

		return;
	}

	_client->disconnected = 1;
	_client = NULL;

	log_debug("Disconnected RED Brick gadget");
}

static void gadget_handle_state_change(void *opaque) {
	uint8_t state;

	(void)opaque;

	if (file_seek(&_state_file, SEEK_SET, 0) == (off_t)-1) {
		log_error("Could not seek '%s': %s (%d)",
		          G_RED_BRICK_STATE_FILENAME, get_errno_name(errno), errno);

		return;
	}

	if (file_read(&_state_file, &state, sizeof(state)) != sizeof(state)) {
		log_error("Could not read from '%s': %s (%d)",
		          G_RED_BRICK_STATE_FILENAME, get_errno_name(errno), errno);

		return;
	}

	switch (state) {
	case GADGET_STATE_CONNECTED:
		gadget_connect();

		break;

	case GADGET_STATE_DISCONNECTED:
		gadget_disconnect();

		break;

	default:
		log_warn("Unknown RED Brick gadget state %u", state);

		break;
	}
}

int gadget_init(void) {
	int phase = 0;
	FILE *fp;
	char base58[BASE58_MAX_LENGTH + 1]; // +1 for the \n
	int rc;
	uint8_t state;

	log_debug("Initializing gadget subsystem");

	// read UID from /proc/red_brick_uid
	fp = fopen(RED_BRICK_UID_FILENAME, "rb");

	if (fp == NULL) {
		log_error("Could not open '%s': %s (%d)",
		          RED_BRICK_UID_FILENAME, get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 1;

	rc = fread(base58, 1, sizeof(base58), fp);

	if (rc < 1) {
		log_error("Could not read enough data from '%s'",
		          RED_BRICK_UID_FILENAME);

		goto cleanup;
	}

	if (base58[rc - 1] != '\n') {
		log_error("'%s' contains invalid data",
		          RED_BRICK_UID_FILENAME);

		goto cleanup;
	}

	base58[rc - 1] = '\0';

	if (base58_decode(&_uid, base58) < 0) {
		log_error("'%s' is not valid Base58: %s (%d)",
		          base58, get_errno_name(errno), errno);

		goto cleanup;
	}

	_uid = uint32_to_le(_uid);

	log_debug("Using %s (%u) as UID for the RED Brick",
	          base58, uint32_from_le(_uid));

	// read current USB gadget state from /proc/g_red_brick_state
	if (file_create(&_state_file, G_RED_BRICK_STATE_FILENAME, O_RDONLY) < 0) {
		log_error("Could not create file object for '%s': %s (%d)",
		          G_RED_BRICK_STATE_FILENAME, get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 2;

	if (event_add_source(_state_file.base.handle, EVENT_SOURCE_TYPE_GENERIC,
	                     EVENT_READ, gadget_handle_state_change, NULL) < 0) {
		goto cleanup;
	}

	phase = 3;

	if (file_read(&_state_file, &state, sizeof(state)) != sizeof(state)) {
		log_error("Could not read from '%s': %s (%d)",
		          G_RED_BRICK_STATE_FILENAME, get_errno_name(errno), errno);

		goto cleanup;
	}

	if (state == GADGET_STATE_CONNECTED && gadget_connect() < 0) {
		goto cleanup;
	}

	phase = 4;

	fclose(fp);

cleanup:
	switch (phase) { // no breaks, all cases fall through intentionally
	case 3:
		event_remove_source(_state_file.base.handle, EVENT_SOURCE_TYPE_GENERIC, EVENT_READ);

	case 2:
		file_destroy(&_state_file);

	case 1:
		fclose(fp);

	default:
		break;
	}

	return phase == 4 ? 0 : -1;
}

void gadget_exit(void) {
	log_debug("Shutting down gadget subsystem");

	event_remove_source(_state_file.base.handle, EVENT_SOURCE_TYPE_GENERIC, EVENT_READ);

	file_destroy(&_state_file);
}
