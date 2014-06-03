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
#include <sys/stat.h>
#include <fcntl.h>

#include "gadget.h"

#include "event.h"
#include "file.h"
#include "log.h"
#include "network.h"

#define LOG_CATEGORY LOG_CATEGORY_NETWORK

static File _state_file;
static Client *_client = NULL;

typedef enum {
	GADGET_STATE_DISCONNECTED = 0,
	GADGET_STATE_CONNECTED = 1
} GadgetState;

static int gadget_connect(void) {
	File *file;

	if (_client != NULL) {
		log_warn("RED Brick gadget is already connected");

		return 0;
	}

	file = calloc(1, sizeof(File));

	if (file == NULL) {
		log_error("Could not allocate file object: %s (%d)",
		          get_errno_name(ENOMEM), ENOMEM);

		return -1;
	}

	if (file_create(file, "/dev/g_red_brick_data", O_RDWR) < 0) {
		log_error("Could not create file object for '/dev/g_red_brick_data': %s (%d)",
		          get_errno_name(errno), errno);

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

	log_debug("RED Brick gadget connected");

	return 0;
}

static void gadget_disconnect() {
	if (_client == NULL) {
		log_warn("RED Brick gadget is already disconnected");

		return;
	}

	_client->disconnected = 1;
	_client = NULL;

	log_debug("RED Brick gadget disconnected");
}

static void gadget_handle_state_change(void *opaque) {
	uint8_t state;

	(void)opaque;

	if (file_seek(&_state_file, SEEK_SET, 0) == (off_t)-1) {
		log_error("Could not seek '/proc/g_red_brick_state': %s (%d)",
		          get_errno_name(errno), errno);

		return;
	}

	if (file_read(&_state_file, &state, sizeof(state)) != sizeof(state)) {
		log_error("Could not read from '/proc/g_red_brick_state': %s (%d)",
		          get_errno_name(errno), errno);

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
	uint8_t state;

	log_debug("Initializing gadget subsystem");

	if (file_create(&_state_file, "/proc/g_red_brick_state", O_RDONLY) < 0) {
		log_error("Could not create file object for '/proc/g_red_brick_state': %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 1;

	if (event_add_source(_state_file.base.handle, EVENT_SOURCE_TYPE_GENERIC,
	                     EVENT_READ, gadget_handle_state_change, NULL) < 0) {
		goto cleanup;
	}

	phase = 2;

	if (file_read(&_state_file, &state, sizeof(state)) != sizeof(state)) {
		log_error("Could not read from '/proc/g_red_brick_state': %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	if (state == GADGET_STATE_CONNECTED && gadget_connect() < 0) {
		goto cleanup;
	}

	phase = 3;

cleanup:
	switch (phase) { // no breaks, all cases fall through intentionally
	case 2:
		event_remove_source(_state_file.base.handle, EVENT_SOURCE_TYPE_GENERIC, EVENT_READ);

	case 1:
		file_destroy(&_state_file);

	default:
		break;
	}

	return phase == 3 ? 0 : -1;
}

void gadget_exit(void) {
	log_debug("Shutting down gadget subsystem");

	event_remove_source(_state_file.base.handle, EVENT_SOURCE_TYPE_GENERIC, EVENT_READ);

	file_destroy(&_state_file);
}
