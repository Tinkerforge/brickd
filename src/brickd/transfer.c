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

static void LIBUSB_CALL transfer_wrapper(struct libusb_transfer *handle) {
	Transfer *transfer = handle->user_data;

	if (!transfer->submitted) {
		log_error("%s transfer %p returned from %s [%s], but was not submitted before",
		          transfer_get_type_name(transfer->type, 1), transfer,
		          transfer->brick->product, transfer->brick->serial_number);

		return;
	}

	transfer->submitted = 0;
	transfer->completed = 1;

	if (handle->status == LIBUSB_TRANSFER_CANCELLED) {
		log_debug("%s transfer %p for %s [%s] was cancelled",
		          transfer_get_type_name(transfer->type, 1), transfer,
		          transfer->brick->product, transfer->brick->serial_number);

		return;
	} else if (handle->status == LIBUSB_TRANSFER_NO_DEVICE) {
		log_debug("%s transfer %p for %s [%s] was aborted, device got disconnected",
		          transfer_get_type_name(transfer->type, 1), transfer,
		          transfer->brick->product, transfer->brick->serial_number);

		return;
	} else if (handle->status == LIBUSB_TRANSFER_STALL) {
		log_debug("%s transfer %p for %s [%s] got stalled",
		          transfer_get_type_name(transfer->type, 1), transfer,
		          transfer->brick->product, transfer->brick->serial_number);

		return;
	} else if (handle->status != LIBUSB_TRANSFER_COMPLETED) {
		log_warn("%s transfer %p returned with an error from %s [%s]: %s (%d)",
		         transfer_get_type_name(transfer->type, 1), transfer,
		         transfer->brick->product, transfer->brick->serial_number,
		         get_libusb_transfer_status_name(transfer->handle->status),
		         transfer->handle->status);
	} else {
		log_debug("%s transfer %p returned successfully from %s [%s]",
		         transfer_get_type_name(transfer->type, 1), transfer,
		         transfer->brick->product, transfer->brick->serial_number);

		if (transfer->function != NULL) {
			transfer->function(transfer);
		}
	}

	if (transfer->type == TRANSFER_TYPE_READ) {
		transfer_submit(transfer);
	}
}

const char *transfer_get_type_name(TransferType type, int upper) {
	switch (type) {
	case TRANSFER_TYPE_READ:
		return upper ? "Read" : "read";

	case TRANSFER_TYPE_WRITE:
		return upper ? "Write" : "write";

	default:
		return upper ? "<Unknown>" : "<unknown>";
	}
}

int transfer_create(Transfer *transfer, Brick *brick, TransferType type,
                    TransferFunction function) {
	transfer->brick = brick;
	transfer->type = type;
	transfer->submitted = 0;
	transfer->completed = 0;
	transfer->function = function;
	transfer->handle = libusb_alloc_transfer(0);

	if (transfer->handle == NULL) {
		log_error("Could not allocate libusb %s transfer for %s [%s]",
		          transfer_get_type_name(transfer->type, 0),
		          brick->product, brick->serial_number);

		return -1;
	}

	return 0;
}

void transfer_destroy(Transfer *transfer) {
	struct timeval tv;
	time_t start;
	time_t now;
	int rc;

	log_debug("Destroying %s transfer %p for %s [%s]",
	          transfer_get_type_name(transfer->type, 0), transfer,
	          transfer->brick->product, transfer->brick->serial_number);

	if (transfer->submitted) {
		transfer->completed = 0;

		rc = libusb_cancel_transfer(transfer->handle);

		if (rc < 0) {
			log_warn("Could not cancel pending %s transfer %p for %s [%s]: %s (%d)",
			         transfer_get_type_name(transfer->type, 0), transfer,
			         transfer->brick->product, transfer->brick->serial_number,
			         get_libusb_error_name(rc), rc);
		} else {
			tv.tv_sec = 0;
			tv.tv_usec = 0;

			start = time(NULL);
			now = start;

			// FIXME: don't wait 1sec per transfer
			while (!transfer->completed && now >= start && now < start + 1) {
				rc = libusb_handle_events_timeout(transfer->brick->context, &tv);

				if (rc < 0) {
					log_error("Could not handle USB events: %s (%d)",
					          get_libusb_error_name(rc), rc);
				}

				now = time(NULL);
			}

			if (!transfer->completed) {
				log_warn("Attempt to cancel pending %s transfer %p for %s [%s] timed out",
				         transfer_get_type_name(transfer->type, 0), transfer,
				         transfer->brick->product, transfer->brick->serial_number);
			}
		}
	}

	if (!transfer->submitted) {
		libusb_free_transfer(transfer->handle);
	} else {
		log_warn("Leaking pending %s transfer %p for %s [%s]",
		         transfer_get_type_name(transfer->type, 0), transfer,
		         transfer->brick->product, transfer->brick->serial_number);
	}
}

int transfer_submit(Transfer *transfer) {
	uint8_t end_point;
	int length;
	int rc;

	if (transfer->submitted) {
		log_error("%s transfer %p is already submitted for %s [%s]",
		          transfer_get_type_name(transfer->type, 1), transfer,
		          transfer->brick->product, transfer->brick->serial_number);

		return -1;
	}

	switch (transfer->type) {
	case TRANSFER_TYPE_READ:
		end_point = LIBUSB_ENDPOINT_IN + USB_ENDPOINT_IN;
		length = sizeof(Packet);

		break;

	case TRANSFER_TYPE_WRITE:
		end_point = LIBUSB_ENDPOINT_OUT + USB_ENDPOINT_OUT;
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
		log_error("Could not submit %s transfer %p to %s [%s]: %s (%d)",
		          transfer_get_type_name(transfer->type, 0), transfer,
		          transfer->brick->product, transfer->brick->serial_number,
		          get_libusb_error_name(rc), rc);

		transfer->submitted = 0;

		return -1;
	}

	log_debug("Submitted %s transfer %p for %u bytes to %s [%s]",
	          transfer_get_type_name(transfer->type, 0), transfer, length,
	          transfer->brick->product, transfer->brick->serial_number);

	return 0;
}
