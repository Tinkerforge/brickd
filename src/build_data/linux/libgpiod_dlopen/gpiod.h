/*
 * brickd
 * Copyright (C) 2025 Matthias Bolte <matthias@tinkerforge.com>
 *
 * gpiod.h: dlopen wrapper for libgpiod2 API
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

#ifndef BRICKD_GPIOD_H
#define BRICKD_GPIOD_H

#include <stddef.h>
#include <stdbool.h>

extern int libgpiod_abi;

// ABI 2 and 3
struct gpiod_chip;
struct gpiod_line;

typedef void (*gpiod_chip_close_t)(struct gpiod_chip *chip);

extern gpiod_chip_close_t gpiod_chip_close;

// ABI 2
typedef int (*gpiod_ctxless_find_line_t)(const char *name, char *chipname, size_t chipname_size, unsigned int *offset);
typedef struct gpiod_chip *(*gpiod_chip_open_by_name_t)(const char *name);
typedef struct gpiod_line *(*gpiod_chip_get_line_t)(struct gpiod_chip *chip, unsigned int offset);
typedef int (*gpiod_line_request_output_t)(struct gpiod_line *line, const char *consumer, int default_val);
typedef int (*gpiod_line_set_value_t)(struct gpiod_line *line, int value);
typedef void (*gpiod_line_release_t)(struct gpiod_line *line);

extern gpiod_ctxless_find_line_t gpiod_ctxless_find_line;
extern gpiod_chip_open_by_name_t gpiod_chip_open_by_name;
extern gpiod_chip_get_line_t gpiod_chip_get_line;
extern gpiod_line_request_output_t gpiod_line_request_output;
extern gpiod_line_set_value_t gpiod_line_set_value;
extern gpiod_line_release_t gpiod_line_release;

// ABI 3
struct gpiod_chip_info;
struct gpiod_line_settings;
struct gpiod_line_config;
struct gpiod_line_request;
struct gpiod_request_config;

enum gpiod_line_value {
	GPIOD_LINE_VALUE_ERROR = -1,
	GPIOD_LINE_VALUE_INACTIVE = 0,
	GPIOD_LINE_VALUE_ACTIVE = 1,
};

enum gpiod_line_direction {
	GPIOD_LINE_DIRECTION_AS_IS = 1,
	GPIOD_LINE_DIRECTION_INPUT,
	GPIOD_LINE_DIRECTION_OUTPUT,
};

typedef bool (*gpiod_is_gpiochip_device_t)(const char *path);
typedef struct gpiod_chip *(*gpiod_chip_open_t)(const char *path);
typedef struct gpiod_chip_info *(*gpiod_chip_get_info_t)(struct gpiod_chip *chip);
typedef int (*gpiod_chip_get_line_offset_from_name_t)(struct gpiod_chip *chip, const char *name);
typedef struct gpiod_line_request *(*gpiod_chip_request_lines_t)(struct gpiod_chip *chip, struct gpiod_request_config *req_cfg, struct gpiod_line_config *line_cfg);
typedef void (*gpiod_chip_info_free_t)(struct gpiod_chip_info *info);
typedef const char *(*gpiod_chip_info_get_name_t)(struct gpiod_chip_info *info);
typedef struct gpiod_line_settings *(*gpiod_line_settings_new_t)(void);
typedef void (*gpiod_line_settings_free_t)(struct gpiod_line_settings *settings);
typedef int (*gpiod_line_settings_set_direction_t)(struct gpiod_line_settings *settings, enum gpiod_line_direction direction);
typedef int (*gpiod_line_settings_set_output_value_t)(struct gpiod_line_settings *settings, enum gpiod_line_value value);
typedef struct gpiod_line_config *(*gpiod_line_config_new_t)(void);
typedef void (*gpiod_line_config_free_t)(struct gpiod_line_config *config);
typedef int (*gpiod_line_config_add_line_settings_t)(struct gpiod_line_config *config, const unsigned int *offsets, size_t num_offsets, struct gpiod_line_settings *settings);
typedef void (*gpiod_line_request_release_t)(struct gpiod_line_request *request);
typedef int (*gpiod_line_request_set_value_t)(struct gpiod_line_request *request, unsigned int offset, enum gpiod_line_value value);
typedef struct gpiod_request_config *(*gpiod_request_config_new_t)(void);
typedef void (*gpiod_request_config_free_t)(struct gpiod_request_config *config);
typedef void (*gpiod_request_config_set_consumer_t)(struct gpiod_request_config *config, const char *consumer);

extern gpiod_is_gpiochip_device_t gpiod_is_gpiochip_device;
extern gpiod_chip_open_t gpiod_chip_open;
extern gpiod_chip_get_info_t gpiod_chip_get_info;
extern gpiod_chip_get_line_offset_from_name_t gpiod_chip_get_line_offset_from_name;
extern gpiod_chip_request_lines_t gpiod_chip_request_lines;
extern gpiod_chip_info_free_t gpiod_chip_info_free;
extern gpiod_chip_info_get_name_t gpiod_chip_info_get_name;
extern gpiod_line_settings_new_t gpiod_line_settings_new;
extern gpiod_line_settings_free_t gpiod_line_settings_free;
extern gpiod_line_settings_set_direction_t gpiod_line_settings_set_direction;
extern gpiod_line_settings_set_output_value_t gpiod_line_settings_set_output_value;
extern gpiod_line_config_new_t gpiod_line_config_new;
extern gpiod_line_config_free_t gpiod_line_config_free;
extern gpiod_line_config_add_line_settings_t gpiod_line_config_add_line_settings;
extern gpiod_line_request_release_t gpiod_line_request_release;
extern gpiod_line_request_set_value_t gpiod_line_request_set_value;
extern gpiod_request_config_new_t gpiod_request_config_new;
extern gpiod_request_config_free_t gpiod_request_config_free;
extern gpiod_request_config_set_consumer_t gpiod_request_config_set_consumer;

int libgpiod_dlopen(void);
void libgpiod_dlclose(void);

#endif // BRICKD_GPIOD_H
