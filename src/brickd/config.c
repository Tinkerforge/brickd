/*
 * brickd
 * Copyright (C) 2012-2014 Matthias Bolte <matthias@tinkerforge.com>
 * Copyright (C) 2014 Olaf Lüke <olaf@tinkerforge.com>
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

static int _check_only = 0;
static int _has_error = 0;
static int _using_default_values = 1;
static const char *_default_listen_address = "0.0.0.0";
static char *_listen_address = NULL;
static uint16_t _listen_port = 4223;
static uint16_t _listen_websocket_port = 80;
static int _listen_dual_stack = 0;
static LogLevel _log_levels[MAX_LOG_CATEGORIES]; // config_init calls config_reset to initialize this

static void config_error(const char *format, ...) ATTRIBUTE_FMT_PRINTF(1, 2);

static void config_error(const char *format, ...) {
	va_list arguments;

	_has_error = 1;

	if (!_check_only) {
		return;
	}

	va_start(arguments, format);

	vfprintf(stderr, format, arguments);
	fprintf(stderr, "\n");
	fflush(stderr);

	va_end(arguments);
}

static void config_reset(void) {
	int i;

	if (_listen_address != _default_listen_address) {
		free(_listen_address);
		_listen_address = (char *)_default_listen_address;
	}

	_listen_port = 4223;
	_listen_websocket_port = 80;
	_listen_dual_stack = 0;

	for (i = 0; i < MAX_LOG_CATEGORIES; ++i) {
		_log_levels[i] = LOG_LEVEL_INFO;
	}
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

static const char *config_format_log_level(LogLevel level) {
	switch (level) {
	case LOG_LEVEL_NONE:  return "none";
	case LOG_LEVEL_ERROR: return "error";
	case LOG_LEVEL_WARN:  return "warn";
	case LOG_LEVEL_INFO:  return "info";
	case LOG_LEVEL_DEBUG: return "debug";
	default:              return "<unknown>";
	}
}

static void config_parse_line(char *string) {
	char *p;
	char *option;
	char *value;
	int port;

	string = config_trim_string(string);

	// ignore comment
	if (*string == '#') {
		return;
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

			return;
		}

		if (_listen_address != _default_listen_address) {
			free(_listen_address);
		}

		_listen_address = strdup(value);

		if (_listen_address == NULL) {
			_listen_address = (char *)_default_listen_address;

			config_error("Could not duplicate listen.address value '%s'", value);

			return;
		}
	} else if (strcmp(option, "listen.port") == 0) {
		if (config_parse_int(value, &port) < 0) {
			config_error("Value '%s' for listen.port option is not an integer", value);

			return;
		}

		if (port < 1 || port > UINT16_MAX) {
			config_error("Value %d for listen.port option is out-of-range", port);

			return;
		}

		_listen_port = (uint16_t)port;
	} else if (strcmp(option, "listen.websocket_port") == 0) {
		if (config_parse_int(value, &port) < 0) {
			config_error("Value '%s' for listen.port option is not an integer", value);

			return;
		}

		if (port < 1 || port > UINT16_MAX) {
			config_error("Value %d for listen.port option is out-of-range", port);

			return;
		}

		_listen_websocket_port = (uint16_t)port;
	} else if (strcmp(option, "listen.dual_stack") == 0) {
		config_lower_string(value);

		if (strcmp(value, "on") == 0) {
			_listen_dual_stack = 1;
		} else if (strcmp(value, "off") == 0) {
			_listen_dual_stack = 0;
		} else {
			config_error("Value '%s' for listen.dual_stack option is invalid", value);

			return;
		}
	} else if (strcmp(option, "log_level.event") == 0) {
		if (config_parse_log_level(value, &_log_levels[LOG_CATEGORY_EVENT]) < 0) {
			config_error("Value '%s' for log_level.event option is invalid", value);

			return;
		}
	} else if (strcmp(option, "log_level.usb") == 0) {
		if (config_parse_log_level(value, &_log_levels[LOG_CATEGORY_USB]) < 0) {
			config_error("Value '%s' for log_level.usb option is invalid", value);

			return;
		}
	} else if (strcmp(option, "log_level.network") == 0) {
		if (config_parse_log_level(value, &_log_levels[LOG_CATEGORY_NETWORK]) < 0) {
			config_error("Value '%s' for log_level.network option is invalid", value);

			return;
		}
	} else if (strcmp(option, "log_level.hotplug") == 0) {
		if (config_parse_log_level(value, &_log_levels[LOG_CATEGORY_HOTPLUG]) < 0) {
			config_error("Value '%s' for log_level.hotplug option is invalid", value);

			return;
		}
	} else if (strcmp(option, "log_level.hardware") == 0) {
		if (config_parse_log_level(value, &_log_levels[LOG_CATEGORY_HARDWARE]) < 0) {
			config_error("Value '%s' for log_level.hardware option is invalid", value);

			return;
		}
	} else if (strcmp(option, "log_level.websocket") == 0) {
		if (config_parse_log_level(value, &_log_levels[LOG_CATEGORY_WEBSOCKET]) < 0) {
			config_error("Value '%s' for log_level.websocket option is invalid", value);

			return;
		}
	} else if (strcmp(option, "log_level.other") == 0) {
		if (config_parse_log_level(value, &_log_levels[LOG_CATEGORY_OTHER]) < 0) {
			config_error("Value '%s' for log_level.other option is invalid", value);

			return;
		}
	} else {
		config_error("Unknown option '%s'", option);
	}
}

int config_check(const char *filename) {
	_check_only = 1;

	config_init(filename);

	if (_has_error) {
		fprintf(stderr, "Errors found in config file '%s'\n", filename);

		config_exit();

		return -1;
	}

	if (!_using_default_values) {
		printf("No errors found in config file '%s'\n", filename);
	}

	printf("\n");
	printf("Using the following config values:\n");
	printf("  listen.address        = %s\n", config_get_listen_address());
	printf("  listen.port           = %u\n", config_get_listen_port());
	printf("  listen.websocket_port = %u\n", config_get_listen_websocket_port());
	printf("  listen.dual_stack     = %s\n", config_get_listen_dual_stack() ? "on" : "off");
	printf("  log_level.event       = %s\n", config_format_log_level(config_get_log_level(LOG_CATEGORY_EVENT)));
	printf("  log_level.usb         = %s\n", config_format_log_level(config_get_log_level(LOG_CATEGORY_USB)));
	printf("  log_level.network     = %s\n", config_format_log_level(config_get_log_level(LOG_CATEGORY_NETWORK)));
	printf("  log_level.hotplug     = %s\n", config_format_log_level(config_get_log_level(LOG_CATEGORY_HOTPLUG)));
	printf("  log_level.hardware    = %s\n", config_format_log_level(config_get_log_level(LOG_CATEGORY_HARDWARE)));
	printf("  log_level.other       = %s\n", config_format_log_level(config_get_log_level(LOG_CATEGORY_OTHER)));

	config_exit();

	return 0;
}

void config_init(const char *filename) {
	FILE *file;
	size_t rc;
	char c;
	char line[256] = "";
	int length = 0;
	int skip = 0;

	config_reset();

	file = fopen(filename, "rb");

	if (file == NULL) {
		if (_check_only) {
			printf("Config file '%s' not found, using default values\n", filename);
		}

		return;
	}

	_using_default_values = 0;

	while (!feof(file)) {
		rc = fread(&c, 1, 1, file);

		if (rc == 0 && ferror(file)) {
			config_error("Error while reading config file '%s'", filename);
			config_reset();

			break;
		} else if (feof(file) || c == '\r' || c == '\n') {
			if (length > 0) {
				line[length] = '\0';

				config_parse_line(line);
			}

			length = 0;
			skip = 0;
		} else if (!skip) {
			if (length < (int)sizeof(line) - 1) {
				line[length++] = c;
			} else {
				line[32] = '\0';

				config_error("Line in config file '%s' is too long, starting with '%s...'",
				             filename, line);

				length = 0;
				skip = 1;
			}
		}
	}

	fclose(file);
}

void config_exit(void) {
	if (_listen_address != _default_listen_address) {
		free(_listen_address);
	}
}

int config_has_error(void) {
	return _has_error;
}

const char *config_get_listen_address(void) {
	return _listen_address;
}

uint16_t config_get_listen_port(void) {
	return _listen_port;
}

uint16_t config_get_listen_websocket_port(void) {
	return _listen_websocket_port;
}

int config_get_listen_dual_stack(void) {
	return _listen_dual_stack;
}

LogLevel config_get_log_level(LogCategory category) {
	return _log_levels[category];
}
