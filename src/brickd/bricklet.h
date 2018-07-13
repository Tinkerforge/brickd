/*
 * brickd
 * Copyright (C) 2018 Olaf Lüke <olaf@tinkerforge.com>
 *
 * bricklet.c: Bricklet support
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

#ifndef BRICKD_BRICKLET_H
#define BRICKD_BRICKLET_H

typedef enum {
	BRICKLET_CHIP_SELECT_DRIVER_HARDWARE =  0,
	BRICKLET_CHIP_SELECT_DRIVER_GPIO,
	BRICKLET_CHIP_SELECT_DRIVER_WIRINGPI,
} BrickletChipSelectDriver;

int bricklet_init(void);
void bricklet_exit(void);

#endif // BRICKD_BRICKLET_H
