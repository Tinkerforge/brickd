/*
 * brickd
 * Copyright (C) 2023 Erik Fleckstein <erik@tinkerforge.com>
 *
 * raspberry_pi.h: Raspberry Pi detection
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

#include <stddef.h>

#define RASPBERRY_PI_NOT_DETECTED 0
#define RASPBERRY_PI_DETECTED 1
#define RASPBERRY_PI_5_DETECTED 2
int raspberry_pi_detect(char *spidev_reason, size_t spidev_reason_len);
