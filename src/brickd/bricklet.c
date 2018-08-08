/*
 * brickd
 * Copyright (C) 2018 Olaf Lüke <olaf@tinkerforge.com>
 *
 * bricklet.c: Bricklet support
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

#include "bricklet.h"

#include "bricklet_stack.h"

#include <daemonlib/config.h>
#include <daemonlib/log.h>
#include <daemonlib/threads.h>

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>

#define BRICKLET_SPI_MAX_NUM           2
#define BRICKLET_CS_MAX_NUM            10

#define BRICKLET_CONFIG_STR_GROUP_POS  14
#define BRICKLET_CONFIG_STR_CS_POS     (BRICKLET_CONFIG_STR_GROUP_POS + 4)

#define BRICKLET_RPI_HAT_SPIDEV        "/dev/spidev0.0"
#define BRICKLET_RPI_HAT_SPIDEV_NUM    0
#define BRICKLET_RPI_HAT_MASTER_CS     8

#define BRICKLET_RPI_PRODUCT_ID        "0x084e"
#define BRICKLET_RPI_RPODUCT_ID_LENGTH 6

static LogSource _log_source = LOG_SOURCE_INITIALIZER;

// We support up to two parallel SPI hardware units, each one of those needs a mutex.
static Mutex _bricklet_spi_mutex[BRICKLET_SPI_MAX_NUM];
static int _bricklet_stack_num = 0;
static BrickletStack *_bricklet_stack[BRICKLET_SPI_MAX_NUM*BRICKLET_CS_MAX_NUM] = {NULL};

// The "connected to uid" can be overwritten if the UID of the HAT itself is known.
// In this case the Bricklets will be shown as connected to the HAT in Brick Viewer.
static uint32_t bricklet_connected_uid = 0;

// GPIOs configuration for HAT Bricklet.
static const uint8_t bricklet_stack_rpi_hat_gpios[] = {23, 27, 24, 22, 25, 7, 26, 6, 5};

// The equivalent configuration in brickd.conf looks as follows:
/**************************************
bricklet.group0.spidev = /dev/spidev0.0

bricklet.group0.cs0.driver = gpio
bricklet.group0.cs0.name = gpio23
bricklet.group0.cs0.num = 23

bricklet.group0.cs1.driver = gpio
bricklet.group0.cs1.name = gpio27
bricklet.group0.cs1.num = 27

bricklet.group0.cs2.driver = gpio
bricklet.group0.cs2.name = gpio24
bricklet.group0.cs2.num = 24

bricklet.group0.cs3.driver = gpio
bricklet.group0.cs3.name = gpio22
bricklet.group0.cs3.num = 22

bricklet.group0.cs4.driver = gpio
bricklet.group0.cs4.name = gpio25
bricklet.group0.cs4.num = 25

bricklet.group0.cs5.driver = gpio
bricklet.group0.cs5.name = gpio7
bricklet.group0.cs5.num = 7

bricklet.group0.cs6.driver = gpio
bricklet.group0.cs6.name = gpio26
bricklet.group0.cs6.num = 26

bricklet.group0.cs7.driver = gpio
bricklet.group0.cs7.name = gpio6
bricklet.group0.cs7.num = 6

bricklet.group0.cs8.driver = gpio
bricklet.group0.cs8.name = gpio5
bricklet.group0.cs8.num = 5
*************************************/

// spidev1.x on RPi does not support CPHA:
// https://www.raspberrypi.org/forums/viewtopic.php?t=186019
// https://www.raspberrypi.org/forums/viewtopic.php?f=44&t=96069
// https://www.raspberrypi.org/forums/viewtopic.php?t=149981
// So we have to keep it at one SPI device for the RPi HAT...

