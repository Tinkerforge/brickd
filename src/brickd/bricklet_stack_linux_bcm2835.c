/*
 * brickd
 * Copyright (C) 2020 Erik Fleckstein <erik@tinkerforge.com>
 *
 * bricklet_stack_linux_bcm2835.c: Linux BCM2835 specific parts of SPI Tinkerforge Protocol
 *                                 (SPITFP) implementation for direct communication
 *                                 between brickd and Bricklet with co-processor
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
#include <stdbool.h>
#include <unistd.h>
#include <sys/eventfd.h>

#include <daemonlib/log.h>

#include "bricklet_stack.h"

#include "bricklet.h"
#include "bcm2835.h"
#include "vcgencmd.h"

#define BRICKLET_STACK_SPI_CONFIG_MODE             BCM2835_SPI_MODE3
#define BRICKLET_STACK_SPI_CONFIG_BIT_ORDER        BCM2835_SPI_BIT_ORDER_MSBFIRST
#define BRICKLET_STACK_SPI_CONFIG_HARDWARE_CS_PINS BCM2835_SPI_CS_NONE
#define BRICKLET_STACK_SPI_CONFIG_MAX_SPEED_HZ     1400000

struct _BrickletStackPlatform {
	int chip_select_pin;
};

static LogSource _log_source = LOG_SOURCE_INITIALIZER;

static BrickletStackPlatform _platform[BRICKLET_SPI_MAX_NUM * BRICKLET_CS_MAX_NUM];

// Keep track of the count of bricklet_stack_create_platform calls.
// bricklet_stack_destroy_platform then only closes the bcm2835 handle if
// this is the last platform to be destroyed.
static int platform_init_counter = 0;

uint32_t bcm2835_core_clk_hz;

static int bricklet_stack_parse_core_freq(const char *name, int *value) {
	char buffer[128];
	int length;

	length = vcgencmd_get_config(name, buffer, sizeof(buffer) - 1);

	if (length < 0) {
		log_error("Could not read Raspberry Pi %s config", name);

		return -1;
	}

	buffer[length] = '\0';

	if (parse_int(buffer, NULL, 10, value) < 0) {
		log_error("Could not parse Raspberry Pi %s value: %s", name, buffer);

		return -1;
	}

	if (*value == 0) {
		// zero means default value, which is 250 for core_freq and core_freq_min
		// https://github.com/raspberrypi/userland/issues/653
		log_debug("Raspberry Pi %s value is zero, assuming 250 MHz", name);

		*value = 250;
	}

	if (*value < 100 || *value > 1000) {
		log_error("Invalid value for Raspberry Pi %s config: %d", name, *value);

		return -1;
	}

	return 0;
}

int bricklet_stack_create_platform_bcm2835(BrickletStack *bricklet_stack) {
	BrickletStackPlatform *platform = &_platform[bricklet_stack->config.index];
	int core_freq;
	int core_freq_min;

	memset(platform, 0, sizeof(BrickletStackPlatform));

	bricklet_stack->platform = platform;

	if (strcmp(bricklet_stack->config.spidev, "/dev/spidev0.0") != 0) {
		log_error("Only /dev/spidev0.0 is supported");

		return -1;
	}

	if (bricklet_stack->config.chip_select_driver != BRICKLET_CHIP_SELECT_DRIVER_GPIO) {
		log_error("Only chip-select-driver gpio is supported");

		return -1;
	}

	if (platform_init_counter == 0) {
		// core_freq
		if (bricklet_stack_parse_core_freq("core_freq", &core_freq) < 0) {
			return -1;
		}

		// core_freq_min
		if (bricklet_stack_parse_core_freq("core_freq_min", &core_freq_min) < 0) {
			return -1;
		}

		// bcm2835
		if (core_freq != core_freq_min) {
			log_warn("Raspberry Pi core frequency (core_freq: %d, core_freq_min: %d) is unstable, SPI throughput will be unstable too",
			         core_freq, core_freq_min);
		}

		log_info("Using %d MHz Raspberry Pi core frequency (core_freq: %d, core_freq_min: %d) for BCM2835 backend",
		         core_freq, core_freq, core_freq_min);

		bcm2835_core_clk_hz = core_freq * 1000000; // MHz -> Hz

		if (!bcm2835_init()) {
			log_error("Could not init bcm2835");

			return -1;
		}

		if (!bcm2835_spi_begin()) {
			log_error("Could not begin bcm2835 spi");
			bcm2835_close();

			return -1;
		}

		bcm2835_spi_setBitOrder(BRICKLET_STACK_SPI_CONFIG_BIT_ORDER);
		bcm2835_spi_setDataMode(BRICKLET_STACK_SPI_CONFIG_MODE);
		bcm2835_spi_set_speed_hz(BRICKLET_STACK_SPI_CONFIG_MAX_SPEED_HZ);
		bcm2835_spi_chipSelect(BRICKLET_STACK_SPI_CONFIG_HARDWARE_CS_PINS);
	}

	// configure GPIO chip select
	bcm2835_gpio_fsel(bricklet_stack->config.chip_select_num, BCM2835_GPIO_FSEL_OUTP);
	bcm2835_gpio_write(bricklet_stack->config.chip_select_num, HIGH);

	bricklet_stack->platform->chip_select_pin = bricklet_stack->config.chip_select_num;

	++platform_init_counter;

	return 0;
}

void bricklet_stack_destroy_platform_bcm2835(BrickletStack *bricklet_stack) {
	(void)bricklet_stack;

	--platform_init_counter;

	if (platform_init_counter == 0) {
		bcm2835_spi_end();
		bcm2835_close();
	}
}

int bricklet_stack_chip_select_gpio_bcm2835(BrickletStack *bricklet_stack, bool enable) {
	bcm2835_gpio_write(bricklet_stack->platform->chip_select_pin, enable ? LOW : HIGH);

	return 0;
}

int bricklet_stack_notify_bcm2835(BrickletStack *bricklet_stack) {
	eventfd_t ev = 1;

	if (eventfd_write(bricklet_stack->notification_event, ev) < 0) {
		log_error("Could not write to Bricklet stack SPI notification event: %s (%d)",
		          get_errno_name(errno), errno);

		return -1;
	}

	return 0;
}

int bricklet_stack_wait_bcm2835(BrickletStack *bricklet_stack) {
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

int bricklet_stack_spi_transceive_bcm2835(BrickletStack *bricklet_stack, uint8_t *write_buffer,
                                          uint8_t *read_buffer, int length) {
	(void)bricklet_stack;

	bcm2835_spi_transfernb((char *)write_buffer, (char *)read_buffer, length);

	return length;
}
