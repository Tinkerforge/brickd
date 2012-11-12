/*
 * brickd
 * Copyright (C) 2012 Matthias Bolte <matthias@tinkerforge.com>
 *
 * transfer.h: libusb transfer specific functions
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

#ifndef BRICKD_TRANSFER_H
#define BRICKD_TRANSFER_H

#include <libusb.h>

#include "brick.h"
#include "packet.h"

typedef enum {
	TRANSFER_TYPE_READ = 0,
	TRANSFER_TYPE_WRITE
} TransferType;

typedef struct _Transfer Transfer;

typedef void (*TransferFunction)(Transfer *transfer);

struct _Transfer {
	Brick *brick;
	TransferType type;
	int submitted;
	int completed;
	TransferFunction function;
	struct libusb_transfer *handle;
	Packet packet;
};

const char *transfer_get_type_name(TransferType type, int upper);

int transfer_create(Transfer *transfer, Brick *brick, TransferType type,
                    TransferFunction function);
void transfer_destroy(Transfer *transfer);

int transfer_submit(Transfer *transfer);

#endif // BRICKD_TRANSFER_H
