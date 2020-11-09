/*
 * brickd
 * Copyright (C) 2020 Matthias Bolte <matthias@tinkerforge.com>
 *
 * bricklet_stack_linux.c: Linux specific parts of SPI Tinkerforge Protocol
 *                         (SPITFP) implementation for direct communication
 *                         between brickd and Bricklet with co-processor
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
#include <stdbool.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <daemonlib/log.h>
#include <daemonlib/utils.h>

#include "bricklet_stack.h"

static LogSource _log_source = LOG_SOURCE_INITIALIZER;

extern int bricklet_stack_create_platform_bcm2835(BrickletStack *bricklet_stack);
extern void bricklet_stack_destroy_platform_bcm2835(BrickletStack *bricklet_stack);
extern int bricklet_stack_chip_select_gpio_bcm2835(BrickletStack *bricklet_stack, bool enable);
extern int bricklet_stack_notify_bcm2835(BrickletStack *bricklet_stack);
extern int bricklet_stack_wait_bcm2835(BrickletStack *bricklet_stack);
extern int bricklet_stack_spi_transceive_bcm2835(BrickletStack *bricklet_stack, uint8_t *write_buffer,
                                                 uint8_t *read_buffer, int length);

extern int bricklet_stack_create_platform_spidev(BrickletStack *bricklet_stack);
extern void bricklet_stack_destroy_platform_spidev(BrickletStack *bricklet_stack);
extern int bricklet_stack_chip_select_gpio_spidev(BrickletStack *bricklet_stack, bool enable);
extern int bricklet_stack_notify_spidev(BrickletStack *bricklet_stack);
extern int bricklet_stack_wait_spidev(BrickletStack *bricklet_stack);
extern int bricklet_stack_spi_transceive_spidev(BrickletStack *bricklet_stack, uint8_t *write_buffer,
                                                uint8_t *read_buffer, int length);

typedef int (*create_platform_t)(BrickletStack *bricklet_stack);
typedef void (*destroy_platform_t)(BrickletStack *bricklet_stack);
typedef int (*chip_select_gpio_t)(BrickletStack *bricklet_stack, bool enable);
typedef int (*notify_t)(BrickletStack *bricklet_stack);
typedef int (*wait_t)(BrickletStack *bricklet_stack);
typedef int (*spi_transceive_t)(BrickletStack *bricklet_stack, uint8_t *write_buffer,
                                uint8_t *read_buffer, int length);

static int _raspberry_pi = -1;
static create_platform_t _create_platform = NULL;
static destroy_platform_t _destroy_platform = NULL;
static chip_select_gpio_t _chip_select_gpio = NULL;
static notify_t _notify = NULL;
static wait_t _wait = NULL;
static spi_transceive_t _spi_transceive = NULL;

int bricklet_stack_create_platform(BrickletStack *bricklet_stack) {
	bool raspberry_pi = false;
#if defined __arm__ || defined __aarch64__
	char spidev_reason[256] = "<unknown>";
	const char *model_path = "/proc/device-tree/model";
	char buffer[256];
	int length;
	const char *model_perfix = "Raspberry Pi";
	int fd;

	if (_raspberry_pi < 0) {
		fd = open(model_path, O_RDONLY);

		if (fd < 0) {
			if (errno == ENOENT) {
				snprintf(spidev_reason, sizeof(spidev_reason), "%s not found", model_path);
			} else {
				snprintf(spidev_reason, sizeof(spidev_reason), "could not open %s for reading: %s (%d)",
				         model_path, get_errno_name(errno), errno);
			}
		} else {
			length = robust_read(fd, buffer, sizeof(buffer) - 1);

			if (length < 0) {
				snprintf(spidev_reason, sizeof(spidev_reason), "could not read from %s: %s (%d)",
				         model_path, get_errno_name(errno), errno);
			} else {
				buffer[length] = '\0';

				if (strncmp(buffer, model_perfix, strlen(model_perfix)) != 0) {
					snprintf(spidev_reason, sizeof(spidev_reason), "no 'Raspberry Pi' prefix in %s",
					         model_path);
				} else {
					raspberry_pi = true;
				}
			}

			close(fd);
		}
	}
#else
	const char *spidev_reason =  "non-ARM architecture";
#endif

	if (_raspberry_pi < 0) {
		if (raspberry_pi) {
			log_info("Using BCM2835 backend for Bricklets (Raspberry Pi detected)");

			_raspberry_pi = 1;
			_create_platform = bricklet_stack_create_platform_bcm2835;
			_destroy_platform = bricklet_stack_destroy_platform_bcm2835;
			_chip_select_gpio = bricklet_stack_chip_select_gpio_bcm2835;
			_notify = bricklet_stack_notify_bcm2835;
			_wait = bricklet_stack_wait_bcm2835;
			_spi_transceive = bricklet_stack_spi_transceive_bcm2835;
		} else {
			log_info("Using spidev backend for Bricklets (%s)", spidev_reason);

			_raspberry_pi = 0;
			_create_platform = bricklet_stack_create_platform_spidev;
			_destroy_platform = bricklet_stack_destroy_platform_spidev;
			_chip_select_gpio = bricklet_stack_chip_select_gpio_spidev;
			_notify = bricklet_stack_notify_spidev;
			_wait = bricklet_stack_wait_spidev;
			_spi_transceive = bricklet_stack_spi_transceive_spidev;
		}
	}

	return _create_platform(bricklet_stack);
}

void bricklet_stack_destroy_platform(BrickletStack *bricklet_stack) {
	_destroy_platform(bricklet_stack);
}

int bricklet_stack_chip_select_gpio(BrickletStack *bricklet_stack, bool enable) {
	return _chip_select_gpio(bricklet_stack, enable);
}

int bricklet_stack_notify(BrickletStack *bricklet_stack) {
	return _notify(bricklet_stack);
}

int bricklet_stack_wait(BrickletStack *bricklet_stack) {
	return _wait(bricklet_stack);
}

int bricklet_stack_spi_transceive(BrickletStack *bricklet_stack, uint8_t *write_buffer,
                                  uint8_t *read_buffer, int length) {
	return _spi_transceive(bricklet_stack, write_buffer, read_buffer, length);
}
