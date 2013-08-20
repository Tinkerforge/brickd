/*
 * brickd
 * Copyright (C) 2012-2013 Matthias Bolte <matthias@tinkerforge.com>
 *
 * stack.h: Stack specific functions
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

#ifndef BRICKD_STACK_H
#define BRICKD_STACK_H

#include "packet.h"
#include "utils.h"

typedef struct _Stack Stack;

typedef int (*DispatchPacketFunction)(Stack *stack, Packet *packet);

#define MAX_STACK_NAME 128

struct _Stack {
	char name[MAX_STACK_NAME]; // for display purpose
	DispatchPacketFunction dispatch_packet;
	Array uids;
};

int stack_create(Stack *stack, const char *name,
                 DispatchPacketFunction dispatch_packet);
void stack_destroy(Stack *stack);

int stack_add_uid(Stack *stack, uint32_t uid /* always little endian */);
int stack_knows_uid(Stack *stack, uint32_t uid /* always little endian */);

int stack_dispatch_packet(Stack *stack, Packet *packet, int force);

#endif // BRICKD_STACK_H
