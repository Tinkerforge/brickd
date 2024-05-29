/*
 * brickd
 * Copyright (C) 2018 Olaf Lüke <olaf@tinkerforge.com>
 * Copyright (C) 2018-2019 Matthias Bolte <matthias@tinkerforge.com>
 *
 * bricklet_stack_linux_spidev.c: Linux spidev specific parts of SPI Tinkerforge Protocol
 *                                (SPITFP) implementation for direct communication
 *                                between brickd and Bricklet with co-processor
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

#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <stdbool.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#include <unistd.h>

#if defined(BRICKD_LIBGPIOD2_V2)
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#endif

#include <daemonlib/log.h>
#include <daemonlib/utils.h>

#include "bricklet_stack.h"

#include "bricklet.h"

#include "raspberry_pi.h"

#include <gpiod.h>

#define BRICKLET_STACK_SPI_CONFIG_MODE           SPI_MODE_3
#define BRICKLET_STACK_SPI_CONFIG_LSB_FIRST      0
#define BRICKLET_STACK_SPI_CONFIG_BITS_PER_WORD  8
#define BRICKLET_STACK_SPI_CONFIG_MAX_SPEED_HZ   1400000 // 400000 - 2000000

#if !defined(BRICKD_LIBGPIOD2_V1) && !defined(BRICKD_LIBGPIOD2_V2)
#error "Unknown libgpiod API version!"
#endif

struct _BrickletStackPlatform {
	int spi_fd;
#if defined(BRICKD_LIBGPIOD2_V1)
	struct gpiod_chip *chip;
	struct gpiod_line *line;
#elif defined(BRICKD_LIBGPIOD2_V2)
	struct gpiod_line_request *request;
	unsigned int offset;
#endif
};

static LogSource _log_source = LOG_SOURCE_INITIALIZER;

static BrickletStackPlatform _platform[BRICKLET_SPI_MAX_NUM * BRICKLET_CS_MAX_NUM];

#if defined(BRICKD_LIBGPIOD2_V2)

// This is a modified version of libgpiod-2.1.1's find_line_by_name example.

static int chip_dir_filter(const struct dirent *entry)
{
	struct stat sb;

    // entries[i]->d_name is 256 bytes.
    char path[300];
    memset(path, 0, sizeof(path));

	if (robust_snprintf(path, sizeof(path), "/dev/%s", entry->d_name) < 0)
		return 0;

	if ((lstat(path, &sb) == 0) && (!S_ISLNK(sb.st_mode)) &&
	    gpiod_is_gpiochip_device(path))
		return 1;

	return 0;
}

static int all_chip_paths(char ***paths_ptr)
{
	int i, j, num_chips, ret = 0;
	struct dirent **entries;
	char **paths;

	num_chips = scandir("/dev/", &entries, chip_dir_filter, NULL);
	if (num_chips < 0)
		return 0;

	paths = calloc(num_chips, sizeof(*paths));
	if (paths == NULL)
		return -1;

	for (i = 0; i < num_chips; i++) {
        // entries[i]->d_name is 256 bytes.
		paths[i] = calloc(300, sizeof(char));

		if (paths[i] == NULL || robust_snprintf(paths[i], 300, "/dev/%s", entries[i]->d_name) < 0) {
			for (j = 0; j <= i; j++)
				free(paths[j]);

			free(paths);
			return -1;
		}
	}

	*paths_ptr = paths;
	ret = num_chips;

	for (i = 0; i < num_chips; i++)
		free(entries[i]);

	free(entries);
	return ret;
}

static int find_line(const char *line_name, unsigned int *out_offset, struct gpiod_chip **out_chip) {
	int i, num_chips, offset;
	struct gpiod_chip *chip;
	char **chip_paths;

	/*
	* Names are not guaranteed unique, so this finds the first line with
	* the given name.
	*/
	num_chips = all_chip_paths(&chip_paths);
	if (num_chips == -1)
		return -1;

    log_warn("No GPIO chips found!");


	for (i = 0; i < num_chips; i++) {
		chip = gpiod_chip_open(chip_paths[i]);
		if (!chip) {
            log_warn("Failed to open chip %s: %s (%d)", chip_paths[i], get_errno_name(errno), errno);
			continue;
        }

		offset = gpiod_chip_get_line_offset_from_name(chip, line_name);
		if (offset == -1) {
            if (errno != ENOENT)
                log_warn("Failed to get line offset from chip %s: %s (%d)", chip_paths[i], get_errno_name(errno), errno);
			goto close_chip;
        }

		*out_chip = chip;
		*out_offset = offset;
		return 1;

close_chip:
		gpiod_chip_close(chip);
	}

	// line not found
	*out_chip = NULL;
	*out_offset = 0;
	return 0;
}

