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

#include "libgpiod2.h"

#include <errno.h>
#include <gpiod.h>
#if defined(BRICKD_WITH_LIBGPIOD_DLOPEN) || defined(BRICKD_LIBGPIOD_ABI_3)
	#include <dirent.h>
	#include <stdio.h>
	#include <stdlib.h>
	#include <sys/stat.h>
#endif

#include <daemonlib/log.h>
#include <daemonlib/utils.h>

struct libgpiod2_chip {
	struct gpiod_chip *chip; // ABI 2 and 3
};

struct libgpiod2_line {
	struct gpiod_line *line; // ABI 2
	struct gpiod_chip *chip; // ABI 3
	unsigned int offset; // ABI 3
	struct gpiod_line_request *request; // ABI 3
};

#if defined(BRICKD_WITH_LIBGPIOD_DLOPEN) || defined(BRICKD_LIBGPIOD_ABI_3)
static LogSource _log_source = LOG_SOURCE_INITIALIZER;
#endif

int libgpiod2_ctxless_find_line(const char *name, char *chipname, size_t chipname_size, unsigned int *offset) {
#if defined(BRICKD_LIBGPIOD_ABI_2)
	return gpiod_ctxless_find_line(name, chipname, chipname_size, offset);
#endif

#if defined(BRICKD_WITH_LIBGPIOD_DLOPEN)
	if (libgpiod_abi == 2) {
		return gpiod_ctxless_find_line(name, chipname, chipname_size, offset);
	}
#endif

#if defined(BRICKD_WITH_LIBGPIOD_DLOPEN) || defined(BRICKD_LIBGPIOD_ABI_3)
	DIR *dp = opendir("/dev");

	if (dp == NULL) {
		return -1;
	}

	for (struct dirent *d = readdir(dp); d != NULL; d = readdir(dp)) {
		char path[512];

		if (robust_snprintf(path, sizeof(path), "/dev/%s", d->d_name) < 0) {
			closedir(dp);
			return -1;
		}

		struct stat st;

		if (lstat(path, &st) < 0) {
			continue;
		}

		if (S_ISLNK(st.st_mode)) {
			continue;
		}

		if (!gpiod_is_gpiochip_device(path)) {
			continue;
		}

		struct gpiod_chip *chip = gpiod_chip_open(path);

		if (chip == NULL) {
			log_warn("Failed to open chip %s: %s (%d)", path, get_errno_name(errno), errno);
			continue;
		}

		int offset_or_error = gpiod_chip_get_line_offset_from_name(chip, name);

		if (offset_or_error < 0) {
			if (errno != ENOENT) {
				log_warn("Failed to get line offset from chip %s: %s (%d)", path, get_errno_name(errno), errno);
			}

			gpiod_chip_close(chip);
			continue;
		}

		*offset = (unsigned int)offset_or_error;

		struct gpiod_chip_info *info = gpiod_chip_get_info(chip);

		if (info == NULL) {
			log_warn("Failed to get chip info %s: %s (%d)", path, get_errno_name(errno), errno);
			gpiod_chip_close(chip);
			continue;
		}

		if (robust_snprintf(chipname, chipname_size, "%s", gpiod_chip_info_get_name(info)) < 0) {
			gpiod_chip_info_free(info);
			gpiod_chip_close(chip);
			closedir(dp);
			return -1;
		}

		gpiod_chip_info_free(info);
		gpiod_chip_close(chip);
		closedir(dp);
		return 1;
	}

	closedir(dp);
	return 0;
#endif
}

static struct libgpiod2_chip *wrap_chip(struct gpiod_chip *chip, void (*close_chip)(struct gpiod_chip *chip)) {
	if (chip == NULL) {
		return NULL;
	}

	struct libgpiod2_chip *wrapper = malloc(sizeof(struct libgpiod2_chip));

	if (wrapper == NULL) {
		close_chip(chip);

		errno = ENOMEM;

		return NULL;
	}

	wrapper->chip = chip;

	return wrapper;
}

struct libgpiod2_chip *libgpiod2_chip_open_by_name(const char *name) {
#if defined(BRICKD_LIBGPIOD_ABI_2)
	return wrap_chip(gpiod_chip_open_by_name(name), gpiod_chip_close);
#endif

#if defined(BRICKD_WITH_LIBGPIOD_DLOPEN)
	if (libgpiod_abi == 2) {
		return wrap_chip(gpiod_chip_open_by_name(name), gpiod_chip_close);
	}
#endif

#if defined(BRICKD_WITH_LIBGPIOD_DLOPEN) || defined(BRICKD_LIBGPIOD_ABI_3)
	char path[512];

	if (robust_snprintf(path, sizeof(path), "/dev/%s", name) < 0) {
		return NULL;
	}

	return wrap_chip(gpiod_chip_open(path), gpiod_chip_close);
#endif
}

void libgpiod2_chip_close(struct libgpiod2_chip *chip) {
	if (chip == NULL) {
		return;
	}

	gpiod_chip_close(chip->chip);

	free(chip);
}

