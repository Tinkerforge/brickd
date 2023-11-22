/*
 * brickd
 * Copyright (C) 2020 Erik Fleckstein <erik@tinkerforge.com>
 *
 * raspberry_pi.c: Raspberry Pi detection
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

#include "raspberry_pi.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include <daemonlib/utils.h>

int raspberry_pi_detect(char *spidev_reason, size_t spidev_reason_len) {
#if defined __arm__ || defined __aarch64__
	static int last_result = -1;
	static char last_spidev_reason[256];
	const char *model_path = "/proc/device-tree/model";
	char buffer[256];
	int length;
	const char *model_prefix = "Raspberry Pi ";
	int model_prefix_len = strlen(model_prefix);
	const char * unsupported_model_suffixes[] = {"5"};
	int fd;
	size_t i;
	int result = RASPBERRY_PI_NOT_DETECTED;

	if (last_result >= 0) {
		if (spidev_reason != NULL) {
			memcpy(spidev_reason, last_spidev_reason, MIN(sizeof(last_spidev_reason) / sizeof(last_spidev_reason)[0], spidev_reason_len));
			spidev_reason[spidev_reason_len - 1] = 0;
		}
		return last_result;
	}

	snprintf(last_spidev_reason, sizeof(last_spidev_reason)/sizeof(last_spidev_reason[0]), "<unknown>");

	fd = open(model_path, O_RDONLY);

	if (fd < 0) {
		if (errno == ENOENT) {
			snprintf(last_spidev_reason, sizeof(last_spidev_reason)/sizeof(last_spidev_reason[0]), "%s not found", model_path);
			goto close_fd;
		}
		snprintf(last_spidev_reason, sizeof(last_spidev_reason)/sizeof(last_spidev_reason[0]), "could not open %s for reading: %s (%d)",
		            model_path, get_errno_name(errno), errno);
		goto close_fd;
	}

	length = robust_read(fd, buffer, sizeof(buffer) - 1);

	if (length < 0) {
		snprintf(last_spidev_reason, sizeof(last_spidev_reason)/sizeof(last_spidev_reason[0]), "could not read from %s: %s (%d)",
		            model_path, get_errno_name(errno), errno);
		goto close_fd;
	}

	buffer[length] = '\0';

	if (strncmp(buffer, model_prefix, model_prefix_len) != 0) {
		snprintf(last_spidev_reason, sizeof(last_spidev_reason)/sizeof(last_spidev_reason[0]), "no 'Raspberry Pi' prefix in %s",
		            model_path);
		goto close_fd;
	}

	result = RASPBERRY_PI_DETECTED;

	if (length > model_prefix_len) {
		for(i = 0; i < sizeof(unsupported_model_suffixes)/sizeof(unsupported_model_suffixes[0]); ++i) {
			if (strncmp(buffer + model_prefix_len, unsupported_model_suffixes[i], strlen(unsupported_model_suffixes[i])) == 0) {
				snprintf(last_spidev_reason, sizeof(last_spidev_reason)/sizeof(last_spidev_reason[0]), "unsupported suffix %s after 'Raspberry Pi' in %s", unsupported_model_suffixes[i], model_path);
				result = RASPBERRY_PI_5_DETECTED;
				goto close_fd;
			}
		}
	}

close_fd:
	close(fd);
	if (spidev_reason != NULL) {
		memcpy(spidev_reason, last_spidev_reason, MIN(sizeof(last_spidev_reason) / sizeof(last_spidev_reason)[0], spidev_reason_len));
		spidev_reason[spidev_reason_len - 1] = 0;
	}
	return result;

#else
	if (spidev_reason != NULL) {
		snprintf(spidev_reason, spidev_reason_len, "non-ARM architecture");
	}

	return 0;
#endif
}
