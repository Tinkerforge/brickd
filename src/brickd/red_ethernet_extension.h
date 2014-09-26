/*
 * brickd
 *
 * Copyright (C) 2014 Olaf Lüke <olaf@tinkerforge.com>
 *
 * red_ethernet_extension.h: Ethernet extension support for RED Brick
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

#ifndef BRICKD_RED_ETHERNET_EXTENSION_H
#define BRICKD_RED_ETHERNET_EXTENSION_H

int red_ethernet_extension_init(int extension);
void red_ethernet_extension_exit(void);

#endif
