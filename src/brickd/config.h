/*
 * brickd
 * Copyright (C) 2012 Matthias Bolte <matthias@tinkerforge.com>
 *
 * config.h: Config specific functions
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

#ifndef BRICKD_CONFIG_H
#define BRICKD_CONFIG_H

#include <stdint.h>

#include "log.h"

int config_check(const char *filename);

void config_init(const char *filename);
void config_exit(void);

int config_has_error(void);

const char *config_get_listen_address(void);
uint16_t config_get_listen_port(void);
LogLevel config_get_log_level(LogCategory category);

#endif // BRICKD_CONFIG_H