// This is a modified version of libgpiod-2.1.1's toggle_line_value example.

static struct gpiod_line_request *
request_output_line(struct gpiod_chip *chip, unsigned int offset, enum gpiod_line_value value, const char *consumer)
{
	struct gpiod_request_config *req_cfg = NULL;
	struct gpiod_line_request *request = NULL;
	struct gpiod_line_settings *settings;
	struct gpiod_line_config *line_cfg;

	settings = gpiod_line_settings_new();
	if (!settings)
		goto close_chip;

	if (gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_OUTPUT) == -1)
		goto free_settings;

	if (gpiod_line_settings_set_output_value(settings, value) == -1)
		goto free_settings;

	line_cfg = gpiod_line_config_new();
	if (!line_cfg)
		goto free_settings;

	if (gpiod_line_config_add_line_settings(line_cfg, &offset, 1, settings) == -1)
		goto free_line_config;

	if (consumer) {
		req_cfg = gpiod_request_config_new();
		if (!req_cfg)
			goto free_line_config;

		// Can't fail
		gpiod_request_config_set_consumer(req_cfg, consumer);
	}

	// Request is null if this fails.
	request = gpiod_chip_request_lines(chip, req_cfg, line_cfg);

	gpiod_request_config_free(req_cfg);

free_line_config:
	gpiod_line_config_free(line_cfg);

free_settings:
	gpiod_line_settings_free(settings);

close_chip:
	gpiod_chip_close(chip);

	return request;
}

#endif

