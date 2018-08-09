/*
 * brickd
 * Copyright (C) 2012-2014, 2016-2018 Matthias Bolte <matthias@tinkerforge.com>
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

#include <stdbool.h>
#ifdef _WIN32
	#include <ws2tcpip.h>
#else
	#include <netdb.h>
#endif

#include <daemonlib/array.h>
#include <daemonlib/io.h>
#include <daemonlib/node.h>
#include <daemonlib/packet.h>
#include <daemonlib/writer.h>

#define CLIENT_MAX_NAME_LENGTH 128
#define CLIENT_MAX_PENDING_REQUESTS 32768

typedef struct _Client Client;
typedef struct _Zombie Zombie;

typedef enum {
	CLIENT_AUTHENTICATION_STATE_DISABLED = 0,
	CLIENT_AUTHENTICATION_STATE_ENABLED,
	CLIENT_AUTHENTICATION_STATE_NONCE_SEND,
	CLIENT_AUTHENTICATION_STATE_DONE
} ClientAuthenticationState;

typedef void (*ClientDestroyDoneFunction)(void);

typedef struct _PendingRequest PendingRequest;

struct _PendingRequest {
	Node global_node;
	Node client_node; // also used as zombie_node
	Client *client;
	Zombie *zombie;
	PacketHeader header;
};

struct _Client {
	char name[CLIENT_MAX_NAME_LENGTH]; // for display purpose
	IO *io;
	bool disconnected;
	union {
		uint8_t buffer[512];
		Packet request;
	};
	int buffer_used;
	bool header_checked;
	Node pending_request_sentinel;
	int pending_request_count;
	Writer response_writer;
	ClientAuthenticationState authentication_state;
	uint32_t authentication_nonce; // server
	ClientDestroyDoneFunction destroy_done;
};

#define CLIENT_SIGNATURE_FORMAT "N: %s, T: %s, H: %d/%d, A: %s"
#define client_expand_signature(client) (client)->name, (client)->io->type, \
	(int)(client)->io->read_handle, (int)(client)->io->write_handle, \
	client_get_authentication_state_name((client)->authentication_state)

void pending_request_remove_and_free(PendingRequest *pending_request);

const char *client_get_authentication_state_name(ClientAuthenticationState state);

int client_create(Client *client, const char *name, IO *io,
                  uint32_t authentication_nonce,
                  ClientDestroyDoneFunction destroy_done);
void client_destroy(Client *client);

void client_dispatch_response(Client *client, PendingRequest *pending_request,
                              Packet *response, bool force, bool ignore_authentication);

#ifdef BRICKD_WITH_RED_BRICK

void client_send_red_brick_enumerate(Client *client, EnumerationType type);

#endif

#endif // BRICKD_CLIENT_H
