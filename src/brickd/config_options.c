/*
 * brickd
 * Copyright (C) 2014, 2017-2018 Matthias Bolte <matthias@tinkerforge.com>
 * Copyright (C) 2016 Ishraq Ibne Ashraf <ishraq@tinkerforge.com>
 * Copyright (C) 2018 Olaf Lüke <olaf@tinkerforge.com>
 *
 * config_options.c: Config options
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

#include <daemonlib/config.h>
#include <daemonlib/enum.h>
#ifdef BRICKD_WITH_RED_BRICK
	#include <daemonlib/red_led.h>
#endif

#ifdef BRICKD_WITH_BRICKLET
	#include "bricklet.h"
#endif


#ifdef BRICKD_WITH_RED_BRICK

static EnumValueName _red_led_trigger_enum_value_names[] = {
	{ RED_LED_TRIGGER_CPU,       "cpu" },
	{ RED_LED_TRIGGER_GPIO,      "gpio" },
	{ RED_LED_TRIGGER_HEARTBEAT, "heartbeat" },
	{ RED_LED_TRIGGER_MMC,       "mmc" },
	{ RED_LED_TRIGGER_OFF,       "off" },
	{ RED_LED_TRIGGER_ON,        "on" },
	{ -1,                        NULL }
};

static int config_parse_red_led_trigger(const char *string, int *value) {
	return enum_get_value(_red_led_trigger_enum_value_names, string, value, true);
}

static const char *config_format_red_led_trigger(int value) {
	return enum_get_name(_red_led_trigger_enum_value_names, value, "<unknown>");
}

#endif

#ifdef BRICKD_WITH_BRICKLET

static EnumValueName _bricklet_chip_select_driver_enum_value_names[] = {
	{ BRICKLET_CHIP_SELECT_DRIVER_HARDWARE, "hardware" },
	{ BRICKLET_CHIP_SELECT_DRIVER_GPIO,     "gpio" },
	{ BRICKLET_CHIP_SELECT_DRIVER_WIRINGPI, "wiringpi" },
	{ -1,                                   NULL }
};

static int config_parse_bricklet_chip_select_driver(const char *string, int *value) {
	return enum_get_value(_bricklet_chip_select_driver_enum_value_names, string, value, true);
}

static const char *config_format_bricklet_chip_select_driver(int value) {
	return enum_get_name(_bricklet_chip_select_driver_enum_value_names, value, "<unknown>");
}

#endif

ConfigOption config_options[] = {
	CONFIG_OPTION_STRING_INITIALIZER("listen.address", 1, -1, "127.0.0.1"),
	CONFIG_OPTION_INTEGER_INITIALIZER("listen.plain_port", 1, UINT16_MAX, 4223),
	CONFIG_OPTION_INTEGER_INITIALIZER("listen.websocket_port", 0, UINT16_MAX, 0), // default to enable: 4280
	CONFIG_OPTION_INTEGER_INITIALIZER("listen.mesh_gateway_port", 1, UINT16_MAX, 4240),
	CONFIG_OPTION_BOOLEAN_INITIALIZER("listen.dual_stack", false),
	CONFIG_OPTION_STRING_INITIALIZER("authentication.secret", 0, 64, NULL),
	CONFIG_OPTION_SYMBOL_INITIALIZER("log.level", config_parse_log_level, config_format_log_level, LOG_LEVEL_INFO),
	CONFIG_OPTION_STRING_INITIALIZER("log.debug_filter", 0, -1, NULL),
#ifdef BRICKD_WITH_RED_BRICK
	CONFIG_OPTION_SYMBOL_INITIALIZER("led_trigger.green", config_parse_red_led_trigger, config_format_red_led_trigger, RED_LED_TRIGGER_HEARTBEAT),
	CONFIG_OPTION_SYMBOL_INITIALIZER("led_trigger.red", config_parse_red_led_trigger, config_format_red_led_trigger, RED_LED_TRIGGER_OFF),
	CONFIG_OPTION_INTEGER_INITIALIZER("poll_delay.spi", 50, INT32_MAX, 50), // microseconds
	CONFIG_OPTION_INTEGER_INITIALIZER("poll_delay.rs485", 50, INT32_MAX, 4000), // microseconds
#endif
#ifdef BRICKD_WITH_BRICKLET
	CONFIG_OPTION_STRING_INITIALIZER("bricklet.group0.spidev", 0, 64, ""),
	CONFIG_OPTION_STRING_INITIALIZER("bricklet.group1.spidev", 0, 64, ""),

	CONFIG_OPTION_SYMBOL_INITIALIZER("bricklet.group0.cs0.driver", config_parse_bricklet_chip_select_driver, config_format_bricklet_chip_select_driver, -1),
	CONFIG_OPTION_SYMBOL_INITIALIZER("bricklet.group0.cs1.driver", config_parse_bricklet_chip_select_driver, config_format_bricklet_chip_select_driver, -1),
	CONFIG_OPTION_SYMBOL_INITIALIZER("bricklet.group0.cs2.driver", config_parse_bricklet_chip_select_driver, config_format_bricklet_chip_select_driver, -1),
	CONFIG_OPTION_SYMBOL_INITIALIZER("bricklet.group0.cs3.driver", config_parse_bricklet_chip_select_driver, config_format_bricklet_chip_select_driver, -1),
	CONFIG_OPTION_SYMBOL_INITIALIZER("bricklet.group0.cs4.driver", config_parse_bricklet_chip_select_driver, config_format_bricklet_chip_select_driver, -1),
	CONFIG_OPTION_SYMBOL_INITIALIZER("bricklet.group0.cs5.driver", config_parse_bricklet_chip_select_driver, config_format_bricklet_chip_select_driver, -1),
	CONFIG_OPTION_SYMBOL_INITIALIZER("bricklet.group0.cs6.driver", config_parse_bricklet_chip_select_driver, config_format_bricklet_chip_select_driver, -1),
	CONFIG_OPTION_SYMBOL_INITIALIZER("bricklet.group0.cs7.driver", config_parse_bricklet_chip_select_driver, config_format_bricklet_chip_select_driver, -1),
	CONFIG_OPTION_SYMBOL_INITIALIZER("bricklet.group0.cs8.driver", config_parse_bricklet_chip_select_driver, config_format_bricklet_chip_select_driver, -1),
	CONFIG_OPTION_SYMBOL_INITIALIZER("bricklet.group0.cs9.driver", config_parse_bricklet_chip_select_driver, config_format_bricklet_chip_select_driver, -1),
	CONFIG_OPTION_SYMBOL_INITIALIZER("bricklet.group1.cs0.driver", config_parse_bricklet_chip_select_driver, config_format_bricklet_chip_select_driver, -1),
	CONFIG_OPTION_SYMBOL_INITIALIZER("bricklet.group1.cs1.driver", config_parse_bricklet_chip_select_driver, config_format_bricklet_chip_select_driver, -1),
	CONFIG_OPTION_SYMBOL_INITIALIZER("bricklet.group1.cs2.driver", config_parse_bricklet_chip_select_driver, config_format_bricklet_chip_select_driver, -1),
	CONFIG_OPTION_SYMBOL_INITIALIZER("bricklet.group1.cs3.driver", config_parse_bricklet_chip_select_driver, config_format_bricklet_chip_select_driver, -1),
	CONFIG_OPTION_SYMBOL_INITIALIZER("bricklet.group1.cs4.driver", config_parse_bricklet_chip_select_driver, config_format_bricklet_chip_select_driver, -1),
	CONFIG_OPTION_SYMBOL_INITIALIZER("bricklet.group1.cs5.driver", config_parse_bricklet_chip_select_driver, config_format_bricklet_chip_select_driver, -1),
	CONFIG_OPTION_SYMBOL_INITIALIZER("bricklet.group1.cs6.driver", config_parse_bricklet_chip_select_driver, config_format_bricklet_chip_select_driver, -1),
	CONFIG_OPTION_SYMBOL_INITIALIZER("bricklet.group1.cs7.driver", config_parse_bricklet_chip_select_driver, config_format_bricklet_chip_select_driver, -1),
	CONFIG_OPTION_SYMBOL_INITIALIZER("bricklet.group1.cs8.driver", config_parse_bricklet_chip_select_driver, config_format_bricklet_chip_select_driver, -1),
	CONFIG_OPTION_SYMBOL_INITIALIZER("bricklet.group1.cs9.driver", config_parse_bricklet_chip_select_driver, config_format_bricklet_chip_select_driver, -1),

	CONFIG_OPTION_INTEGER_INITIALIZER("bricklet.group0.cs0.num", 0, UINT16_MAX, -1),
	CONFIG_OPTION_INTEGER_INITIALIZER("bricklet.group0.cs1.num", 0, UINT16_MAX, -1),
	CONFIG_OPTION_INTEGER_INITIALIZER("bricklet.group0.cs2.num", 0, UINT16_MAX, -1),
	CONFIG_OPTION_INTEGER_INITIALIZER("bricklet.group0.cs3.num", 0, UINT16_MAX, -1),
	CONFIG_OPTION_INTEGER_INITIALIZER("bricklet.group0.cs4.num", 0, UINT16_MAX, -1),
	CONFIG_OPTION_INTEGER_INITIALIZER("bricklet.group0.cs5.num", 0, UINT16_MAX, -1),
	CONFIG_OPTION_INTEGER_INITIALIZER("bricklet.group0.cs6.num", 0, UINT16_MAX, -1),
	CONFIG_OPTION_INTEGER_INITIALIZER("bricklet.group0.cs7.num", 0, UINT16_MAX, -1),
	CONFIG_OPTION_INTEGER_INITIALIZER("bricklet.group0.cs8.num", 0, UINT16_MAX, -1),
	CONFIG_OPTION_INTEGER_INITIALIZER("bricklet.group0.cs9.num", 0, UINT16_MAX, -1),
	CONFIG_OPTION_INTEGER_INITIALIZER("bricklet.group1.cs0.num", 0, UINT16_MAX, -1),
	CONFIG_OPTION_INTEGER_INITIALIZER("bricklet.group1.cs1.num", 0, UINT16_MAX, -1),
	CONFIG_OPTION_INTEGER_INITIALIZER("bricklet.group1.cs2.num", 0, UINT16_MAX, -1),
	CONFIG_OPTION_INTEGER_INITIALIZER("bricklet.group1.cs3.num", 0, UINT16_MAX, -1),
	CONFIG_OPTION_INTEGER_INITIALIZER("bricklet.group1.cs4.num", 0, UINT16_MAX, -1),
	CONFIG_OPTION_INTEGER_INITIALIZER("bricklet.group1.cs5.num", 0, UINT16_MAX, -1),
	CONFIG_OPTION_INTEGER_INITIALIZER("bricklet.group1.cs6.num", 0, UINT16_MAX, -1),
	CONFIG_OPTION_INTEGER_INITIALIZER("bricklet.group1.cs7.num", 0, UINT16_MAX, -1),
	CONFIG_OPTION_INTEGER_INITIALIZER("bricklet.group1.cs8.num", 0, UINT16_MAX, -1),
	CONFIG_OPTION_INTEGER_INITIALIZER("bricklet.group1.cs9.num", 0, UINT16_MAX, -1),

	CONFIG_OPTION_STRING_INITIALIZER("bricklet.group0.cs0.name", 0, 32, ""),
	CONFIG_OPTION_STRING_INITIALIZER("bricklet.group0.cs1.name", 0, 32, ""),
	CONFIG_OPTION_STRING_INITIALIZER("bricklet.group0.cs2.name", 0, 32, ""),
	CONFIG_OPTION_STRING_INITIALIZER("bricklet.group0.cs3.name", 0, 32, ""),
	CONFIG_OPTION_STRING_INITIALIZER("bricklet.group0.cs4.name", 0, 32, ""),
	CONFIG_OPTION_STRING_INITIALIZER("bricklet.group0.cs5.name", 0, 32, ""),
	CONFIG_OPTION_STRING_INITIALIZER("bricklet.group0.cs6.name", 0, 32, ""),
	CONFIG_OPTION_STRING_INITIALIZER("bricklet.group0.cs7.name", 0, 32, ""),
	CONFIG_OPTION_STRING_INITIALIZER("bricklet.group0.cs8.name", 0, 32, ""),
	CONFIG_OPTION_STRING_INITIALIZER("bricklet.group0.cs9.name", 0, 32, ""),
	CONFIG_OPTION_STRING_INITIALIZER("bricklet.group1.cs0.name", 0, 32, ""),
	CONFIG_OPTION_STRING_INITIALIZER("bricklet.group1.cs1.name", 0, 32, ""),
	CONFIG_OPTION_STRING_INITIALIZER("bricklet.group1.cs2.name", 0, 32, ""),
	CONFIG_OPTION_STRING_INITIALIZER("bricklet.group1.cs3.name", 0, 32, ""),
	CONFIG_OPTION_STRING_INITIALIZER("bricklet.group1.cs4.name", 0, 32, ""),
	CONFIG_OPTION_STRING_INITIALIZER("bricklet.group1.cs5.name", 0, 32, ""),
	CONFIG_OPTION_STRING_INITIALIZER("bricklet.group1.cs6.name", 0, 32, ""),
	CONFIG_OPTION_STRING_INITIALIZER("bricklet.group1.cs7.name", 0, 32, ""),
	CONFIG_OPTION_STRING_INITIALIZER("bricklet.group1.cs8.name", 0, 32, ""),
	CONFIG_OPTION_STRING_INITIALIZER("bricklet.group1.cs9.name", 0, 32, ""),
#endif
	CONFIG_OPTION_NULL_INITIALIZER // end of list
};