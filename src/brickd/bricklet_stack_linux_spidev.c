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

#include <daemonlib/gpio_sysfs.h>
#include <daemonlib/log.h>
#include <daemonlib/utils.h>

#include "bricklet_stack.h"

#include "bricklet.h"

#define BRICKLET_STACK_SPI_CONFIG_MODE           SPI_MODE_3
#define BRICKLET_STACK_SPI_CONFIG_LSB_FIRST      0
#define BRICKLET_STACK_SPI_CONFIG_BITS_PER_WORD  8

// On RPi 3 make sure to set "core_freq=250" in /boot/config.txt.
// The SPI clock is scaled with the variable core_freq otherwise
// and the SPI clock is not stable...
#define BRICKLET_STACK_SPI_CONFIG_MAX_SPEED_HZ   1400000

struct _BrickletStackPlatform {
	int spi_fd;
	int chip_select_gpio_fd;
};

static LogSource _log_source = LOG_SOURCE_INITIALIZER;
static BrickletStackPlatform _platform[BRICKLET_SPI_MAX_NUM * BRICKLET_CS_MAX_NUM];

int bricklet_stack_create_platform_spidev(BrickletStack *bricklet_stack) {
	// Use HW chip select if it is done by SPI hardware unit, otherwise set SPI_NO_CS flag.
	const int mode = BRICKLET_STACK_SPI_CONFIG_MODE | (bricklet_stack->config.chip_select_driver == BRICKLET_CHIP_SELECT_DRIVER_HARDWARE ? 0 : SPI_NO_CS);
	const int lsb_first = BRICKLET_STACK_SPI_CONFIG_LSB_FIRST;
	const int bits_per_word = BRICKLET_STACK_SPI_CONFIG_BITS_PER_WORD;
	const int max_speed_hz = BRICKLET_STACK_SPI_CONFIG_MAX_SPEED_HZ;
	BrickletStackPlatform *platform = &_platform[bricklet_stack->config.index];
	char buffer[256];

	memset(platform, 0, sizeof(BrickletStackPlatform));

	bricklet_stack->platform = platform;

	// configure GPIO chip select
	if (bricklet_stack->config.chip_select_driver == BRICKLET_CHIP_SELECT_DRIVER_GPIO) {
		if (gpio_sysfs_export(&bricklet_stack->config.chip_select_gpio_sysfs) < 0) {
			log_error("Could not export %s: %s (%d)",
			          bricklet_stack->config.chip_select_gpio_sysfs.name, get_errno_name(errno), errno);

			return -1;
		}

		// FIXME: this will configure the pin as output-low, change this to directly configure as output-high
		if (gpio_sysfs_set_direction(&bricklet_stack->config.chip_select_gpio_sysfs, GPIO_SYSFS_DIRECTION_OUTPUT) < 0) {
			log_error("Could not set direction for %s: %s (%d)",
			          bricklet_stack->config.chip_select_gpio_sysfs.name, get_errno_name(errno), errno);

			return -1; // FIXME: unexport gpio cs pin
		}

		if (gpio_sysfs_set_output(&bricklet_stack->config.chip_select_gpio_sysfs, GPIO_SYSFS_VALUE_HIGH) < 0) {
			log_error("Could not set output for %s: %s (%d)",
			          bricklet_stack->config.chip_select_gpio_sysfs.name, get_errno_name(errno), errno);

			return -1; // FIXME: unexport gpio cs pin
		}

		snprintf(buffer, sizeof(buffer), "/sys/class/gpio/%s/value", bricklet_stack->config.chip_select_name);
		bricklet_stack->platform->chip_select_gpio_fd = open(buffer, O_WRONLY);

		if (bricklet_stack->platform->chip_select_gpio_fd < 0) {
			log_error("Could not open %s: %s (%d)",
			          buffer, get_errno_name(errno), errno);

			return -1; // FIXME: unexport gpio cs pin
		}
	}

	// Open spidev
	bricklet_stack->platform->spi_fd = open(bricklet_stack->config.spidev, O_RDWR);

	if (bricklet_stack->platform->spi_fd < 0) {
		log_error("Could not open %s: %s (%d)",
		          bricklet_stack->config.spidev, get_errno_name(errno), errno);

		return -1; // FIXME: close gpio_fd and unexport gpio cs pin
	}

	if (ioctl(bricklet_stack->platform->spi_fd, SPI_IOC_WR_MODE, &mode) < 0) {
		log_error("Could not configure SPI mode: %s (%d)",
		          get_errno_name(errno), errno);

		return -1; // FIXME: close spi_fd, close gpio_fd and unexport gpio cs pin
	}

	if (ioctl(bricklet_stack->platform->spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &max_speed_hz) < 0) {
		log_error("Could not configure SPI max speed: %s (%d)",
		          get_errno_name(errno), errno);

		return -1; // FIXME: close spi_fd, close gpio_fd and unexport gpio cs pin
	}

	if (ioctl(bricklet_stack->platform->spi_fd, SPI_IOC_WR_BITS_PER_WORD, &bits_per_word) < 0) {
		log_error("Could not configure SPI bits per word: %s (%d)",
		          get_errno_name(errno), errno);

		return -1; // FIXME: close spi_fd, close gpio_fd and unexport gpio cs pin
	}

	if (ioctl(bricklet_stack->platform->spi_fd, SPI_IOC_WR_LSB_FIRST, &lsb_first) < 0) {
		log_error("Could not configure SPI LSB first: %s (%d)",
		          get_errno_name(errno), errno);

		return -1; // FIXME: close spi_fd, close gpio_fd and unexport gpio cs pin
	}

	return 0;
}

void bricklet_stack_destroy_platform_spidev(BrickletStack *bricklet_stack) {
	robust_close(bricklet_stack->platform->spi_fd);
	robust_close(bricklet_stack->platform->chip_select_gpio_fd);

	if (bricklet_stack->config.chip_select_driver == BRICKLET_CHIP_SELECT_DRIVER_GPIO) {
		gpio_sysfs_unexport(&bricklet_stack->config.chip_select_gpio_sysfs);
	}
}

int bricklet_stack_chip_select_gpio_spidev(BrickletStack *bricklet_stack, bool enable) {
	// Use direct write call instead of gpio_sysfs_set_output on buffered fd to save some CPU time
	return write(bricklet_stack->platform->chip_select_gpio_fd, enable ? "0" : "1", 1);
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
