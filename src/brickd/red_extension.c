/*
 * brickd
 * Copyright (C) 2014 Olaf Lüke <olaf@tinkerforge.com>
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


#include "red_extension.h"

#include "red_rs485_extension.h"
#include "red_ethernet_extension.h"

#include <daemonlib/red_i2c_eeprom.h>
#include <daemonlib/log.h>

#define LOG_CATEGORY LOG_CATEGORY_RED_BRICK

#define EXTENSION_NUM_MAX 2
#define EXTENSION_EEPROM_TYPE_LOCATION 0
#define EXTENSION_EEPROM_TYPE_SIZE 4

typedef enum  {
	EXTENSION_TYPE_NONE = 0,
	EXTENSION_TYPE_CHIBI = 1,
	EXTENSION_TYPE_RS485 = 2,
	EXTENSION_TYPE_WIFI = 3,
	EXTENSION_TYPE_ETHERNET = 4
} ExtensionType;


// Discovered extension types (for both extensions)
ExtensionType _red_extension_type[EXTENSION_NUM_MAX] = {EXTENSION_TYPE_NONE, EXTENSION_TYPE_NONE};


int red_extension_init(void) {
    uint8_t buf[4];
    int i;
    uint32_t type;

    for(i = 0; i < /*EXTENSION_NUM_MAX*/ 1; i++) {
    	I2CEEPROM i2c_eeprom;
    	log_debug("Checking for presence of Extension at position %d", i);

    	if(i2c_eeprom_init(&i2c_eeprom, i) < 0) {
    		return -1;
    	}

		if(i2c_eeprom_read(&i2c_eeprom,
		                   EXTENSION_EEPROM_TYPE_LOCATION,
		                   buf,
		                   EXTENSION_EEPROM_TYPE_SIZE) < EXTENSION_EEPROM_TYPE_SIZE) {
			log_info("Could not find Extension at position %d", i);
			return 0;
		}

		i2c_eeprom_release(&i2c_eeprom);

		type = (buf[0] << 0) | (buf[1] << 8) | (buf[2] << 16) | (buf[3] << 24);

		// If there is an extension that is either not configured (Extension type NONE)
		// Or that we currently don't support (WIFI), we will log it, but try to
		// continue finding extensions. We can support an extension at position 1 if
		// there is an unsupported extension at position 0.
		if(type == EXTENSION_TYPE_NONE) {
			log_warn("Could not find Extension at position %d (Type None)", i);
			continue;
		}

		if((type != EXTENSION_TYPE_ETHERNET) && (type != EXTENSION_TYPE_RS485)) {
			log_warn("Extension at position %d not supported (type %d)", i, type);
			continue;
		}

		switch(type) {
			case EXTENSION_TYPE_RS485:
				log_info("Found RS485 Extension at position %d", i);
				if(red_rs485_extension_init(i) < 0) {
					return -1;
				}

				_red_extension_type[i] = EXTENSION_TYPE_RS485;
				break;

			case EXTENSION_TYPE_ETHERNET:
				log_info("Found Ethernet Extension at position %d", i);
				if(red_ethernet_extension_init(i) < 0) {
					return -1;
				}

				_red_extension_type[i] = EXTENSION_TYPE_ETHERNET;
				break;
		}
    }

    return 0;
}

void red_extension_exit(void) {
    int i;

    for(i = 0; i < EXTENSION_NUM_MAX; i++) {
		switch(_red_extension_type[i]) {
			case EXTENSION_TYPE_RS485:
				red_rs485_extension_exit();
				_red_extension_type[i] = EXTENSION_TYPE_NONE;
				break;

			case EXTENSION_TYPE_ETHERNET:
				red_ethernet_extension_exit();
				_red_extension_type[i] = EXTENSION_TYPE_NONE;
				break;

			default:
				break; // Nothing to do here
		}
    }
}
