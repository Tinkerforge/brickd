/*
 * brickd
 * Copyright (C) 2012-2014 Matthias Bolte <matthias@tinkerforge.com>
 * Copyright (C) 2014 Olaf Lüke <olaf@tinkerforge.com>
 *
 * network.h: Network specific functions
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

#ifndef BRICKD_NETWORK_H
#define BRICKD_NETWORK_H

#include <daemonlib/packet.h>

#include "client.h"

int network_init(void);
void network_exit(void);

Client *network_create_client(const char *name, IO *io);
int network_create_zombie(Client *client);

void network_cleanup_clients_and_zombies(void);

void network_client_expects_response(Client *client, Packet *request);
void network_dispatch_response(Packet *response);

#ifdef BRICKD_WITH_RED_BRICK

void network_broadcast_red_brick_enumerate_disconnect(void);

#endif

#endif // BRICKD_NETWORK_H