#if defined(BRICKD_LIBGPIOD_ABI_2) || defined(BRICKD_WITH_LIBGPIOD_DLOPEN)

static struct libgpiod2_line *wrap_line(struct gpiod_line *line, void (*close_line)(struct gpiod_line *line)) {
	if (line == NULL) {
		return NULL;
	}

	struct libgpiod2_line *wrapper = malloc(sizeof(struct libgpiod2_line));

	if (wrapper == NULL) {
		close_line(line);

		errno = ENOMEM;

		return NULL;
	}

	wrapper->line = line;
	wrapper->chip = NULL;
	wrapper->offset = -1;
	wrapper->request = NULL;

	return wrapper;
}

#endif

struct libgpiod2_line *libgpiod2_chip_get_line(struct libgpiod2_chip *chip, unsigned int offset) {
#if defined(BRICKD_LIBGPIOD_ABI_2)
	return wrap_line(gpiod_chip_get_line(chip->chip, offset), gpiod_line_release);
#endif

#if defined(BRICKD_WITH_LIBGPIOD_DLOPEN)
	if (libgpiod_abi == 2) {
		return wrap_line(gpiod_chip_get_line(chip->chip, offset), gpiod_line_release);
	}
#endif

#if defined(BRICKD_WITH_LIBGPIOD_DLOPEN) || defined(BRICKD_LIBGPIOD_ABI_3)
	struct libgpiod2_line *wrapper = malloc(sizeof(struct libgpiod2_line));

	if (wrapper == NULL) {
		errno = ENOMEM;

		return NULL;
	}

	wrapper->line = NULL;
	wrapper->chip = chip->chip;
	wrapper->offset = offset;
	wrapper->request = NULL;

	return wrapper;
#endif
}

int libgpiod2_line_request_output(struct libgpiod2_line *line, const char *consumer, int default_val) {
#if defined(BRICKD_LIBGPIOD_ABI_2)
	return gpiod_line_request_output(line->line, consumer, default_val);
#endif

#if defined(BRICKD_WITH_LIBGPIOD_DLOPEN)
	if (libgpiod_abi == 2) {
		return gpiod_line_request_output(line->line, consumer, default_val);
	}
#endif

#if defined(BRICKD_WITH_LIBGPIOD_DLOPEN) || defined(BRICKD_LIBGPIOD_ABI_3)
	struct gpiod_line_settings *settings = NULL;
	struct gpiod_line_config *config = NULL;
	struct gpiod_request_config *request_config = NULL;

	settings = gpiod_line_settings_new();

	if (settings == NULL) {
		return -1;
	}

	if (gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_OUTPUT) < 0) {
		goto free_settings;
	}

	if (gpiod_line_settings_set_output_value(settings, default_val) < 0) {
		goto free_settings;
	}

	config = gpiod_line_config_new();

	if (config == NULL) {
		goto free_settings;
	}

	if (gpiod_line_config_add_line_settings(config, &line->offset, 1, settings) < 0) {
		goto free_config;
	}

	request_config = gpiod_request_config_new();

	if (request_config == NULL) {
		goto free_config;
	}

	gpiod_request_config_set_consumer(request_config, consumer);

	line->request = gpiod_chip_request_lines(line->chip, request_config, config);

	gpiod_request_config_free(request_config);

free_config:
	gpiod_line_config_free(config);

free_settings:
	gpiod_line_settings_free(settings);

	return line->request == NULL ? -1 : 0;
#endif
}

int libgpiod2_line_set_value(struct libgpiod2_line *line, int value) {
#if defined(BRICKD_LIBGPIOD_ABI_2)
	return gpiod_line_set_value(line->line, value);
#endif

#if defined(BRICKD_WITH_LIBGPIOD_DLOPEN)
	if (libgpiod_abi == 2) {
		return gpiod_line_set_value(line->line, value);
	}
#endif

#if defined(BRICKD_WITH_LIBGPIOD_DLOPEN) || defined(BRICKD_LIBGPIOD_ABI_3)
	return gpiod_line_request_set_value(line->request, line->offset, value ? GPIOD_LINE_VALUE_ACTIVE : GPIOD_LINE_VALUE_INACTIVE);
#endif
}

void libgpiod2_line_release(struct libgpiod2_line *line) {
	if (line == NULL) {
		return;
	}

#if defined(BRICKD_LIBGPIOD_ABI_2)
	gpiod_line_release(line->line);
	free(line);
#endif

#if defined(BRICKD_WITH_LIBGPIOD_DLOPEN)
	if (libgpiod_abi == 2) {
		gpiod_line_release(line->line);
		free(line);
		return;
	}
#endif

#if defined(BRICKD_WITH_LIBGPIOD_DLOPEN) || defined(BRICKD_LIBGPIOD_ABI_3)
	gpiod_line_request_release(line->request);
	free(line);
#endif
}
