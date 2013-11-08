/*
 * brickd
 * Copyright (C) 2012-2013 Matthias Bolte <matthias@tinkerforge.com>
 *
 * array.h: Array specific functions
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

#ifndef BRICKD_ARRAY_H
#define BRICKD_ARRAY_H

#include <stdint.h>

#include "utils.h"

typedef struct {
	int allocated;
	int count; // number of items in the array
	int size; // size of a single item in bytes
	int relocatable; // true if item can be moved in memory
	uint8_t *bytes;
} Array;

int array_create(Array *array, int reserved, int size, int relocatable);
void array_destroy(Array *array, FreeFunction function);

int array_reserve(Array *array, int count);
int array_resize(Array *array, int count, FreeFunction function);

void *array_append(Array *array);
void array_remove(Array *array, int i, FreeFunction function);

void *array_get(Array *array, int i);

#endif // BRICKD_ARRAY_H
