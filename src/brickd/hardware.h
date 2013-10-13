/*
 * brickd
 * Copyright (C) 2013 Matthias Bolte <matthias@tinkerforge.com>
 *
 * hardware.h: Hardware specific functions
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

#ifndef BRICKD_HARDWARE_H
#define BRICKD_HARDWARE_H

#include "stack.h"
#include "packet.h"

int hardware_init(void);
void hardware_exit(void);

int hardware_add_stack(Stack *stack);
int hardware_remove_stack(Stack *stack);

void hardware_dispatch_request(Packet *request);

#endif // BRICKD_HARDWARE_H
