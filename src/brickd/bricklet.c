/*
 * brickd
 * Copyright (C) 2018, 2021 Olaf Lüke <olaf@tinkerforge.com>
 * Copyright (C) 2019, 2021 Matthias Bolte <matthias@tinkerforge.com>
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

#include <errno.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>

#include <daemonlib/config.h>
#include <daemonlib/log.h>
#include <daemonlib/threads.h>

#include "bricklet_stack.h"

#define BRICKLET_CONFIG_STR_GROUP_POS                     14
#define BRICKLET_CONFIG_STR_CS_POS                        (BRICKLET_CONFIG_STR_GROUP_POS + 4)

#ifdef BRICKD_UWP_BUILD
	#define BRICKLET_RPI_HAT_SPIDEV                       "SPI0"
	#define BRICKLET_RPI_HAT_ZERO_SPIDEV                  "SPI0"
#else
	#define BRICKLET_RPI_HAT_SPIDEV                       "/dev/spidev0.%d"
	#define BRICKLET_RPI_HAT_ZERO_SPIDEV                  "/dev/spidev0.%d"
#endif

#define BRICKLET_RPI_HAT_SPIDEV_INDEX                     0
#define BRICKLET_RPI_HAT_ZERO_SPIDEV_INDEX                0

#define BRICKLET_RPI_PRODUCT_ID_LENGTH                    6
#define BRICKLET_RPI_HAT_PRODUCT_ID                       "0x084e" // tf device id 2126
#define BRICKLET_RPI_HAT_ZERO_PRODUCT_ID                  "0x085d" // tf device id 2141

static LogSource _log_source = LOG_SOURCE_INITIALIZER;

// We support up to two parallel SPI hardware units, each one of those needs a mutex.
static Mutex _bricklet_spi_mutex[BRICKLET_SPI_MAX_NUM];
static int _bricklet_stack_count;
static BrickletStack _bricklet_stack[BRICKLET_SPI_MAX_NUM * BRICKLET_CS_MAX_NUM];

// The "connected to uid" can be overwritten if the UID of the HAT itself is known.
// In this case the Bricklets will be shown as connected to the HAT in Brick Viewer.
static uint32_t bricklet_connected_uid = 0;

static const char *_chip_select_driver_names[] = {
	"hardware",
	"gpio",
	"wiringpi"
};

typedef struct {
	int driver;
	int num;
	bool hat_itself;
} BrickletChipSelectConfig;

// Chip select config for HAT Brick
static const BrickletChipSelectConfig bricklet_stack_rpi_hat_cs_config[] = {
	{BRICKLET_CHIP_SELECT_DRIVER_GPIO, 23, false},
	{BRICKLET_CHIP_SELECT_DRIVER_GPIO, 22, false},
	{BRICKLET_CHIP_SELECT_DRIVER_GPIO, 25, false},
	{BRICKLET_CHIP_SELECT_DRIVER_GPIO, 26, false},
	{BRICKLET_CHIP_SELECT_DRIVER_GPIO, 27, false},
	{BRICKLET_CHIP_SELECT_DRIVER_GPIO, 24, false},
#ifdef BRICKD_UWP_BUILD
	// UWP in contrast to Linux doesn't allow to control the dedicated
	// hardware chip-select pins as GPIO pins while the SPI device is active
	{BRICKLET_CHIP_SELECT_DRIVER_HARDWARE, 1, false},
#else
	{BRICKLET_CHIP_SELECT_DRIVER_GPIO, 7, false},
#endif
	{BRICKLET_CHIP_SELECT_DRIVER_GPIO, 6, false},
	{BRICKLET_CHIP_SELECT_DRIVER_GPIO, 5, true},
	{-1, 0, false}
};

// Chip select config for HAT Zero Brick
static const BrickletChipSelectConfig bricklet_stack_rpi_hat_zero_cs_config[] = {
	{BRICKLET_CHIP_SELECT_DRIVER_GPIO, 27, false},
	{BRICKLET_CHIP_SELECT_DRIVER_GPIO, 23, false},
	{BRICKLET_CHIP_SELECT_DRIVER_GPIO, 24, false},
	{BRICKLET_CHIP_SELECT_DRIVER_GPIO, 22, false},
	{BRICKLET_CHIP_SELECT_DRIVER_GPIO, 25, true},
	{-1, 0, false}
};


// The equivalent Linux HAT configuration in brickd.conf looks as follows:
/*

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

*/

