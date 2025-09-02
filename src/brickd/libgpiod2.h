/*
 * brickd
 * Copyright (C) 2025 Matthias Bolte <matthias@tinkerforge.com>
 *
 * libgpiod2.h: Emulate libgpiod2 based on libgpiod3, if libgpiod2 is not available
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

#ifndef BRICKD_LIBGPIOD2_H
#define BRICKD_LIBGPIOD2_H

#include <stddef.h>

struct libgpiod2_chip;
struct libgpiod2_line;

int libgpiod2_ctxless_find_line(const char *name, char *chipname, size_t chipname_size, unsigned int *offset);
struct libgpiod2_chip *libgpiod2_chip_open_by_name(const char *name);
void libgpiod2_chip_close(struct libgpiod2_chip *chip);
struct libgpiod2_line *libgpiod2_chip_get_line(struct libgpiod2_chip *chip, unsigned int offset);
int libgpiod2_line_request_output(struct libgpiod2_line *line, const char *consumer, int default_val);
int libgpiod2_line_set_value(struct libgpiod2_line *line, int value);
void libgpiod2_line_release(struct libgpiod2_line *line);

#endif // BRICKD_LIBGPIOD2_H
