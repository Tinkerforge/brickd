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

#include "bricklet.h"

#include "bricklet_stack.h"

// TODO: This will be the return of the yet to be implemented
//       linux board discovery mechanism in the future.
//       We may also get more than one spidev here.
BrickletStackConfig _bricklet_stack_config = {
    .spi_device = "/dev/spidev0.0"
};

BrickletStack *_bricklet_stack = NULL;

int bricklet_init(void) {
	_bricklet_stack = bricklet_stack_init(&_bricklet_stack_config);
	if(_bricklet_stack == NULL) {
        return -1;
	}

    return 0;
}

void bricklet_exit(void) {
   bricklet_stack_exit(_bricklet_stack); 
}