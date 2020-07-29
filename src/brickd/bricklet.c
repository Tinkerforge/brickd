/*
 * brickd
 * Copyright (C) 2018 Olaf Lüke <olaf@tinkerforge.com>
 * Copyright (C) 2019 Matthias Bolte <matthias@tinkerforge.com>
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

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>

#include <daemonlib/config.h>
#include <daemonlib/log.h>
#include <daemonlib/threads.h>

#include "bricklet_stack.h"

#define BRICKLET_CONFIG_STR_GROUP_POS     14
#define BRICKLET_CONFIG_STR_CS_POS        (BRICKLET_CONFIG_STR_GROUP_POS + 4)

#define BRICKLET_RPI_HAT_SPIDEV           "/dev/spidev0.0"
#define BRICKLET_RPI_HAT_SPIDEV_NUM       0
#define BRICKLET_RPI_HAT_MASTER_CS        8

#define BRICKLET_RPI_HAT_ZERO_SPIDEV      "/dev/spidev0.0"
#define BRICKLET_RPI_HAT_ZERO_SPIDEV_NUM  0
#define BRICKLET_RPI_HAT_ZERO_MASTER_CS   5

#define BRICKLET_RPI_PRODUCT_ID_LENGTH    6
#define BRICKLET_RPI_HAT_PRODUCT_ID       "0x084e" // tf device id 2126
#define BRICKLET_RPI_HAT_ZERO_PRODUCT_ID  "0x085d" // tf device id 2141

static LogSource _log_source = LOG_SOURCE_INITIALIZER;

// We support up to two parallel SPI hardware units, each one of those needs a mutex.
static Mutex _bricklet_spi_mutex[BRICKLET_SPI_MAX_NUM];
static int _bricklet_stack_count = 0;
static BrickletStack _bricklet_stack[BRICKLET_SPI_MAX_NUM * BRICKLET_CS_MAX_NUM];

// The "connected to uid" can be overwritten if the UID of the HAT itself is known.
// In this case the Bricklets will be shown as connected to the HAT in Brick Viewer.
static uint32_t bricklet_connected_uid = 0;

// GPIOs configuration for HAT Brick.
static const uint8_t bricklet_stack_rpi_hat_gpio_cs[] = {23, 22, 25, 26, 27, 24, 7, 6, 5};
static const uint8_t bricklet_stack_rpi_hat_zero_gpio_cs[] = {27, 23, 24, 22, 25};

// The equivalent configuration in brickd.conf looks as follows:
/**************************************

HAT:

bricklet.group0.spidev = /dev/spidev0.0

bricklet.group0.cs0.driver = gpio
bricklet.group0.cs0.name = gpio23
bricklet.group0.cs0.num = 23

bricklet.group0.cs1.driver = gpio
bricklet.group0.cs1.name = gpio22
bricklet.group0.cs1.num = 22

bricklet.group0.cs2.driver = gpio
bricklet.group0.cs2.name = gpio25
bricklet.group0.cs2.num = 25

bricklet.group0.cs3.driver = gpio
bricklet.group0.cs3.name = gpio26
bricklet.group0.cs3.num = 26

bricklet.group0.cs4.driver = gpio
bricklet.group0.cs4.name = gpio27
bricklet.group0.cs4.num = 27

bricklet.group0.cs5.driver = gpio
bricklet.group0.cs5.name = gpio24
bricklet.group0.cs5.num = 24

bricklet.group0.cs6.driver = gpio
bricklet.group0.cs6.name = gpio7
bricklet.group0.cs6.num = 7

bricklet.group0.cs7.driver = gpio
bricklet.group0.cs7.name = gpio6
bricklet.group0.cs7.num = 6

bricklet.group0.cs8.driver = gpio
bricklet.group0.cs8.name = gpio5
bricklet.group0.cs8.num = 5


HAT Zero:

bricklet.group0.spidev = /dev/spidev0.0

bricklet.group0.cs0.driver = gpio
bricklet.group0.cs0.name = gpio27
bricklet.group0.cs0.num = 27

bricklet.group0.cs1.driver = gpio
bricklet.group0.cs1.name = gpio23
bricklet.group0.cs1.num = 23

bricklet.group0.cs2.driver = gpio
bricklet.group0.cs2.name = gpio24
bricklet.group0.cs2.num = 24

bricklet.group0.cs3.driver = gpio
bricklet.group0.cs3.name = gpio22
bricklet.group0.cs3.num = 22

bricklet.group0.cs4.driver = gpio
bricklet.group0.cs4.name = gpio25
bricklet.group0.cs4.num = 25
*************************************/

