/*
 * brickd
 * Copyright (C) 2012-2014 Matthias Bolte <matthias@tinkerforge.com>
 * Copyright (C) 2014 Olaf Lüke <olaf@tinkerforge.com>
 *
 * client.h: Client specific functions
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

#ifndef BRICKD_CLIENT_H
#define BRICKD_CLIENT_H

#ifdef _WIN32
	#include <ws2tcpip.h>
#else
	#include <netdb.h>
#endif

#include "array.h"
#include "event.h"
#include "io.h"
#include "packet.h"
#include "queue.h"

typedef struct _Client Client;

typedef enum {
	CLIENT_AUTHENTICATION_STATE_DISABLED = 0,
	CLIENT_AUTHENTICATION_STATE_ENABLED,
	CLIENT_AUTHENTICATION_STATE_NONCE_SEND,
	CLIENT_AUTHENTICATION_STATE_DONE
} ClientAuthenticationState;

#define CLIENT_MAX_NAME_LENGTH 128

struct _Client {
	char name[CLIENT_MAX_NAME_LENGTH]; // for display purpose
	IO *io;
	int disconnected;
	Packet request;
	int request_used;
	int request_header_checked;
	Array pending_requests;
	ClientAuthenticationState authentication_state;
	uint32_t authentication_nonce; // server
	Queue write_queue;
};

#define CLIENT_INFO_FORMAT "N: %s, T: %s, H: %d, A: %s"
#define client_expand_info(client) (client)->name, (client)->io->type, \
	(client)->io->handle, client_get_authentication_state_name((client)->authentication_state)

const char *client_get_authentication_state_name(ClientAuthenticationState state);

int client_create(Client *client, const char *name, IO *io,
                  uint32_t authentication_nonce);
void client_destroy(Client *client);

int client_dispatch_response(Client *client, Packet *response, int force,
                             int ignore_authentication);

#endif // BRICKD_CLIENT_H
