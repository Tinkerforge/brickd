/*
 * brickd
 * Copyright (C) 2012-2013 Matthias Bolte <matthias@tinkerforge.com>
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

#include "event.h"
#include "packet.h"
#include "utils.h"

typedef struct {
	EventHandle socket;
	char *peer;
	int disconnected;
	Packet packet;
	int packet_used;
	Array pending_requests;
} Client;

int client_create(Client *client, EventHandle socket,
                  struct sockaddr_in *address, socklen_t length);
void client_destroy(Client *client);

int client_dispatch_packet(Client *client, Packet *packet, int force);

#endif // BRICKD_CLIENT_H
