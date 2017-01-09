/*
 * brickd
 * Copyright (C) 2016 Ishraq Ibne Ashraf <ishraq@tinkerforge.com>
 *
 * mesh.h: Mesh specific functions
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

#ifndef BRICKD_MESH_H
#define BRICKD_MESH_H

#include <stdbool.h>
#include <daemonlib/socket.h>

int mesh_init(void);
void mesh_exit(void);
void mesh_handle_accept(void *opaque);
int mesh_start_listening(uint16_t mesh_listen_port,
                         SocketCreateAllocatedFunction create_allocated);

#endif // BRICKD_MESH_H
