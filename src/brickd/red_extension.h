/*
 * brickd
 * Copyright (C) 2014 Olaf Lüke <olaf@tinkerforge.com>
 *
 * red_extension.h: Extension initialization for RED Brick
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

#ifndef BRICKD_RED_EXTENSION_H
#define BRICKD_RED_EXTENSION_H

#include <stdint.h>

#define EXTENSION_CONFIG_SIZE_MAX   256

#define EXTENSION_RS485_SLAVES_MAX  32
#define EXTENSION_ETHERNET_MAC_SIZE 6

typedef struct {
	int type;
	int extension;

	uint8_t buf[EXTENSION_CONFIG_SIZE_MAX];
} ExtensionBaseConfig;

typedef struct {
	int type;
	int extension;

	uint32_t baudrate;
	uint8_t parity;
	uint8_t stopbits;
	uint32_t address;
	uint32_t slave_num;
	uint32_t slave_address[EXTENSION_RS485_SLAVES_MAX];
} ExtensionRS485Config;

typedef struct {
	int type;
	int extension;

	uint8_t mac[EXTENSION_ETHERNET_MAC_SIZE];
} ExtensionEthernetConfig;

int red_extension_init(void);
void red_extension_exit(void);

#endif
