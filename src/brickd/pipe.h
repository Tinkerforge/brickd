/*
 * brickd
 * Copyright (C) 2012 Matthias Bolte <matthias@tinkerforge.com>
 *
 * pipe.h: Pipe specific functions
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

#ifndef BRICKD_PIPE_H
#define BRICKD_PIPE_H

#include "event.h"

int pipe_create(EventHandle handles[2]);
void pipe_destroy(EventHandle handles[2]);

int pipe_read(EventHandle handle, void *buffer, int length);
int pipe_write(EventHandle handle, void *buffer, int length);

#endif // BRICKD_PIPE_H
