/*
 * brickd
 * Copyright (C) 2014 Olaf Lüke <olaf@tinkerforge.com>
 * Copyright (C) 2014-2016 Matthias Bolte <matthias@tinkerforge.com>
 *
 * red_extension.c: Extension initialization for RED Brick
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
#include <stdio.h>
#include <string.h>

#include <daemonlib/conf_file.h>
#include <daemonlib/log.h>
#include <daemonlib/red_i2c_eeprom.h>
#include <daemonlib/red_gpio.h>

#include "red_extension.h"

#include "red_rs485_extension.h"
#include "red_ethernet_extension.h"

static LogSource _log_source = LOG_SOURCE_INITIALIZER;

#define EEPROM_SIZE 8192

#define EXTENSION_NUM_MAX 2
#define EXTENSION_EEPROM_TYPE_LOCATION 0
#define EXTENSION_EEPROM_TYPE_SIZE 4

#define EXTENSION_EEPROM_RS485_ADDRESS_LOCATION                  4
#define EXTENSION_EEPROM_RS485_SLAVE_ADDRESSES_START_LOCATION    100
#define EXTENSION_EEPROM_RS485_BAUDRATE_LOCATION                 400
#define EXTENSION_EEPROM_RS485_PARTIY_LOCATION                   404
#define EXTENSION_EEPROM_RS485_STOPBITS_LOCATION                 405

#define EXTENSION_EEPROM_ETHERNET_MAC_ADDRESS                    (32*4)

#define EXTENSION_CONFIG_COMMENT  "# This file is written by brickd on startup and read-only after that. Changing values in this file does not change the configuration."
#define EXTENSION_CONFIG_PATH     "/tmp/extension_position_%d.conf"

typedef enum  {
	EXTENSION_TYPE_NONE = 0,
	EXTENSION_TYPE_CHIBI = 1,
	EXTENSION_TYPE_RS485 = 2,
	EXTENSION_TYPE_WIFI = 3,
	EXTENSION_TYPE_ETHERNET = 4
} ExtensionType;

#define EXTENSION_POS0_GPIO0  {GPIO_PORT_B, GPIO_PIN_13}
#define EXTENSION_POS0_GPIO1  {GPIO_PORT_B, GPIO_PIN_14}
#define EXTENSION_POS0_GPIO2  {GPIO_PORT_B, GPIO_PIN_19}
#define EXTENSION_POS0_SELECT {GPIO_PORT_G, GPIO_PIN_9}

#define EXTENSION_POS1_GPIO0  {GPIO_PORT_G, GPIO_PIN_2}
#define EXTENSION_POS1_GPIO1  {GPIO_PORT_G, GPIO_PIN_3}
#define EXTENSION_POS1_GPIO2  {GPIO_PORT_G, GPIO_PIN_4}
#define EXTENSION_POS1_SELECT {GPIO_PORT_G, GPIO_PIN_13}

#define EXTENSION_SPI_CLK     {GPIO_PORT_G, GPIO_PIN_10}
#define EXTENSION_SPI_MOSI    {GPIO_PORT_G, GPIO_PIN_11}
#define EXTENSION_SPI_MISO    {GPIO_PORT_G, GPIO_PIN_12}

#define EXTENSION_SER_TXD     {GPIO_PORT_C, GPIO_PIN_16}
#define EXTENSION_SER_RXD     {GPIO_PORT_C, GPIO_PIN_17}
#define EXTENSION_SER_RTS     {GPIO_PORT_C, GPIO_PIN_19}

typedef struct {
	GPIOPin pin[2];
	GPIOMux mux;
	int value;   // If input: 0 = default, 1 = Pullup. If Output: 0 = low, 1 = high. Else ignored.
} ExtensionPinConfig;

typedef struct {
	int num_configs;
	ExtensionPinConfig config[];
} ExtensionPinConfigArray;

static ExtensionPinConfigArray extension_startup = {1, {
	{{EXTENSION_POS0_SELECT, EXTENSION_POS1_SELECT}, GPIO_MUX_OUTPUT, 0}, // Deselect eeprom
}};

static ExtensionPinConfigArray extension_rs485_pin_config = {7, {
	{{EXTENSION_POS0_GPIO0,  EXTENSION_POS1_GPIO0},  GPIO_MUX_OUTPUT, 0}, // RXE low = RX enable
	{{EXTENSION_POS0_GPIO1,  EXTENSION_POS1_GPIO1},  GPIO_MUX_INPUT,  1}, // Unused
	{{EXTENSION_POS0_GPIO2,  EXTENSION_POS1_GPIO2},  GPIO_MUX_INPUT,  1}, // Unused
	{{EXTENSION_POS0_SELECT, EXTENSION_POS1_SELECT}, GPIO_MUX_OUTPUT, 0}, // Default = deselect eeprom
	{{EXTENSION_SER_TXD,     EXTENSION_SER_TXD},     GPIO_MUX_4,      0}, // Mux to UART3_TX
	{{EXTENSION_SER_RXD,     EXTENSION_SER_RXD},     GPIO_MUX_4,      0}, // Mux to UART3_RX
	{{EXTENSION_SER_RTS,     EXTENSION_SER_RTS},     GPIO_MUX_4,      0}, // Mux to UART3_RTS
}};

static ExtensionPinConfigArray extension_ethernet_pin_config = {7, {
	{{EXTENSION_POS0_GPIO0,  EXTENSION_POS1_GPIO0},  GPIO_MUX_OUTPUT, 1}, // nRESET = high
	{{EXTENSION_POS0_GPIO1,  EXTENSION_POS1_GPIO1},  GPIO_MUX_6,      0}, // Mux to EINT3/EINT28
	{{EXTENSION_POS0_GPIO2,  EXTENSION_POS1_GPIO2},  GPIO_MUX_OUTPUT, 0}, // PWDN = low
	{{EXTENSION_POS0_SELECT, EXTENSION_POS1_SELECT}, GPIO_MUX_2,      0}, // Mux to SPI1_CS0
	{{EXTENSION_SPI_CLK,     EXTENSION_SPI_CLK},     GPIO_MUX_2,      0}, // Mux to SPI1_CLK
	{{EXTENSION_SPI_MOSI,    EXTENSION_SPI_MOSI},    GPIO_MUX_2,      0}, // Mux to SPI1_MOSI
	{{EXTENSION_SPI_MISO,    EXTENSION_SPI_MISO},    GPIO_MUX_2,      0}, // Mux to SPI1_MISO
}};

// Discovered extension types (for both extensions)
static ExtensionType _red_extension_type[EXTENSION_NUM_MAX] = {EXTENSION_TYPE_NONE, EXTENSION_TYPE_NONE};

static void red_extension_configure_pin(ExtensionPinConfig *config, int extension) {
	gpio_mux_configure(config->pin[extension], config->mux);

	if (config->value == 0) {
		gpio_output_clear(config->pin[extension]);
	} else {
		gpio_output_set(config->pin[extension]); // This should enable pull-up in case of input
	}
}

int red_extension_read_eeprom_from_fs(uint8_t *buffer, int extension) {
	FILE *fp;
	char file_name[128];
	int length;

	if (robust_snprintf(file_name, sizeof(file_name),
	                    "/tmp/new_eeprom_extension_%d.conf", extension) < 0) {
		return -1;
	}

	fp = fopen(file_name, "rb");

	if (fp == NULL) {
		return -1;
	}

	length = robust_fread(fp, buffer, EEPROM_SIZE);

	if (fclose(fp) < 0) {
		log_warn("Could not close file %s", file_name);
	}

	if (remove(file_name) < 0) {
		log_warn("Could not delete file %s", file_name);
	}

	return length;
}

int red_extension_save_rs485_config_to_fs(ExtensionRS485Config *config) {
	ConfFile conf_file;
	ConfFileLine *line;
	char buffer[1024];
	int ret = 0;
	uint8_t i = 0;

	// Create file
	if (conf_file_create(&conf_file) < 0) {
		log_error("Could not create rs485 conf object: %s (%d)",
		          get_errno_name(errno), errno);

		return -1;
	}

	// Write comment
	line = array_append(&conf_file.lines);

	if (line == NULL) {
		log_error("Could not add comment to RS485 conf file: %s (%d)",
		          get_errno_name(errno), errno);

		ret = -1;

		goto cleanup;
	}

	line->raw = strdup(EXTENSION_CONFIG_COMMENT);
	line->name = NULL;
	line->value = NULL;

	// Write options
	snprintf(buffer, sizeof(buffer), "%d", config->type);

	if (conf_file_set_option_value(&conf_file, "type" , buffer) < 0) {
		log_error("Could not set '%s' option for RS485: %s (%d)",
		          "type", get_errno_name(errno), errno);

		ret = -1;

		goto cleanup;
	}

	snprintf(buffer, sizeof(buffer), "%d", config->address);

	if (conf_file_set_option_value(&conf_file, "address" , buffer) < 0) {
		log_error("Could not set '%s' option for RS485: %s (%d)",
		          "address", get_errno_name(errno), errno);

		ret = -1;

		goto cleanup;
	}

	snprintf(buffer, sizeof(buffer), "%d", config->slave_address[0]);

	for (i = 1; i < config->slave_num; i++) {
		if (config->slave_address[i] == 0) {
			break;
		}

		snprintf(buffer + strlen(buffer), sizeof(buffer)-strlen(buffer), ", %d", config->slave_address[i]);
	}

	if (conf_file_set_option_value(&conf_file, "slave_address" , buffer) < 0) {
		log_error("Could not set '%s' option for RS485: %s (%d)",
		          "slave_address", get_errno_name(errno), errno);

		ret = -1;

		goto cleanup;
	}

	snprintf(buffer, sizeof(buffer), "%d", config->baudrate);

	if (conf_file_set_option_value(&conf_file, "baudrate" , buffer) < 0) {
		log_error("Could not set '%s' option for RS485: %s (%d)",
		          "baudrate", get_errno_name(errno), errno);

		ret = -1;

		goto cleanup;
	}

	snprintf(buffer, sizeof(buffer), "%c", config->parity);

	if (conf_file_set_option_value(&conf_file, "parity" , buffer) < 0) {
		log_error("Could not set '%s' option for RS485: %s (%d)",
		          "parity", get_errno_name(errno), errno);

		ret = -1;
		goto cleanup;
	}

	snprintf(buffer, sizeof(buffer), "%d", config->slave_num);

	if (conf_file_set_option_value(&conf_file, "slave_num" , buffer) < 0) {
		log_error("Could not set '%s' option for RS485: %s (%d)",
		          "slave_num", get_errno_name(errno), errno);

		ret = -1;
		goto cleanup;
	}

	snprintf(buffer, sizeof(buffer), "%d", config->stopbits);

	if (conf_file_set_option_value(&conf_file, "stopbits" , buffer) < 0) {
		log_error("Could not set '%s' option for RS485: %s (%d)",
		          "stopbits", get_errno_name(errno), errno);

		ret = -1;

		goto cleanup;
	}

	// Write config to filesystem
	snprintf(buffer, sizeof(buffer), EXTENSION_CONFIG_PATH, config->extension);

	if (conf_file_write(&conf_file, buffer) < 0) {
		log_error("Could not write config to '%s': %s (%d)",
		          buffer, get_errno_name(errno), errno);

		ret = -1;

		goto cleanup;
	}

cleanup:
	conf_file_destroy(&conf_file);

	return ret;
}

int red_extension_read_rs485_config(I2CEEPROM *i2c_eeprom, ExtensionRS485Config *config) {
	uint8_t buf[4];

	// address
	if (i2c_eeprom_read(i2c_eeprom, EXTENSION_EEPROM_RS485_ADDRESS_LOCATION, buf, 4) < 4) {
		log_error("Could not read RS485 address from EEPROM");

		return -1;
	}

	config->address = (buf[0] << 0) | (buf[1] << 8) | (buf[2] << 16) | (buf[3] << 24);

	// baudrate
	if (i2c_eeprom_read(i2c_eeprom, EXTENSION_EEPROM_RS485_BAUDRATE_LOCATION, buf, 4) < 4) {
		log_error("Could not read RS485 baudrate from EEPROM");

		return -1;
	}

	config->baudrate = (buf[0] << 0) | (buf[1] << 8) | (buf[2] << 16) | (buf[3] << 24);

	if (config->baudrate < 8) {
		log_error("Configured RS485 baudrate is too low");

		return -1;
	}

	// parity
	if (i2c_eeprom_read(i2c_eeprom, EXTENSION_EEPROM_RS485_PARTIY_LOCATION, buf, 1) < 1) {
		log_error("Could not read RS485 parity from EEPROM");

		return -1;
	}

	if (buf[0] == EXTENSION_RS485_PARITY_NONE) {
		config->parity = EXTENSION_RS485_PARITY_NONE;
	} else if (buf[0] == EXTENSION_RS485_PARITY_EVEN) {
		config->parity = EXTENSION_RS485_PARITY_EVEN;
	} else {
		config->parity = EXTENSION_RS485_PARITY_ODD;
	}

	// stopbits
	if (i2c_eeprom_read(i2c_eeprom, EXTENSION_EEPROM_RS485_STOPBITS_LOCATION, buf, 1) < 1) {
		log_error("Could not read RS485 stopbits from EEPROM");

		return -1;
	}

	config->stopbits = buf[0];

	// slave addresses
	if (config->address == 0) {
		config->slave_num = 0;
		uint16_t current_eeprom_location = EXTENSION_EEPROM_RS485_SLAVE_ADDRESSES_START_LOCATION;
		uint32_t current_slave_address;

		config->slave_address[0] = 0;

		while (config->slave_num < EXTENSION_RS485_SLAVES_MAX) {
			if (i2c_eeprom_read(i2c_eeprom, current_eeprom_location, buf, 4) < 4) {
				log_error("Could not read RS485 slave addresses from EEPROM");
				return -1;
			}

			current_slave_address = (buf[0] << 0) | (buf[1] << 8) | (buf[2] << 16) | (buf[3] << 24);

			config->slave_address[config->slave_num] = current_slave_address;

			if (current_slave_address == 0) {
				break;
			}

			config->slave_num++;
			current_eeprom_location += 4;
		}
	}

	return 0;
}

int red_extension_save_ethernet_config_to_fs(ExtensionEthernetConfig *config) {
	ConfFile conf_file;
	ConfFileLine *line;
	char buffer[1024];
	int ret = 0;
	uint8_t i = 0;

	// Create file
	if (conf_file_create(&conf_file) < 0) {
		log_error("Could not create ethernet conf object: %s (%d)",
		          get_errno_name(errno), errno);

		return -1;
	}

	// Write comment
	line = array_append(&conf_file.lines);

	if (line == NULL) {
		log_error("Could not add comment to ethernet conf file: %s (%d)",
		          get_errno_name(errno), errno);

		ret = -1;

		goto cleanup;
	}

	line->raw = strdup(EXTENSION_CONFIG_COMMENT);
	line->name = NULL;
	line->value = NULL;

	// Write options
	snprintf(buffer, sizeof(buffer), "%d", config->type);

	if (conf_file_set_option_value(&conf_file, "type" , buffer) < 0) {
		log_error("Could not set '%s' option for RS485: %s (%d)",
		          "type", get_errno_name(errno), errno);

		ret = -1;
		goto cleanup;
	}

	snprintf(buffer, sizeof(buffer), "%02x", (unsigned char)config->mac[0]);

	for (i = 1; i < EXTENSION_ETHERNET_MAC_SIZE; i++) {
		snprintf(buffer + strlen(buffer), sizeof(buffer)-strlen(buffer), ":%02x", (unsigned char)config->mac[i]);
	}

	if (conf_file_set_option_value(&conf_file, "mac", buffer) < 0) {
		log_error("Could not set '%s' option for Ethernet: %s (%d)",
		          "mac", get_errno_name(errno), errno);

		ret = -1;

		goto cleanup;
	}

	// Write config to filesystem
	snprintf(buffer, sizeof(buffer), EXTENSION_CONFIG_PATH, config->extension);

	if (conf_file_write(&conf_file, buffer) < 0) {
		log_error("Could not write config to '%s': %s (%d)",
		          buffer, get_errno_name(errno), errno);

		ret = -1;

		goto cleanup;
	}

cleanup:
	conf_file_destroy(&conf_file);

	return ret;
}

int red_extension_save_unsupported_config_to_fs(ExtensionBaseConfig *config) {
	ConfFile conf_file;
	ConfFileLine *line;
	char buffer[1024];
	int ret = 0;

	// Create file
	if (conf_file_create(&conf_file) < 0) {
		log_error("Could not create ethernet conf object: %s (%d)",
		          get_errno_name(errno), errno);

		return -1;
	}

	// Write comment
	line = array_append(&conf_file.lines);

	if (line == NULL) {
		log_error("Could not add comment to ethernet conf file: %s (%d)",
		          get_errno_name(errno), errno);

		ret = -1;

		goto cleanup;
	}

	line->raw = strdup(EXTENSION_CONFIG_COMMENT);
	line->name = NULL;
	line->value = NULL;

	// Write options
	snprintf(buffer, sizeof(buffer), "%d", config->type);

	if (conf_file_set_option_value(&conf_file, "type" , buffer) < 0) {
		log_error("Could not set '%s' option for RS485: %s (%d)",
		          "type", get_errno_name(errno), errno);

		ret = -1;
		goto cleanup;
	}

	// Write config to filesystem
	snprintf(buffer, sizeof(buffer), EXTENSION_CONFIG_PATH, config->extension);

	if (conf_file_write(&conf_file, buffer) < 0) {
		log_error("Could not write config to '%s': %s (%d)",
		          buffer, get_errno_name(errno), errno);

		ret = -1;

		goto cleanup;
	}

cleanup:
	conf_file_destroy(&conf_file);

	return ret;
}

int red_extension_read_ethernet_config(I2CEEPROM *i2c_eeprom, ExtensionEthernetConfig *config) {
	if (i2c_eeprom_read(i2c_eeprom,
	                    EXTENSION_EEPROM_ETHERNET_MAC_ADDRESS,
	                    config->mac,
	                    EXTENSION_ETHERNET_MAC_SIZE) < EXTENSION_ETHERNET_MAC_SIZE) {
		log_warn("Can't read MAC address, using default address");

		config->mac[0] = 0x40;
		config->mac[1] = 0xD8;
		config->mac[2] = 0x55;
		config->mac[3] = 0x02;
		config->mac[4] = 0xA1;
		config->mac[5] = 0x00;

		return -1;
	}

	return 0;
}

int red_extension_init(void) {
	uint8_t buf[4];
	int i, j, ret;
	ExtensionBaseConfig base_config[2];

	log_debug("Initializing RED Brick Extension subsystem");

	// First we remove the Ethernet Extension kernel module (if there is one)
	// to make sure that there isn't a collision between SPI select and I2C select.
	red_ethernet_extension_rmmod();

	// Then we deselect all EEPROMS
	for (i = 0; i < EXTENSION_NUM_MAX; i++) {
		for (j = 0; j < extension_startup.num_configs; j++) {
			red_extension_configure_pin(&extension_startup.config[j], i);
		}
	}

	// Now we can try to find the configurations
	for (i = 0; i < EXTENSION_NUM_MAX; i++) {
		I2CEEPROM i2c_eeprom;
		log_debug("Checking for presence of Extension at position %d", i);
		int eeprom_length = 0;
		uint8_t eeprom_buffer[EEPROM_SIZE];

		base_config[i].extension = i;
		base_config[i].type = EXTENSION_TYPE_NONE;

		if (i2c_eeprom_create(&i2c_eeprom, i) < 0) {
			return -1;
		}

		if ((eeprom_length = red_extension_read_eeprom_from_fs(eeprom_buffer, i)) > 2) {
			int start_addr = eeprom_buffer[0] | (eeprom_buffer[1] << 8);

			if (eeprom_length + start_addr >= EEPROM_SIZE) {
				log_warn("Found malformed EEPROM config (start=%d, length=%d) for extension %d",
				         start_addr, eeprom_length, i);
			} else {
				log_info("Found new EEPROM config (start=%d, length=%d) for extension %d",
				         start_addr, eeprom_length, i);

				if (i2c_eeprom_write(&i2c_eeprom, start_addr, eeprom_buffer+2, eeprom_length-2) < 0) {
					log_warn("Writing EEPROM config for extension %d failed", i);
				} else {
					log_debug("Wrote EEPROM config (start=%d, length=%d) for extension %d",
					          start_addr, eeprom_length, i);
				}
			}
		}

		if (i2c_eeprom_read(&i2c_eeprom,
		                    EXTENSION_EEPROM_TYPE_LOCATION,
		                    buf,
		                    EXTENSION_EEPROM_TYPE_SIZE) < EXTENSION_EEPROM_TYPE_SIZE) {
			log_info("Could not find Extension at position %d", i);

			continue;
		}

		base_config[i].type = (buf[0] << 0) | (buf[1] << 8) | (buf[2] << 16) | (buf[3] << 24);

		// If there is an extension that is either not configured (Extension type NONE)
		// Or that we currently don't support (WIFI), we will log it, but try to
		// continue finding extensions. We can support an extension at position 1 if
		// there is an unsupported extension at position 0.
		if (base_config[i].type == EXTENSION_TYPE_NONE) {
			log_warn("Could not find Extension at position %d (Type None)", i);
			continue;
		}

		if ((base_config[i].type != EXTENSION_TYPE_ETHERNET) && (base_config[i].type != EXTENSION_TYPE_RS485)) {
			log_warn("Extension at position %d not supported (type %d)", i, base_config[i].type);
			if (red_extension_save_unsupported_config_to_fs(&base_config[i]) < 0) {
				log_warn("Could not save config for unsupported Extension at position %d.", i);
			}
			continue;
		}

		switch (base_config[i].type) {
		case EXTENSION_TYPE_RS485:
			ret = red_extension_read_rs485_config(&i2c_eeprom, (ExtensionRS485Config *)&base_config[i]);
			i2c_eeprom_destroy(&i2c_eeprom);

			if (ret < 0) {
				log_warn("Could not read RS485 config, ignoring extension at position %d", i);

				continue;
			}

			if (red_extension_save_rs485_config_to_fs((ExtensionRS485Config *)&base_config[i]) < 0) {
				log_warn("Could not save RS485 config. RS485 Extension at position %d will not show up in Brick Viewer", i);
			}

			break;

		case EXTENSION_TYPE_ETHERNET:
			ret = red_extension_read_ethernet_config(&i2c_eeprom, (ExtensionEthernetConfig *)&base_config[i]);
			i2c_eeprom_destroy(&i2c_eeprom);

			if (ret < 0) {
				log_warn("Could not read Ethernet config, ignoring extension at position %d", i);

				continue;
			}

			if (red_extension_save_ethernet_config_to_fs((ExtensionEthernetConfig *)&base_config[i]) < 0) {
				log_warn("Could not save Ethernet config. Ethernet Extension at position %d will not show up in Brick Viewer", i);
			}

			break;
		}
	}

	// Configure the pins and initialize extensions
	for (i = 0; i < EXTENSION_NUM_MAX; i++) {
		switch (base_config[i].type) {
		case EXTENSION_TYPE_RS485:
			log_info("Found RS485 Extension at position %d", i);

			for (j = 0; j < extension_rs485_pin_config.num_configs; j++) {
				red_extension_configure_pin(&extension_rs485_pin_config.config[j], i);
			}

			if (red_rs485_extension_init((ExtensionRS485Config *) &base_config[i]) < 0) {
				continue;
			}

			_red_extension_type[i] = EXTENSION_TYPE_RS485;
			break;

		case EXTENSION_TYPE_ETHERNET:
			log_info("Found Ethernet Extension at position %d", i);

			for (j = 0; j < extension_ethernet_pin_config.num_configs; j++) {
				red_extension_configure_pin(&extension_ethernet_pin_config.config[j], i);
			}

			if (red_ethernet_extension_init((ExtensionEthernetConfig *) &base_config[i]) < 0) {
				continue;
			}

			_red_extension_type[i] = EXTENSION_TYPE_ETHERNET;
			break;
		}
	}

	return 0;
}

void red_extension_exit(void) {
	int i;

	log_debug("Shutting down RED Brick Extension subsystem");

	for (i = 0; i < EXTENSION_NUM_MAX; i++) {
		switch (_red_extension_type[i]) {
		case EXTENSION_TYPE_RS485:
			red_rs485_extension_exit();

			break;

		case EXTENSION_TYPE_ETHERNET:
			red_ethernet_extension_exit();

			break;

		default:
			break; // Nothing to do here
		}

		_red_extension_type[i] = EXTENSION_TYPE_NONE;
	}
}
