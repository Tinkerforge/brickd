/*
 * brickd
 * Copyright (C) 2018 Olaf Lüke <olaf@tinkerforge.com>
 *
 * bricklet.c: SPI Tinkerforge Protocol (SPITFP) implementation for direct
 *             communication between brickd and Bricklets with co-processor
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

#include <daemonlib/log.h>

static LogSource _log_source = LOG_SOURCE_INITIALIZER;

static Bricklet _bricklet;

// New packet from brickd event loop is queued to be written to Bricklet via SPI
static int bricklet_dispatch_to_spi(Stack *stack, Packet *request, Recipient *recipient) {

    return 0;
}

int bricklet_init(void) {
    int phase = 0;

    log_debug("Initializing Bricklet subsystem");

	// create base stack
	if (stack_create(&_bricklet.base, "bricklet", bricklet_dispatch_to_spi) < 0) {
		log_error("Could not create base stack for Bricklet: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 1;

    cleanup:
	switch (phase) { // no breaks, all cases fall through intentionally

	case 0:
		stack_destroy(&_bricklet.base);
		// fall through

	default:
		break;
	}

	return phase == 1 ? 0 : -1;
}

void bricklet_exit(void) {
	hardware_remove_stack(&_bricklet.base);
	stack_destroy(&_bricklet.base);
}