// spidev1.x on RPi does not support CPHA:
// https://www.raspberrypi.org/forums/viewtopic.php?t=186019
// https://www.raspberrypi.org/forums/viewtopic.php?f=44&t=96069
// https://www.raspberrypi.org/forums/viewtopic.php?t=149981
// So we have to keep it at one SPI device for the RPi HAT...

// Additionally, on spidev0.x the SPI_NO_CS option does not work,
// so we can't intermix hardware CS with gpio CS pins. Because
// of this the HAT can only use pins for CS that are not HW CS pins...
int bricklet_init_rpi_hat(const char *product_id_test, const char *spidev,
                          const int spidev_num, const uint8_t *gpio_cs,
                          const int gpio_cs_num, const int master_cs,
                          const char *name, const bool last) {
	char product_id[BRICKLET_RPI_PRODUCT_ID_LENGTH+1] = "\0";
	BrickletStackConfig config;
	char str_sleep_between_reads_bricklet[] = "bricklet.portX.sleep_between_reads";
	char str_sleep_between_reads_hat[]      = "bricklet.portHAT.sleep_between_reads";

#ifdef BRICKD_UWP_BUILD
	#if defined BRICKD_WITH_UWP_HAT_BRICK && defined BRICKD_WITH_UWP_HAT_ZERO_BRICK
		#error HAT Brick and HAT Zero Brick support cannot be enabled at the same time
	#elif defined BRICKD_WITH_UWP_HAT_BRICK
	strcpy(product_id, BRICKLET_RPI_HAT_PRODUCT_ID);
	#elif defined BRICKD_WITH_UWP_HAT_ZERO_BRICK
	strcpy(product_id, BRICKLET_RPI_HAT_ZERO_PRODUCT_ID);
	#endif
#else
	int fd = open("/proc/device-tree/hat/product_id", O_RDONLY);
	int rc;

	if(fd < 0) {
		log_debug("Could not open HAT product_id in device tree, not using pre-configured %s Brick setup", name);
		return 1;
	}

	rc = robust_read(fd, &product_id, BRICKLET_RPI_PRODUCT_ID_LENGTH);

	robust_close(fd);

	if(rc != BRICKLET_RPI_PRODUCT_ID_LENGTH) {
		log_debug("Could not read HAT product_id in device tree, not using pre-configured %s Brick setup", name);
		return 1;
	}
#endif

	if(strncmp(product_id_test, product_id, BRICKLET_RPI_PRODUCT_ID_LENGTH) != 0) {
		if(last) {
			log_debug("The product_id of the connected HAT (%s) is not supported, not using pre-configured %s Brick setup", product_id, name);
		}

		return 1;
	}

	log_info("Found product_id \"%s\" in device tree, using pre-configured %s Brick setup", product_id, name);

	memset(&config, 0, sizeof(config));

	config.mutex = &_bricklet_spi_mutex[spidev_num];
	strcpy(config.spidev, spidev);
	config.connected_uid = &bricklet_connected_uid;

	for(uint8_t cs = 0; cs < gpio_cs_num; cs++) {
		if(cs == master_cs) {
			config.startup_wait_time = 0;
		} else {
			config.startup_wait_time = 1000;
		}

		config.index = _bricklet_stack_count;
#ifdef BRICKD_UWP_BUILD
		// FIXME: UWP in contrast to Linux doesn't allow to control the dedicated
		//        hardware chip-select pins as GPIO pins while the SPI device is enabled
		config.chip_select_driver = gpio_cs[cs] == 7 ? BRICKLET_CHIP_SELECT_DRIVER_HARDWARE : BRICKLET_CHIP_SELECT_DRIVER_GPIO;
#else
		config.chip_select_driver = BRICKLET_CHIP_SELECT_DRIVER_GPIO;
#endif
		config.chip_select_gpio_num = gpio_cs[cs];

		if(cs == gpio_cs_num - 1) { // Last CS is the HAT itself
			config.sleep_between_reads = config_get_option_value(str_sleep_between_reads_hat)->integer;
		} else {
			str_sleep_between_reads_bricklet[13] = 'A' + cs;
			config.sleep_between_reads = config_get_option_value(str_sleep_between_reads_bricklet)->integer;
		}

		sprintf(config.chip_select_gpio_name, "gpio%d", config.chip_select_gpio_num);

		log_debug("Bricklet found: spidev %s, driver %d, name %s (num %d)",
		          config.spidev,
		          config.chip_select_driver,
		          config.chip_select_gpio_name,
		          config.chip_select_gpio_num);

		if(bricklet_stack_create(&_bricklet_stack[_bricklet_stack_count], &config) < 0) {
			return -1;
		}

		_bricklet_stack_count++;
	}

	return 0;
}

