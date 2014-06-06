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

#include <daemonlib/utils.h>

#include "config.h"

static int _check_only = 0;
static int _has_error = 0;
static int _has_warning = 0;
static int _using_default_values = 1;
static ConfigOption _invalid = CONFIG_OPTION_STRING_INITIALIZER("<invalid>", NULL, 0, -1, "<invalid>");

extern ConfigOption config_options[];

#define config_error(...) config_message(&_has_error, __VA_ARGS__)
#define config_warn(...) config_message(&_has_warning, __VA_ARGS__)

static void config_message(int *has_message, const char *format, ...) ATTRIBUTE_FMT_PRINTF(2, 3);

static void config_message(int *has_message, const char *format, ...) {
	va_list arguments;

	*has_message = 1;

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
	int i = 0;

	_using_default_values = 1;

	for (i = 0; config_options[i].name != NULL; ++i) {
		if (config_options[i].type == CONFIG_OPTION_TYPE_STRING) {
			if (config_options[i].value.string != config_options[i].default_value.string) {
				free((char *)config_options[i].value.string);
			}
		}

		memcpy(&config_options[i].value, &config_options[i].default_value,
		       sizeof(config_options[i].value));
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
	char *name;
	char *value;
	int i;
	int length;
	int integer;

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

	name = config_trim_string(string);
	value = config_trim_string(p + 1);

	config_lower_string(name);

	// check option
	for (i = 0; config_options[i].name != NULL; ++i) {
		if (strcmp(name, config_options[i].name) == 0 ||
		    (config_options[i].legacy_name != NULL &&
		     strcmp(name, config_options[i].legacy_name) == 0)) {
			switch (config_options[i].type) {
			case CONFIG_OPTION_TYPE_STRING:
				if (config_options[i].value.string != config_options[i].default_value.string) {
					free((char *)config_options[i].value.string);
					config_options[i].value.string = NULL;
				}

				length = strlen(value);

				if (length < config_options[i].string_min_length) {
					config_error("Value '%s' for %s option is too short (minimum: %d chars)",
					             value, name, config_options[i].string_min_length);

					return;
				} else if (config_options[i].string_max_length >= 0 &&
				           length > config_options[i].string_max_length) {
					config_error("Value '%s' for %s option is too long (maximum: %d chars)",
					             value, name, config_options[i].string_max_length);

					return;
				} else if (length > 0) {
					config_options[i].value.string = strdup(value);

					if (config_options[i].value.string == NULL) {
						config_error("Could not duplicate %s value '%s'", name, value);

						return;
					}
				}

				break;

			case CONFIG_OPTION_TYPE_INTEGER:
				if (config_parse_int(value, &integer) < 0) {
					config_warn("Value '%s' for %s option is not an integer", value, name);

					return;
				}

				if (integer < config_options[i].integer_min || integer > config_options[i].integer_max) {
					config_warn("Value %d for %s option is out-of-range (min: %d, max: %d)",
					            integer, name, config_options[i].integer_min, config_options[i].integer_max);

					return;
				}

				config_options[i].value.integer = integer;

				break;

			case CONFIG_OPTION_TYPE_BOOLEAN:
				config_lower_string(value);

				if (strcmp(value, "on") == 0) {
					config_options[i].value.boolean = 1;
				} else if (strcmp(value, "off") == 0) {
					config_options[i].value.boolean = 0;
				} else {
					config_warn("Value '%s' for %s option is invalid", value, name);

					return;
				}

				break;

			case CONFIG_OPTION_TYPE_LOG_LEVEL:
				if (config_parse_log_level(value, &config_options[i].value.log_level) < 0) {
					config_warn("Value '%s' for %s option is invalid", value, name);

					return;
				}

				break;
			}

		}
	}
}

int config_check(const char *filename) {
	int i;

	_check_only = 1;

	config_init(filename);

	if (_has_error) {
		fprintf(stderr, "Error(s) in config file '%s'\n", filename);

		config_exit();

		return -1;
	}

	if (_has_warning) {
		fprintf(stderr, "Warning(s) in config file '%s'\n", filename);

		config_exit();

		return -1;
	}

	if (!_using_default_values) {
		printf("No warnings or errors in config file '%s'\n", filename);
	}

	printf("\n");
	printf("Using the following config values:\n");

	for (i = 0; config_options[i].name != NULL; ++i) {
		printf("  %s = ", config_options[i].name);

		switch(config_options[i].type) {
		case CONFIG_OPTION_TYPE_STRING:
			if (config_options[i].value.string != NULL) {
				printf("%s", config_options[i].value.string);
			}

			break;

		case CONFIG_OPTION_TYPE_INTEGER:
			printf("%d", config_options[i].value.integer);

			break;

		case CONFIG_OPTION_TYPE_BOOLEAN:
			printf("%s", config_options[i].value.boolean ? "on" : "off");

			break;

		case CONFIG_OPTION_TYPE_LOG_LEVEL:
			printf("%s", config_format_log_level(config_options[i].value.log_level));

			break;

		default:
			printf("<unknown-type>");

			break;
		}

		printf("\n");
	}

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
	int i = 0;

	for (i = 0; config_options[i].name != NULL; ++i) {
		if (config_options[i].type == CONFIG_OPTION_TYPE_STRING) {
			if (config_options[i].value.string != config_options[i].default_value.string) {
				free((char *)config_options[i].value.string);
			}
		}
	}
}

int config_has_error(void) {
	return _has_error;
}

int config_has_warning(void) {
	return _has_warning;
}

ConfigOption *config_get_option(const char *name) {
	int i = 0;

	for (i = 0; config_options[i].name != NULL; ++i) {
		if (strcmp(config_options[i].name, name) == 0) {
			return &config_options[i];
		}
	}

	return &_invalid;
}