// The equivalent Linux HAT Zero configuration in brickd.conf looks as follows:
/*

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

*/


// spidev1.x on RPi does not support CPHA:
// https://www.raspberrypi.org/forums/viewtopic.php?t=186019
// https://www.raspberrypi.org/forums/viewtopic.php?f=44&t=96069
// https://www.raspberrypi.org/forums/viewtopic.php?t=149981
// So we have to keep it at one SPI device for the RPi HAT.

// Example config for accessing HAT Brick port G and H using hardware
// CS driver. Requires to remove all SPI related fragments from HAT
// device tree overlay in order for the /boot/config.txt change to work.
//
// # /etc/brickd.conf
// bricklet.group0.spidev = /dev/spidev0.%d
// bricklet.group0.cs6.driver = hardware
// bricklet.group0.cs6.num = 0
// bricklet.group0.cs7.driver = hardware
// bricklet.group0.cs7.num = 1
//
// # /boot/config.txt
// dtoverlay=spi0-cs,cs0_pin=7,cs1_pin=6

// FIXME: But if the chip select driver is configured as "hardware" then the
//        corresponding GPIO pins that are used by the spidev driver as CS pins
//        have to be manually configure as GPIO output pin to make spidev work.

// Additionally, on spidev0.x the SPI_NO_CS option does not work on Linux,
// so we can't intermix hardware CS with gpio CS pins on Linux. Because
// of this the HAT can only use pins for CS that are not HW CS pins.
int bricklet_init_rpi_hat(const char *product_id_test, const char *spidev,
                          const int spidev_index, const BrickletChipSelectConfig *cs_config,
                          const char *name, const bool last) {
	char product_id[BRICKLET_RPI_PRODUCT_ID_LENGTH+1] = "\0";
	BrickletStackConfig config;
	char str_sleep_between_reads_bricklet[] = "bricklet.portX.sleep_between_reads";
	char str_sleep_between_reads_hat[]      = "bricklet.portHAT.sleep_between_reads";
#ifdef BRICKD_UWP_BUILD
	bool no_hat = false;

	#if defined BRICKD_WITH_UWP_HAT_BRICK && defined BRICKD_WITH_UWP_HAT_ZERO_BRICK
		#error HAT Brick and HAT Zero Brick support cannot be enabled at the same time
	#elif defined BRICKD_WITH_UWP_HAT_BRICK
	strcpy(product_id, BRICKLET_RPI_HAT_PRODUCT_ID);
	#elif defined BRICKD_WITH_UWP_HAT_ZERO_BRICK
	strcpy(product_id, BRICKLET_RPI_HAT_ZERO_PRODUCT_ID);
	#else
	no_hat = true;
	#endif

	if (no_hat) {
		return 1;
	}
#else
	const char *path = "/proc/device-tree/hat/product_id";
	int fd = open(path, O_RDONLY);
	int rc;

	if (fd < 0) {
		if (errno == ENOENT) {
			// log this on debug, because this is the default situation on all non-Raspberry Pi setups
			log_debug("No HAT product_id file in device tree, not using default %s Brick config", name);
		} else {
			log_warn("Could not open %s for reading, not using default %s Brick config: %s (%d)",
			         path, name, get_errno_name(errno), errno);
		}

		return 1;
	}

	rc = robust_read(fd, &product_id, BRICKLET_RPI_PRODUCT_ID_LENGTH);

	robust_close(fd);

	if (rc != BRICKLET_RPI_PRODUCT_ID_LENGTH) {
		if (rc < 0) {
			log_warn("Could not read from %s, not using default %s Brick config: %s (%d)",
			         path, name, get_errno_name(errno), errno);
		} else {
			log_warn("HAT product_id in device tree has wrong length, not using default %s Brick config", name);
		}

		return 1;
	}
#endif

	if (strncmp(product_id_test, product_id, BRICKLET_RPI_PRODUCT_ID_LENGTH) != 0) {
		if (last) {
			log_warn("Found unsupported HAT product_id %s is device tree, not using default %s Brick config", product_id, name);
		}

		return 1;
	}

	log_info("Found supported HAT product_id %s in device tree, using default %s Brick config", product_id, name);

	memset(&config, 0, sizeof(config));

	config.mutex = &_bricklet_spi_mutex[spidev_index];
	config.connected_uid = &bricklet_connected_uid;

	for (uint8_t cs = 0; cs_config[cs].driver >= 0; cs++) {
		if (cs_config[cs].hat_itself) {
			config.startup_wait_time = 0;
		} else {
			config.startup_wait_time = 1000;
		}

		config.index = _bricklet_stack_count;
		config.position = 'A' + cs;
		config.chip_select_driver = cs_config[cs].driver;

		if (config.chip_select_driver == BRICKLET_CHIP_SELECT_DRIVER_HARDWARE) {
			snprintf(config.spidev, sizeof(config.spidev), spidev, cs_config[cs].num);
			config.chip_select_name[0] = '\0';
			config.chip_select_num = cs_config[cs].num;
		} else if (config.chip_select_driver == BRICKLET_CHIP_SELECT_DRIVER_GPIO) {
			snprintf(config.spidev, sizeof(config.spidev), spidev, 0);
			snprintf(config.chip_select_name, sizeof(config.chip_select_name), "gpio%d", cs_config[cs].num);
			config.chip_select_num = cs_config[cs].num;
		} else {
			continue; // FIXME: WiringPi
		}

		if (cs_config[cs].hat_itself) {
			config.sleep_between_reads = config_get_option_value(str_sleep_between_reads_hat)->integer;
		} else {
			str_sleep_between_reads_bricklet[13] = config.position;
			config.sleep_between_reads = config_get_option_value(str_sleep_between_reads_bricklet)->integer;
		}

		log_info("Found Bricklet port %c (spidev: %s, driver: %s, name: %s, num: %d)",
		         config.position, config.spidev,
		         _chip_select_driver_names[config.chip_select_driver],
		         config.chip_select_name[0] == '\0' ? "<unused>" : config.chip_select_name,
		         config.chip_select_num);

		if (bricklet_stack_create(&_bricklet_stack[_bricklet_stack_count], &config) < 0) {
			for (int i = 0; i < _bricklet_stack_count; i++) {
				bricklet_stack_destroy(&_bricklet_stack[i]);
			}

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
	char buffer[256] = "";
	int rc;

	errno = ENOMEM; // popen does not set errno if memory allocation fails
	fp = popen("/sbin/hwclock --hctosys", "r");

	if (fp == NULL) {
		log_warn("Could not execute '/sbin/hwclock --hctosys', system time will not be updated: %s (%d)",
		         get_errno_name(errno), errno);

		return -1;
	}

	// If there is no error, we expect that hwclock does not print anything
	// to stdout or stderr and the exit code is 0.
	if (fgets(buffer, sizeof(buffer), fp) == NULL) {
		rc = pclose(fp);

		if (rc < 0) {
			log_warn("Could not read '/sbin/hwclock --hctosys' exit code: %s (%d)",
			         get_errno_name(errno), errno);

			return -1;
		}

		log_info("Updated system time from RTC time using '/sbin/hwclock --hctosys'");

		return 0;
	}

	rc = pclose(fp);

	log_warn("Unexpected output from '/sbin/hwclock --hctosys' (exit-code: %d): %s", rc, buffer);

	return -1;
#endif
}

int bricklet_init(void) {
	int rc;
	char str_spidev[]              = "bricklet.groupX.spidev";
	char *spidev;
	char str_cs_driver[]           = "bricklet.groupX.csY.driver";
	char str_cs_name[]             = "bricklet.groupX.csY.name";
	char *cs_name;
	char str_cs_num[]              = "bricklet.groupX.csY.num";
	char str_sleep_between_reads[] = "bricklet.portX.sleep_between_reads";
	BrickletStackConfig config;
	bool first = true;

	mutex_create(&_bricklet_spi_mutex[0]);
	mutex_create(&_bricklet_spi_mutex[1]);

	_bricklet_stack_count = 0;

	// First we try to find out if this brickd is installed on a RPi with Raspbian
	// and a Tinkerforge HAT Brick is on top
	rc = bricklet_init_rpi_hat(BRICKLET_RPI_HAT_PRODUCT_ID,
	                           BRICKLET_RPI_HAT_SPIDEV,
	                           BRICKLET_RPI_HAT_SPIDEV_INDEX,
	                           bricklet_stack_rpi_hat_cs_config,
	                           "HAT",
	                           false);

	if (rc < 0) {
		return -1;
	}

	if (rc == 0) {
		// The HAT Brick has a RTC.
		// If we find one, we update the system time with the RTC time.
		bricklet_init_hctosys();
		return 0;
	}

	// or a Tinkerforge HAT Zero Brick is on top
	rc = bricklet_init_rpi_hat(BRICKLET_RPI_HAT_ZERO_PRODUCT_ID,
	                           BRICKLET_RPI_HAT_ZERO_SPIDEV,
	                           BRICKLET_RPI_HAT_ZERO_SPIDEV_INDEX,
	                           bricklet_stack_rpi_hat_zero_cs_config,
	                           "HAT Zero",
	                           false);

	if (rc < 0) {
		return -1;
	}

	if (rc == 0) {
		return 0;
	}

	// If there is no HAT we try to read the SPI configuration from the config
	// log this on debug, because this is the default situation on all non-Raspberry Pi setups
	log_debug("Found no supported HAT product_id in device tree, checking bricklet.* section in config file instead");

	for (uint8_t i = 0; i < BRICKLET_SPI_MAX_NUM; i++) {
		memset(&config, 0, sizeof(config));

		config.mutex = &_bricklet_spi_mutex[i];
		config.connected_uid = &bricklet_connected_uid;
		config.startup_wait_time = 0;

		str_spidev[BRICKLET_CONFIG_STR_GROUP_POS] = '0' + i;
		spidev = config_get_option_value(str_spidev)->string;

		if (spidev == NULL) {
			continue;
		}

		for (uint8_t cs = 0; cs < BRICKLET_CS_MAX_NUM; cs++) {
			config.index = _bricklet_stack_count;
			config.position = 'A' + cs;

			str_cs_driver[BRICKLET_CONFIG_STR_GROUP_POS] = '0' + i;
			str_cs_driver[BRICKLET_CONFIG_STR_CS_POS]    = '0' + cs;
			config.chip_select_driver = config_get_option_value(str_cs_driver)->symbol;

			str_sleep_between_reads[13] = config.position;
			config.sleep_between_reads = config_get_option_value(str_sleep_between_reads)->integer;

			if (config.chip_select_driver == BRICKLET_CHIP_SELECT_DRIVER_GPIO) {
				str_cs_name[BRICKLET_CONFIG_STR_GROUP_POS] = '0' + i;
				str_cs_name[BRICKLET_CONFIG_STR_CS_POS]    = '0' + cs;
				cs_name = config_get_option_value(str_cs_name)->string;

				if (cs_name == NULL) {
					continue;
				}

				memcpy(config.chip_select_name, cs_name, BRICKLET_CS_NAME_MAX_LENGTH);

				str_cs_num[BRICKLET_CONFIG_STR_GROUP_POS] = '0' + i;
				str_cs_num[BRICKLET_CONFIG_STR_CS_POS]    = '0' + cs;
				config.chip_select_num = config_get_option_value(str_cs_num)->integer;

				snprintf(config.spidev, sizeof(config.spidev), spidev, 0);
			} else if (config.chip_select_driver == BRICKLET_CHIP_SELECT_DRIVER_HARDWARE) {
				config.chip_select_name[0] = '\0';

				str_cs_num[BRICKLET_CONFIG_STR_GROUP_POS] = '0' + i;
				str_cs_num[BRICKLET_CONFIG_STR_CS_POS]    = '0' + cs;
				config.chip_select_num = config_get_option_value(str_cs_num)->integer;

				snprintf(config.spidev, sizeof(config.spidev), spidev, config.chip_select_num);
			} else {
				continue; // FIXME: WiringPi
			}

			if (first) {
				log_info("Using bricklet.* section in config file");

				first = false;
			}

			log_info("Found Bricklet port %c (spidev: %s, driver: %s, name: %s, num: %d)",
			         config.position, config.spidev,
			         _chip_select_driver_names[config.chip_select_driver],
			         config.chip_select_name[0] == '\0' ? "<unused>" : config.chip_select_name,
			         config.chip_select_num);

			if (bricklet_stack_create(&_bricklet_stack[_bricklet_stack_count], &config) < 0) {
				for (int k = 0; k < _bricklet_stack_count; k++) {
					bricklet_stack_destroy(&_bricklet_stack[k]);
				}

				return -1;
			}

			_bricklet_stack_count++;
		}
	}

	if (_bricklet_stack_count == 0) {
		// log this on debug, because this is the default situation on all non-HAT setups
		log_debug("Found no bricklet.* section in config file");
	}

	return 0;
}

void bricklet_exit(void) {
	for (int i = 0; i < _bricklet_stack_count; i++) {
		bricklet_stack_destroy(&_bricklet_stack[i]);
	}

	mutex_destroy(&_bricklet_spi_mutex[0]);
	mutex_destroy(&_bricklet_spi_mutex[1]);
}
