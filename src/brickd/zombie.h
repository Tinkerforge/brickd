/*
 * brickd
 * Copyright (C) 2014, 2018 Matthias Bolte <matthias@tinkerforge.com>
 *
 * zombie.h: Zombie client specific functions
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

#ifndef BRICKD_ZOMBIE_H
#define BRICKD_ZOMBIE_H

#include <stdbool.h>
#include <stdint.h>

#include <daemonlib/node.h>
#include <daemonlib/packet.h>
#include <daemonlib/timer.h>
#include <daemonlib/utils.h>

#include "client.h"

struct _Zombie {
	uint32_t id;
	bool finished;
	Timer timer;
	Node pending_request_sentinel;
	int pending_request_count;
};

int zombie_create(Zombie *zombie, Client *client);
void zombie_destroy(Zombie *zombie);

void zombie_dispatch_response(Zombie *zombie, PendingRequest *pending_request,
                              Packet *response);

#endif // BRICKD_ZOMBIE_H
