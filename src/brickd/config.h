/*
 * brickd
 * Copyright (C) 2012, 2014 Matthias Bolte <matthias@tinkerforge.com>
 * Copyright (C) 2014 Olaf Lüke <olaf@tinkerforge.com>
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

#include <daemonlib/log.h>

typedef enum {
	CONFIG_OPTION_TYPE_STRING = 0,
	CONFIG_OPTION_TYPE_INTEGER,
	CONFIG_OPTION_TYPE_BOOLEAN,
	CONFIG_OPTION_TYPE_LOG_LEVEL
} ConfigOptionType;

typedef struct {
	const char *string;
	int integer;
	int boolean;
	LogLevel log_level;
} ConfigOptionValue;

typedef struct {
	const char *name;
	const char *legacy_name;
	ConfigOptionType type;
	int string_min_length;
	int string_max_length;
	int integer_min;
	int integer_max;
	ConfigOptionValue default_value;
	ConfigOptionValue value;
} ConfigOption;

#define CONFIG_OPTION_STRING_LENGTH_RANGE(min, max) \
	min, max

#define CONFIG_OPTION_INTEGER_RANGE(min, max) \
	min, max

#define CONFIG_OPTION_VALUE_STRING_INITIALIZER(value) \
	{ value, 0, 0, LOG_LEVEL_NONE }

#define CONFIG_OPTION_VALUE_INTEGER_INITIALIZER(value) \
	{ NULL, value, 0, LOG_LEVEL_NONE }

#define CONFIG_OPTION_VALUE_BOOLEAN_INITIALIZER(value) \
	{ NULL, 0, value, LOG_LEVEL_NONE }

#define CONFIG_OPTION_VALUE_LOG_LEVEL_INITIALIZER(value) \
	{ NULL, 0, 0, value }

#define CONFIG_OPTION_VALUE_NULL_INITIALIZER \
	{ NULL, 0, 0, LOG_LEVEL_NONE }

#define CONFIG_OPTION_STRING_INITIALIZER(name, legacy_name, min, max, default_value) \
	{ \
		name, \
		legacy_name, \
		CONFIG_OPTION_TYPE_STRING, \
		CONFIG_OPTION_STRING_LENGTH_RANGE(min, max), \
		CONFIG_OPTION_INTEGER_RANGE(0, 0), \
		CONFIG_OPTION_VALUE_STRING_INITIALIZER(default_value), \
		CONFIG_OPTION_VALUE_NULL_INITIALIZER \
	}

#define CONFIG_OPTION_INTEGER_INITIALIZER(name, legacy_name, min, max, default_value) \
	{ \
		name, \
		legacy_name, \
		CONFIG_OPTION_TYPE_INTEGER, \
		CONFIG_OPTION_STRING_LENGTH_RANGE(0, 0), \
		CONFIG_OPTION_INTEGER_RANGE(min, max), \
		CONFIG_OPTION_VALUE_INTEGER_INITIALIZER(default_value), \
		CONFIG_OPTION_VALUE_NULL_INITIALIZER \
	}

#define CONFIG_OPTION_BOOLEAN_INITIALIZER(name, legacy_name, default_value) \
	{ \
		name, \
		legacy_name, \
		CONFIG_OPTION_TYPE_BOOLEAN, \
		CONFIG_OPTION_STRING_LENGTH_RANGE(0, 0), \
		CONFIG_OPTION_INTEGER_RANGE(0, 0), \
		CONFIG_OPTION_VALUE_BOOLEAN_INITIALIZER(default_value), \
		CONFIG_OPTION_VALUE_NULL_INITIALIZER \
	}

#define CONFIG_OPTION_LOG_LEVEL_INITIALIZER(name, legacy_name, default_value) \
	{ \
		name, \
		legacy_name, \
		CONFIG_OPTION_TYPE_LOG_LEVEL, \
		CONFIG_OPTION_STRING_LENGTH_RANGE(0, 0), \
		CONFIG_OPTION_INTEGER_RANGE(0, 0), \
		CONFIG_OPTION_VALUE_LOG_LEVEL_INITIALIZER(default_value), \
		CONFIG_OPTION_VALUE_NULL_INITIALIZER \
	}

#define CONFIG_OPTION_NULL_INITIALIZER \
	{ \
		NULL, \
		NULL, \
		CONFIG_OPTION_TYPE_STRING, \
		CONFIG_OPTION_STRING_LENGTH_RANGE(0, 0), \
		CONFIG_OPTION_INTEGER_RANGE(0, 0), \
		CONFIG_OPTION_VALUE_STRING_INITIALIZER(NULL), \
		CONFIG_OPTION_VALUE_NULL_INITIALIZER \
	}

int config_check(const char *filename);

void config_init(const char *filename);
void config_exit(void);

int config_has_error(void);
int config_has_warning(void);

ConfigOption *config_get_option(const char *name);

#endif // BRICKD_CONFIG_H
