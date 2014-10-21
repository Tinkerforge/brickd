/*
 * brickd
 *
 * Copyright (C) 2014 Olaf Lüke <olaf@tinkerforge.com>
 *
 * red_ethernet_extension.c: Ethernet extension support for RED Brick
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

#include "red_ethernet_extension.h"

#include "red_extension.h"

#include <stdio.h>
#include <errno.h>

//#include <linux/module.h>
#include <daemonlib/log.h>
#include <daemonlib/utils.h>
#include <daemonlib/red_i2c_eeprom.h>
#include <daemonlib/red_gpio.h>

#define LOG_CATEGORY LOG_CATEGORY_RED_BRICK

#define W5X00_PARAM_MAX_SIZE 150
#define W5X00_MODULE_MAX_SIZE (1000*200)
#define W5X00_MODULE_PATH "/lib/modules/3.4.90+/kernel/drivers/net/ethernet/wiznet/w5x00.ko"

#define EXTENSION_POS0_SELECT {GPIO_PORT_G, GPIO_PIN_9}
#define EXTENSION_POS1_SELECT {GPIO_PORT_G, GPIO_PIN_13}

extern int init_module(void *module_image, unsigned long len,
                       const char *param_values);
extern int delete_module(const char *name, int flags);

void red_ethernet_extension_rmmod(void) {
	if(delete_module("w5x00", 0) < 0) {
		// ENOENT = w5x00 was not loaded (which is OK)
		if(errno != ENOENT) {
			log_warn("Could not remove kernel module: %s (%d)",
					get_errno_name(errno), errno);

			// In this error case we run through, maybe we
			// can load the kernel module anyway.
		}
	}
}

int red_ethernet_extension_init(ExtensionEthernetConfig *ethernet_config) {
	FILE *f;
	char buf_module[W5X00_MODULE_MAX_SIZE];
	char buf_param[W5X00_PARAM_MAX_SIZE + 1] = {0};
	int param_pin_reset;
	int param_pin_interrupt;
	int param_select;
	int length;
	GPIOPin pin;

	// Mux SPI CS pins again. They have been overwritten by I2C select!
	pin.port_index = GPIO_PORT_G;

	switch(ethernet_config->extension) {
		case 1:
			param_pin_reset     = 20;
			param_pin_interrupt = 21;
			param_select        = 1;
			pin.pin_index       = GPIO_PIN_13; // CS1
			break;

		default:
			log_warn("Unsupported extension position (%d), assuming position 0", ethernet_config->extension);
			// Fallthrough
		case 0:
			param_pin_reset     = 15;
			param_pin_interrupt = 17;
			param_select        = 0;
			pin.pin_index       = GPIO_PIN_9; // CS0
			break;
	}

	gpio_mux_configure(pin, GPIO_MUX_2);

	snprintf(buf_param,
	         W5X00_PARAM_MAX_SIZE,
	         "param_pin_reset=%d param_pin_interrupt=%d param_select=%d param_mac=%d,%d,%d,%d,%d,%d",
	         param_pin_reset, param_pin_interrupt, param_select,
	         ethernet_config->mac[0], ethernet_config->mac[1], ethernet_config->mac[2],
	         ethernet_config->mac[3], ethernet_config->mac[4], ethernet_config->mac[5]);

	log_debug("Loading w5x00 kernel module for position %d [%s]",
	          ethernet_config->extension,
	          buf_param);

	if((f = fopen(W5X00_MODULE_PATH, "rb")) == NULL) {
		log_error("Could not read w5x00 kernel module: %s (%d)",
		          get_errno_name(errno), errno);
		return -1;
	}

	length = fread(buf_module, sizeof(char), W5X00_MODULE_MAX_SIZE, f);

	// We abort if the read was not successful or the buffer was not big enough
	if(length < 0 || length == W5X00_MODULE_MAX_SIZE) {
		log_error("Could not read %s (%d)", W5X00_MODULE_PATH, length);
		return -1;
	}

	if(init_module(buf_module, length, buf_param) < 0) {
		log_error("Could not initialize w5x00 kernel module (length %d): %s (%d)",
		          length, get_errno_name(errno), errno);
		return -1;
	}

	return 0;
}

void red_ethernet_extension_exit(void) {
	// Nothing to do here, we do not rmmod the module, if brickd
	// is closed. The Ethernet Extension may still be needed!
	// Example: Closing/recompiling/restarting brickd over ssh.
}
