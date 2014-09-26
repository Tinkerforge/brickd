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

#include <stdio.h>
#include <errno.h>

//#include <linux/module.h>
#include <daemonlib/log.h>
#include <daemonlib/utils.h>

#define LOG_CATEGORY LOG_CATEGORY_RED_BRICK

#define W5x00_MODULE_MAX_SIZE (1000*200)
#define W5X00_MODULE_PATH "/root/w5x00.ko"

extern int init_module(void *module_image, unsigned long len,
                       const char *param_values);
extern int delete_module(const char *name, int flags);

int red_ethernet_extension_init(int extension) {
	FILE *f;
	char buf[W5x00_MODULE_MAX_SIZE];
	int length;

	log_debug("Loading w5x00 kernel module for position %d", extension);

	// We starting calling "rmmod w5x00".
	// If the module was loaded before, we make sure that it is reloaded with the
	// correct position and mac address.
	if(delete_module("w5x00", 0) < 0) {
		// ENOENT = w5x00 was not loaded (which is OK)
		if(errno != ENOENT) {
			log_warn("Could not remove kernel module: %s (%d)",
					get_errno_name(errno), errno);

			// In this error case we run through, maybe we
			// can load the kernel module anyway.
		}
	}

	f = fopen(W5X00_MODULE_PATH, "rb");
	length = fread(buf, sizeof(char), W5x00_MODULE_MAX_SIZE, f);

	// We abort if the read was not successfull or the buffer was not big enough
	if(length < 0 || length == W5x00_MODULE_MAX_SIZE) {
		log_error("Could not read %s (%d)", W5X00_MODULE_PATH, length);
		return -1;
	}

	if(init_module(buf, length, "") < 0) {
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
