/*
 * brickd
 * Copyright (C) 2012 Matthias Bolte <matthias@tinkerforge.com>
 *
 * transfer.c: libusb transfer specific functions
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

#include <libusb.h>

#include "transfer.h"

#include "log.h"
#include "usb.h"

#define LOG_CATEGORY LOG_CATEGORY_USB

static const char *transfer_get_type_name(int type) {
	switch (type) {
	case TRANSFER_READ:
		return "read";

	case TRANSFER_WRITE:
		return "write";

	default:
		return NULL;
	}
}

static void LIBUSB_CALL transfer_wrapper(struct libusb_transfer *handle) {
	Transfer *transfer = handle->user_data;

	transfer->submitted = 0;

	if (transfer->function != NULL) {
		transfer->function(transfer);
	}
}

int transfer_create(Transfer *transfer, Brick *brick, int type,
                    TransferFunction function) {
	transfer->brick = brick;
	transfer->type = type;
	transfer->submitted = 0;
	transfer->function = function;
	transfer->handle = libusb_alloc_transfer(0);

	if (transfer->handle == NULL) {
		log_error("Could not allocate libusb %s transfer for %s [%s]",
		          transfer_get_type_name(transfer->type),
		          brick->product, brick->serial_number);

		return -1;
	}

	return 0;
}

void transfer_destroy(Transfer *transfer) {
	// FIXME: assume that transfer is not submitted

	if (transfer->submitted) {
		log_warn("Trying to destroy a submitted transfer for %s [%s]",
		         transfer->brick->product, transfer->brick->serial_number);
	}

	libusb_free_transfer(transfer->handle);
}

int transfer_submit(Transfer *transfer) {
	int end_point;
	int length;
	const char *type_name = transfer_get_type_name(transfer->type);
	int rc;

	if (transfer->submitted) {
		log_error("Trying to submit an already submitted transfer for %s [%s]",
		          transfer->brick->product, transfer->brick->serial_number);

		return -1;
	}

	switch (transfer->type) {
	case TRANSFER_READ:
		end_point = USB_ENDPOINT_IN + 0x80;
		length = sizeof(Packet);

		break;

	case TRANSFER_WRITE:
		end_point = USB_ENDPOINT_OUT;
		length = transfer->packet.header.length;

		break;

	default:
		log_error("Transfer for %s [%s] has invalid type",
		          transfer->brick->product, transfer->brick->serial_number);

		return -1;
	}

	transfer->submitted = 1;

	libusb_fill_bulk_transfer(transfer->handle,
	                          transfer->brick->device_handle,
	                          end_point,
	                          (unsigned char *)&transfer->packet,
	                          length,
	                          transfer_wrapper,
	                          transfer,
	                          0);

	rc = libusb_submit_transfer(transfer->handle);

	if (rc < 0) {
		log_error("Could not submit %s transfer to %s [%s]: %s (%d)",
		          type_name,
		          transfer->brick->product, transfer->brick->serial_number,
		          get_libusb_error_name(rc), rc);

		transfer->submitted = 0;

		return -1;
	}

	/*log_debug("Submitted %s transfer for %u bytes to %s [%s]",
	          type_name, length,
	          transfer->brick->product, transfer->brick->serial_number);*/

	return 0;
}