int bricklet_stack_create_platform_spidev(BrickletStack *bricklet_stack) {
	// Use HW chip select if it is done by SPI hardware unit, otherwise set SPI_NO_CS flag.
	// Raspberry Pi 5 does not support setting this flag, even though we disable all HW chip select pins.
	const int no_cs_flag = ((raspberry_pi_detect(NULL, 0) == RASPBERRY_PI_5_DETECTED) || bricklet_stack->config.chip_select_driver == BRICKLET_CHIP_SELECT_DRIVER_HARDWARE) ? 0 : SPI_NO_CS;
	const int mode = BRICKLET_STACK_SPI_CONFIG_MODE | no_cs_flag;
	const int lsb_first = BRICKLET_STACK_SPI_CONFIG_LSB_FIRST;
	const int bits_per_word = BRICKLET_STACK_SPI_CONFIG_BITS_PER_WORD;
	const int max_speed_hz = BRICKLET_STACK_SPI_CONFIG_MAX_SPEED_HZ;
	BrickletStackPlatform *platform = &_platform[bricklet_stack->config.index];

	memset(platform, 0, sizeof(BrickletStackPlatform));
	platform->spi_fd = -1;

	bricklet_stack->platform = platform;

	// configure GPIO chip select
	if (bricklet_stack->config.chip_select_driver == BRICKLET_CHIP_SELECT_DRIVER_GPIO) {
#if defined(BRICKD_LIBGPIOD2_V1)
		char chip_name[32];
		memset(chip_name, 0, sizeof(chip_name));
		unsigned int offset;

		// Find chip and line of the requested GPIO
		int result = gpiod_ctxless_find_line(bricklet_stack->config.chip_select_name, chip_name, sizeof(chip_name), &offset);
		if (result == -1) {
			log_error("Failed to find line %s: %s (%d)", bricklet_stack->config.chip_select_name, get_errno_name(errno), errno);
			goto cleanup;
		} else if (result == 0) {
			log_error("Could not find line %s", bricklet_stack->config.chip_select_name);
			goto cleanup;
		}

		// Open chip
		platform->chip = gpiod_chip_open_by_name(chip_name);

		if (platform->chip == NULL) {
			log_error("Could not open chip %s: %s (%d)", chip_name, get_errno_name(errno), errno);
			goto cleanup;
		}

		// Open line and request output
		platform->line = gpiod_chip_get_line(platform->chip, offset);

		if (platform->line == NULL) {
			log_error("Could not get line %s %d: %s (%d)", chip_name, offset, get_errno_name(errno), errno);
			goto cleanup;
		}

		if (gpiod_line_request_output(platform->line, "Tinkerforge Brick Daemon", 1) == -1) {
			log_error("Could not reserve line for ouput %s %d: %s (%d)", chip_name, offset, get_errno_name(errno), errno);
			goto cleanup;
		}
#elif defined(BRICKD_LIBGPIOD2_V2)
		struct gpiod_chip *chip;
		int result;

		result = find_line(bricklet_stack->config.chip_select_name, &platform->offset, &chip);

		// Find line
		if (result == -1) {
			log_error("Failed to find line %s: %s (%d)", bricklet_stack->config.chip_select_name, get_errno_name(errno), errno);
			goto cleanup;
		} else if (result == 0) {
			log_error("Could not find line %s", bricklet_stack->config.chip_select_name);
			goto cleanup;
		}

		// Open line and request output.
		// Takes ownership of chip; will close chip even if the request fails.
		platform->request = request_output_line(chip, platform->offset, GPIOD_LINE_VALUE_ACTIVE, "Tinkerforge Brick Daemon");
		if (platform->request == NULL) {
			log_error("Could not request output line %s: %s (%d)", bricklet_stack->config.chip_select_name, get_errno_name(errno), errno);
			goto cleanup;
		}
#endif
	}

	// Open spidev
	bricklet_stack->platform->spi_fd = open(bricklet_stack->config.spidev, O_RDWR);

	if (bricklet_stack->platform->spi_fd < 0) {
		log_error("Could not open %s: %s (%d)",
		          bricklet_stack->config.spidev, get_errno_name(errno), errno);

		goto cleanup;
	}

	if (ioctl(bricklet_stack->platform->spi_fd, SPI_IOC_WR_MODE, &mode) < 0) {
		log_error("Could not configure SPI mode: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	if (ioctl(bricklet_stack->platform->spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &max_speed_hz) < 0) {
		log_error("Could not configure SPI max speed: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	if (ioctl(bricklet_stack->platform->spi_fd, SPI_IOC_WR_BITS_PER_WORD, &bits_per_word) < 0) {
		log_error("Could not configure SPI bits per word: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	if (ioctl(bricklet_stack->platform->spi_fd, SPI_IOC_WR_LSB_FIRST, &lsb_first) < 0) {
		log_error("Could not configure SPI LSB first: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	return 0;

cleanup:
	robust_close(bricklet_stack->platform->spi_fd);

#if defined(BRICKD_LIBGPIOD2_V1)
	if (platform->line != NULL) {
		gpiod_line_release(platform->line);
	}

	if (platform->chip != NULL) {
		gpiod_chip_close(platform->chip);
	}
#elif defined(BRICKD_LIBGPIOD2_V2)
	if (platform->request != NULL) {
		gpiod_line_request_release(platform->request);
	}
#endif

	return -1;
}

void bricklet_stack_destroy_platform_spidev(BrickletStack *bricklet_stack) {
	robust_close(bricklet_stack->platform->spi_fd);

	if (bricklet_stack->config.chip_select_driver == BRICKLET_CHIP_SELECT_DRIVER_GPIO) {
#if defined(BRICKD_LIBGPIOD2_V1)
		gpiod_line_release(bricklet_stack->platform->line);
		gpiod_chip_close(bricklet_stack->platform->chip);
#elif defined(BRICKD_LIBGPIOD2_V2)
		gpiod_line_request_release(bricklet_stack->platform->request);
#endif
	}
}

int bricklet_stack_chip_select_gpio_spidev(BrickletStack *bricklet_stack, bool enable) {
#if defined(BRICKD_LIBGPIOD2_V1)
	return gpiod_line_set_value(bricklet_stack->platform->line, enable ? 0 : 1);
#elif defined(BRICKD_LIBGPIOD2_V2)
	return gpiod_line_request_set_value(bricklet_stack->platform->request, bricklet_stack->platform->offset, enable ? GPIOD_LINE_VALUE_INACTIVE : GPIOD_LINE_VALUE_ACTIVE);
#endif
}

int bricklet_stack_notify_spidev(BrickletStack *bricklet_stack) {
	eventfd_t ev = 1;

	if (eventfd_write(bricklet_stack->notification_event, ev) < 0) {
		log_error("Could not write to Bricklet stack SPI notification event: %s (%d)",
		          get_errno_name(errno), errno);

		return -1;
	}

	return 0;
}

int bricklet_stack_wait_spidev(BrickletStack *bricklet_stack) {
	eventfd_t ev;

	if (eventfd_read(bricklet_stack->notification_event, &ev) < 0) {
		if (errno_would_block()) {
			return -1; // no queue responses left
		}

		log_error("Could not read from SPI notification event: %s (%d)",
		          get_errno_name(errno), errno);

		return -1;
	}

	return 0;
}

int bricklet_stack_spi_transceive_spidev(BrickletStack *bricklet_stack, uint8_t *write_buffer,
                                  uint8_t *read_buffer, int length) {
	struct spi_ioc_transfer spi_transfer = {
		.tx_buf = (unsigned long)write_buffer,
		.rx_buf = (unsigned long)read_buffer,
		.len = length,
	};

	return ioctl(bricklet_stack->platform->spi_fd, SPI_IOC_MESSAGE(1), &spi_transfer);
}
