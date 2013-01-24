/*
 * brickd
 * Copyright (C) 2012 Matthias Bolte <matthias@tinkerforge.com>
 *
 * config.c: Config specific functions
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

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"

#include "utils.h"

static int _check = 0;
static int _error = 0;
static char *_listen_address = NULL;
static uint16_t _listen_port = 4223;
static LogLevel _log_levels[5] = { LOG_LEVEL_INFO,
                                   LOG_LEVEL_INFO,
                                   LOG_LEVEL_INFO,
                                   LOG_LEVEL_INFO,
                                   LOG_LEVEL_INFO };

static void config_error(const char *format, ...) ATTRIBUTE_FMT_PRINTF(1, 2);

static void config_error(const char *format, ...) {
	va_list arguments;

	if (!_check) {
		return;
	}

	_error = 1;

	va_start(arguments, format);

	vfprintf(stderr, format, arguments);
	fprintf(stderr, "\n");
	fflush(stderr);

	va_end(arguments);
}

static char *config_trim_string(char *string) {
	int length;

	while (*string == ' ' || *string == '\t') {
		++string;
	}

	length = strlen(string);

	while (length > 0 &&
	       (string[length - 1] == ' ' || string[length - 1] == '\t')) {
		--length;
	}

	string[length] = '\0';

	return string;
}

static void config_lower_string(char *string) {
	while (*string != '\0') {
		*string = (char)tolower(*string);
		++string;
	}
}

static int config_parse_int(char *string, int *value) {
	char *end = NULL;
	long tmp;

	if (*string == '\0') {
		return -1;
	}

	tmp = strtol(string, &end, 10);

	if (end == NULL || *end != '\0') {
		return -1;
	}

	if (sizeof(long) > sizeof(int) && (tmp < INT32_MIN || tmp > INT32_MAX)) {
		return -1;
	}

	*value = tmp;

	return 0;
}

static int config_parse_log_level(char *string, LogLevel *value) {
	LogLevel tmp;

	config_lower_string(string);

	if (strcmp(string, "error") == 0) {
		tmp = LOG_LEVEL_ERROR;
	} else if (strcmp(string, "warn") == 0) {
		tmp = LOG_LEVEL_WARN;
	} else if (strcmp(string, "info") == 0) {
		tmp = LOG_LEVEL_INFO;
	} else if (strcmp(string, "debug") == 0) {
		tmp = LOG_LEVEL_DEBUG;
	} else {
		return -1;
	}

	*value = tmp;

	return 0;
}

static void config_parse(char *string) {
	char *p;
	char *option;
	char *value;
	int port;

	// remove comment
	p = strchr(string, '#');

	if (p != NULL) {
		*p = '\0';
	}

	// split option and value
	p = strchr(string, '=');

	if (p == NULL) {
		return;
	}

	*p = '\0';

	option = config_trim_string(string);
	value = config_trim_string(p + 1);

	config_lower_string(option);

	// check option
	if (strcmp(option, "listen.address") == 0) {
		if (strlen(value) < 1) {
			config_error("Empty value is not allowed for listen.address option");
		}

		free(_listen_address);
		_listen_address = strdup(value);
	} else if (strcmp(option, "listen.port") == 0) {
		if (config_parse_int(value, &port) < 0) {
			config_error("Value '%s' for listen.port option is not an integer", value);

			return;
		}

		if (port < 0 || port > UINT16_MAX) {
			config_error("Value %d for listen.port option is out-of-range", port);

			return;
		}

		_listen_port = (uint16_t)port;
	} else if (strcmp(option, "log_level.event") == 0) {
		if (config_parse_log_level(value, &_log_levels[LOG_CATEGORY_EVENT]) < 0) {
			config_error("Value '%s' for log_level.event option is invalid", value);

			return;
		}
	} else if (strcmp(option, "log_level.usb") == 0) {
		if (config_parse_log_level(value, &_log_levels[LOG_CATEGORY_USB]) < 0) {
			config_error("Value '%s' for log_level.event option is invalid", value);

			return;
		}
	} else if (strcmp(option, "log_level.network") == 0) {
		if (config_parse_log_level(value, &_log_levels[LOG_CATEGORY_NETWORK]) < 0) {
			config_error("Value '%s' for log_level.event option is invalid", value);

			return;
		}
	} else if (strcmp(option, "log_level.hotplug") == 0) {
		if (config_parse_log_level(value, &_log_levels[LOG_CATEGORY_HOTPLUG]) < 0) {
			config_error("Value '%s' for log_level.event option is invalid", value);

			return;
		}
	} else if (strcmp(option, "log_level.other") == 0) {
		if (config_parse_log_level(value, &_log_levels[LOG_CATEGORY_OTHER]) < 0) {
			config_error("Value '%s' for log_level.event option is invalid", value);

			return;
		}
	} else {
		config_error("Ignoring unknown option '%s'", option);
	}
}

int config_check(const char *name) {
	_check = 1;
	_error = 0;

	config_init(name);
	config_exit();

	if (_error) {
		return -1;
	} else {
		printf("No errors in config found\n");

		return 0;
	}
}

void config_init(const char *name) {
	FILE *file;
	char c;
	char line[128] = "";
	int length = 0;
	int skip = 0;

	_listen_address = strdup("0.0.0.0");

	file = fopen(name, "rb");

	if (file == NULL) {
		return;
	}

	while (!feof(file)) {
		fread(&c, 1, 1, file);

		if (feof(file) || c == '\r' || c == '\n') {
			if (length > 0) {
				config_parse(line);
			}

			length = 0;
			skip = 0;
		} else if (!skip && length < (int)sizeof(line) - 1) {
			line[length++] = c;
			line[length] = '\0';
		} else {
			length = 0;
			skip = 1;
		}
	}

	fclose(file);
}

void config_exit(void) {
	free(_listen_address);
}

const char *config_get_listen_address(void) {
	return _listen_address;
}

uint16_t config_get_listen_port(void) {
	return _listen_port;
}

LogLevel config_get_log_level(LogCategory category) {
	return _log_levels[category];
}
