/*
 * brickd
 * Copyright (C) 2014 Matthias Bolte <matthias@tinkerforge.com>
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

ConfigOption config_options[] = {
	CONFIG_OPTION_STRING_INITIALIZER("listen.address", NULL, 1, -1, "0.0.0.0"),
	CONFIG_OPTION_INTEGER_INITIALIZER("listen.plain_port", "listen.port", 1, UINT16_MAX, 4223),
	CONFIG_OPTION_INTEGER_INITIALIZER("listen.websocket_port", NULL, 0, UINT16_MAX, 0), // default to enable: 4280
	CONFIG_OPTION_BOOLEAN_INITIALIZER("listen.dual_stack", NULL, 0),
	CONFIG_OPTION_STRING_INITIALIZER("authentication.secret", NULL, 0, 64, NULL),
	CONFIG_OPTION_LOG_LEVEL_INITIALIZER("log_level.event", NULL, LOG_LEVEL_INFO),
	CONFIG_OPTION_LOG_LEVEL_INITIALIZER("log_level.usb", NULL, LOG_LEVEL_INFO),
	CONFIG_OPTION_LOG_LEVEL_INITIALIZER("log_level.network", NULL, LOG_LEVEL_INFO),
	CONFIG_OPTION_LOG_LEVEL_INITIALIZER("log_level.hotplug", NULL, LOG_LEVEL_INFO),
	CONFIG_OPTION_LOG_LEVEL_INITIALIZER("log_level.hardware", NULL, LOG_LEVEL_INFO),
	CONFIG_OPTION_LOG_LEVEL_INITIALIZER("log_level.websocket", NULL, LOG_LEVEL_INFO),
	CONFIG_OPTION_LOG_LEVEL_INITIALIZER("log_level.red_brick", NULL, LOG_LEVEL_INFO),
	CONFIG_OPTION_LOG_LEVEL_INITIALIZER("log_level.other", NULL, LOG_LEVEL_INFO),
	CONFIG_OPTION_NULL_INITIALIZER // end of list
};