// Additionally, on spidev0.x the SPI_NO_CS option does not work,
// so we can't intermix hardware CS with gpio CS pins. Because
// of this the HAT can only use pins for CS that are not HW CS pins...
int bricklet_init_rpi_hat(void) {
	int fd;
	int rc;
	char product_id[BRICKLET_RPI_RPODUCT_ID_LENGTH+1] = "\0";

    fd = open("/proc/device-tree/hat/product_id", O_RDONLY);
	if(fd < 0) {
		log_debug("Could not open HAT product_id in device tree, brickd will not use a pre-configured HAT configuration.");
		return 1;
	}

    rc = robust_read(fd, &product_id, BRICKLET_RPI_RPODUCT_ID_LENGTH);
	robust_close(fd);
	if(rc != 6) {
		log_debug("Could not read HAT product_id in device tree, brickd will not use a pre-configured HAT configuration.");
		return 1;
	}

    if(strncmp(BRICKLET_RPI_PRODUCT_ID, product_id, BRICKLET_RPI_RPODUCT_ID_LENGTH) != 0) {
        log_debug("The product_id of the connected HAT (%s) is not supported, brickd will not use a pre-configured HAT configuration.", product_id);
		return 1;
    }

    log_debug("Found product_id \"%s\" in device tree, brickd will use pre-configured HAT configuration.", product_id);
    BrickletStackConfig config = {
        .mutex = &_bricklet_spi_mutex[BRICKLET_RPI_HAT_SPIDEV_NUM],
    };

    strcpy(config.spi_device, BRICKLET_RPI_HAT_SPIDEV);
    config.connected_uid = &bricklet_connected_uid;

    for(uint8_t cs = 0; cs < sizeof(bricklet_stack_rpi_hat_gpios); cs++) {
        if(cs == BRICKLET_RPI_HAT_MASTER_CS) {
            config.startup_wait_time = 0;
        } else {
            config.startup_wait_time = 1;
        }
        config.num = _bricklet_stack_num;
        config.chip_select_driver = BRICKLET_CHIP_SELECT_DRIVER_GPIO;
        config.chip_select_gpio_sysfs.num = bricklet_stack_rpi_hat_gpios[cs];
        sprintf(config.chip_select_gpio_sysfs.name, "gpio%d", config.chip_select_gpio_sysfs.num);

        log_debug("Bricklet found: spidev %s, driver %d, name %s (num %d)", 
                    config.spi_device, 
                    config.chip_select_driver, 
                    config.chip_select_gpio_sysfs.name, 
                    config.chip_select_gpio_sysfs.num);

        _bricklet_stack[_bricklet_stack_num] = bricklet_stack_init(&config);
        if(_bricklet_stack[_bricklet_stack_num] == NULL) {
            return -1;
        }

        _bricklet_stack_num++;
    }

    return 0;
}

int bricklet_init(void) {
    int rc;
    int length = 0;
    char str_spidev[]    = "bricklet.groupX.spidev";
    char str_cs_driver[] = "bricklet.groupX.csY.driver";
    char str_cs_name[]   = "bricklet.groupX.csY.name";
    char str_cs_num[]    = "bricklet.groupX.csY.num";

    mutex_create(&_bricklet_spi_mutex[0]);
    mutex_create(&_bricklet_spi_mutex[1]);

    // First we try to find out if this brickd is installed on a RPi with raspbian
    // and a Tinkerforge Bricklet HAT ist on top.
    rc = bricklet_init_rpi_hat();
    if(rc < 0) {
        return -1;
    } else if(rc == 0) {
        return 0;
    }

    // If there is no HAT we try to read the SPI configuration from the config
    for(uint8_t i = 0; i < BRICKLET_SPI_MAX_NUM; i++) {
        BrickletStackConfig config = {
            .mutex = &_bricklet_spi_mutex[i],
            .connected_uid = &bricklet_connected_uid,
            .startup_wait_time = 0,
        };

        str_spidev[BRICKLET_CONFIG_STR_GROUP_POS] = '0' + i;
        length = strlen(config_get_option_value(str_spidev)->string);
        if(length == 0) {
            continue;
        }

        memcpy(config.spi_device, config_get_option_value(str_spidev)->string, length);

        for(uint8_t cs = 0; cs < BRICKLET_CS_MAX_NUM; cs++) {
            config.num = _bricklet_stack_num;

            str_cs_driver[BRICKLET_CONFIG_STR_GROUP_POS] = '0' + i;
            str_cs_driver[BRICKLET_CONFIG_STR_CS_POS]    = '0' + cs;
            config.chip_select_driver = config_get_option_value(str_cs_driver)->symbol;

            if(config.chip_select_driver == CHIP_SELECT_GPIO) {
                str_cs_num[BRICKLET_CONFIG_STR_GROUP_POS] = '0' + i;
                str_cs_num[BRICKLET_CONFIG_STR_CS_POS]    = '0' + cs;
                config.chip_select_gpio_sysfs.num = config_get_option_value(str_cs_num)->integer;

                str_cs_name[BRICKLET_CONFIG_STR_GROUP_POS] = '0' + i;
                str_cs_name[BRICKLET_CONFIG_STR_CS_POS]    = '0' + cs;
                length = strlen(config_get_option_value(str_cs_name)->string);

                if(length == 0) {
                    continue;
                }

                memset(config.chip_select_gpio_sysfs.name, 0, sizeof(config.chip_select_gpio_sysfs.name));
                memcpy(config.chip_select_gpio_sysfs.name, config_get_option_value(str_cs_name)->string, length);
            } else if(config.chip_select_driver != CHIP_SELECT_HARDWARE) {
                continue;
            }

			log_debug("Bricklet found: spidev %s, driver %d, name %s (num %d)", 
                      config.spi_device, 
                      config.chip_select_driver, 
                      config.chip_select_gpio_sysfs.name, 
                      config.chip_select_gpio_sysfs.num);

            _bricklet_stack[_bricklet_stack_num] = bricklet_stack_init(&config);
            if(_bricklet_stack[_bricklet_stack_num] == NULL) {
                return -1;
            }

            _bricklet_stack_num++;
        }
    }

    return 0;
}

void bricklet_exit(void) {
    for(int i = 0; i < _bricklet_stack_num; i++) {
        bricklet_stack_exit(_bricklet_stack[i]); 
    }

    mutex_destroy(&_bricklet_spi_mutex[0]);
    mutex_destroy(&_bricklet_spi_mutex[1]);
}