int bricklet_init_hctosys(void) {
#ifdef BRICKD_UWP_BUILD
	// FIXME: how to use RTC chip on UWP?
	return 0;
#else
	FILE *fp;
	char buffer[256];
	int rc;

	buffer[0] = '\0';

	fp = popen("/sbin/hwclock --hctosys", "r");

	if(fp == NULL) {
		log_debug("Could not popen /sbin/hwclock, time will not be updated");
		return -1;
	}

	// If there is no error, we expect that hwclock does not print anything
	// to stdout or stderr and the exit code is 0.
	if(fgets(buffer, sizeof(buffer), fp) == NULL) {
		rc = pclose(fp);

		if(rc == 0) {
			log_debug("Updated system time to RTC time with \"hwclock --hctosys\"");
			return 0;
		} else {
			log_debug("Unexpected exit code of \"hwclock --hctosys\"-call: %d", rc);
			return -1;
		}
	}

	rc = pclose(fp);
	log_debug("Unexpected return from /sbin/hwclock call (exit code %d): %s", rc, buffer);

	return -1;
#endif
}

int bricklet_init(void) {
	int rc;
	int length = 0;
	char str_spidev[]              = "bricklet.groupX.spidev";
	char str_cs_driver[]           = "bricklet.groupX.csY.driver";
	char str_cs_name[]             = "bricklet.groupX.csY.name";
	char str_cs_num[]              = "bricklet.groupX.csY.num";
	char str_sleep_between_reads[] = "bricklet.portX.sleep_between_reads";
	BrickletStackConfig config;

	mutex_create(&_bricklet_spi_mutex[0]);
	mutex_create(&_bricklet_spi_mutex[1]);

	// First we try to find out if this brickd is installed on a RPi with Raspbian
	// and a Tinkerforge HAT Brick is on top
	rc = bricklet_init_rpi_hat(BRICKLET_RPI_HAT_PRODUCT_ID,
	                           BRICKLET_RPI_HAT_SPIDEV,
	                           BRICKLET_RPI_HAT_SPIDEV_NUM,
	                           bricklet_stack_rpi_hat_gpio_cs,
	                           sizeof(bricklet_stack_rpi_hat_gpio_cs),
	                           BRICKLET_RPI_HAT_MASTER_CS,
	                           "HAT",
	                           false);

	if(rc < 0) {
		return -1;
	} else if(rc == 0) {
		// The HAT Brick has a RTC.
		// If we find one, we update the system time with the RTC time.
		bricklet_init_hctosys();
		return 0;
	}

	// or a Tinkerforge HAT Zero Brick is on top
	rc = bricklet_init_rpi_hat(BRICKLET_RPI_HAT_ZERO_PRODUCT_ID,
	                           BRICKLET_RPI_HAT_ZERO_SPIDEV,
	                           BRICKLET_RPI_HAT_ZERO_SPIDEV_NUM,
	                           bricklet_stack_rpi_hat_zero_gpio_cs,
	                           sizeof(bricklet_stack_rpi_hat_zero_gpio_cs),
	                           BRICKLET_RPI_HAT_ZERO_MASTER_CS,
	                           "HAT Zero",
	                           true);

	if(rc < 0) {
		return -1;
	} else if(rc == 0) {
		return 0;
	}

	// If there is no HAT we try to read the SPI configuration from the config
	for(uint8_t i = 0; i < BRICKLET_SPI_MAX_NUM; i++) {
		memset(&config, 0, sizeof(config));

		config.mutex = &_bricklet_spi_mutex[i];
		config.connected_uid = &bricklet_connected_uid;
		config.startup_wait_time = 0;

		str_spidev[BRICKLET_CONFIG_STR_GROUP_POS] = '0' + i;
		length = strlen(config_get_option_value(str_spidev)->string);

		if(length == 0) {
			continue;
		}

		memcpy(config.spidev, config_get_option_value(str_spidev)->string, length);

		for(uint8_t cs = 0; cs < BRICKLET_CS_MAX_NUM; cs++) {
			config.index = _bricklet_stack_count;

			str_cs_driver[BRICKLET_CONFIG_STR_GROUP_POS] = '0' + i;
			str_cs_driver[BRICKLET_CONFIG_STR_CS_POS]    = '0' + cs;
			config.chip_select_driver = config_get_option_value(str_cs_driver)->symbol;

			str_sleep_between_reads[13] = 'A' + cs;
			config.sleep_between_reads = config_get_option_value(str_sleep_between_reads)->integer;

			if(config.chip_select_driver == BRICKLET_CHIP_SELECT_DRIVER_GPIO) {
				str_cs_num[BRICKLET_CONFIG_STR_GROUP_POS] = '0' + i;
				str_cs_num[BRICKLET_CONFIG_STR_CS_POS]    = '0' + cs;
				config.chip_select_gpio_num = config_get_option_value(str_cs_num)->integer;

				str_cs_name[BRICKLET_CONFIG_STR_GROUP_POS] = '0' + i;
				str_cs_name[BRICKLET_CONFIG_STR_CS_POS]    = '0' + cs;
				length = strlen(config_get_option_value(str_cs_name)->string);

				if(length == 0) {
					continue;
				}

				memset(config.chip_select_gpio_name, 0, sizeof(config.chip_select_gpio_name));
				memcpy(config.chip_select_gpio_name, config_get_option_value(str_cs_name)->string, length);
			} else if(config.chip_select_driver != BRICKLET_CHIP_SELECT_DRIVER_HARDWARE) {
				continue;
			}

			log_debug("Bricklet found: spidev %s, driver %d, name %s (num %d)",
			          config.spidev,
			          config.chip_select_driver,
			          config.chip_select_gpio_name,
			          config.chip_select_gpio_num);

			if(bricklet_stack_create(&_bricklet_stack[_bricklet_stack_count], &config) < 0) {
				return -1;
			}

			_bricklet_stack_count++;
		}
	}

	return 0;
}

void bricklet_exit(void) {
	for(int i = 0; i < _bricklet_stack_count; i++) {
		bricklet_stack_destroy(&_bricklet_stack[i]);
	}

	mutex_destroy(&_bricklet_spi_mutex[0]);
	mutex_destroy(&_bricklet_spi_mutex[1]);
}
