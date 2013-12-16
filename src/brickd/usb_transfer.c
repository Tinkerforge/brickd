/*
 * brickd
 * Copyright (C) 2012-2013 Matthias Bolte <matthias@tinkerforge.com>
 *
 * usb_transfer.c: libusb transfer specific functions
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

#include "usb_transfer.h"

#include "log.h"
#include "stack.h"
#include "usb.h"

#define LOG_CATEGORY LOG_CATEGORY_USB

static const char *usb_transfer_get_status_name(int usb_transfer_status) {
	#define LIBUSB_TRANSFER_STATUS_NAME(code) case code: return #code

	switch (usb_transfer_status) {
	LIBUSB_TRANSFER_STATUS_NAME(LIBUSB_TRANSFER_COMPLETED);
	LIBUSB_TRANSFER_STATUS_NAME(LIBUSB_TRANSFER_ERROR);
	LIBUSB_TRANSFER_STATUS_NAME(LIBUSB_TRANSFER_TIMED_OUT);
	LIBUSB_TRANSFER_STATUS_NAME(LIBUSB_TRANSFER_CANCELLED);
	LIBUSB_TRANSFER_STATUS_NAME(LIBUSB_TRANSFER_STALL);
	LIBUSB_TRANSFER_STATUS_NAME(LIBUSB_TRANSFER_NO_DEVICE);
	LIBUSB_TRANSFER_STATUS_NAME(LIBUSB_TRANSFER_OVERFLOW);

	default: return "<unknown>";
	}
}

static void LIBUSB_CALL usb_transfer_wrapper(struct libusb_transfer *handle) {
	USBTransfer *usb_transfer = handle->user_data;

	if (!usb_transfer->submitted) {
		log_error("%s transfer %p returned from %s, but was not submitted before",
		          usb_transfer_get_type_name(usb_transfer->type, 1), usb_transfer,
		          usb_transfer->usb_stack->base.name);

		return;
	}

	usb_transfer->submitted = 0;
	usb_transfer->completed = 1;

	if (handle->status == LIBUSB_TRANSFER_CANCELLED) {
		log_debug("%s transfer %p for %s was cancelled",
		          usb_transfer_get_type_name(usb_transfer->type, 1), usb_transfer,
		          usb_transfer->usb_stack->base.name);

		return;
	} else if (handle->status == LIBUSB_TRANSFER_NO_DEVICE) {
		log_debug("%s transfer %p for %s was aborted, device got disconnected",
		          usb_transfer_get_type_name(usb_transfer->type, 1), usb_transfer,
		          usb_transfer->usb_stack->base.name);

		return;
	} else if (handle->status == LIBUSB_TRANSFER_STALL) {
		log_debug("%s transfer %p for %s got stalled",
		          usb_transfer_get_type_name(usb_transfer->type, 1), usb_transfer,
		          usb_transfer->usb_stack->base.name);

		return;
	} else if (handle->status != LIBUSB_TRANSFER_COMPLETED) {
		log_warn("%s transfer %p returned with an error from %s: %s (%d)",
		         usb_transfer_get_type_name(usb_transfer->type, 1), usb_transfer,
		         usb_transfer->usb_stack->base.name,
		         usb_transfer_get_status_name(usb_transfer->handle->status),
		         usb_transfer->handle->status);
	} else {
		log_debug("%s transfer %p returned successfully from %s%s",
		          usb_transfer_get_type_name(usb_transfer->type, 1), usb_transfer,
		          usb_transfer->usb_stack->base.name,
		          usb_transfer->canceled
		            ? ", but it was canceled in the meantime"
		            : (!usb_transfer->usb_stack->active
		                 ? ", but the corresponding USB device is not active anymore"
		                 : ""));

		if (usb_transfer->canceled || !usb_transfer->usb_stack->active) {
			return;
		}

		if (usb_transfer->function != NULL) {
			usb_transfer->function(usb_transfer);
		}
	}

	if (usb_transfer->type == USB_TRANSFER_TYPE_READ && !usb_transfer->canceled &&
	    usb_transfer->usb_stack->active) {
		usb_transfer_submit(usb_transfer);
	}
}

const char *usb_transfer_get_type_name(USBTransferType type, int upper) {
	switch (type) {
	case USB_TRANSFER_TYPE_READ:
		return upper ? "Read" : "read";

	case USB_TRANSFER_TYPE_WRITE:
		return upper ? "Write" : "write";

	default:
		return upper ? "<Unknown>" : "<unknown>";
	}
}

int usb_transfer_create(USBTransfer *usb_transfer, USBStack *usb_stack,
                        USBTransferType type, USBTransferFunction function) {
	usb_transfer->usb_stack = usb_stack;
	usb_transfer->type = type;
	usb_transfer->submitted = 0;
	usb_transfer->completed = 0;
	usb_transfer->canceled = 0;
	usb_transfer->function = function;
	usb_transfer->handle = libusb_alloc_transfer(0);

	if (usb_transfer->handle == NULL) {
		log_error("Could not allocate libusb %s transfer for %s",
		          usb_transfer_get_type_name(usb_transfer->type, 0),
		          usb_stack->base.name);

		return -1;
	}

	return 0;
}

void usb_transfer_destroy(USBTransfer *usb_transfer) {
	struct timeval tv;
	time_t start;
	time_t now;
	int rc;

	log_debug("Destroying %s transfer %p for %s",
	          usb_transfer_get_type_name(usb_transfer->type, 0), usb_transfer,
	          usb_transfer->usb_stack->base.name);

	if (usb_transfer->submitted) {
		usb_transfer->completed = 0;
		usb_transfer->canceled = 1;

		rc = libusb_cancel_transfer(usb_transfer->handle);

		// FIXME: if libusb_cancel_transfer fails with LIBUSB_ERROR_NO_DEVICE
		//        then probably free the transfer anyway, as it fails constantly
		//        this way on Windows XP and Mac OS X. but need to verify that
		//        in those cases freeing the transfer won't trigger a segfault.
		//        the libusb docs forbid to free an active transfer.

		if (rc < 0) {
			log_warn("Could not cancel pending %s transfer %p for %s: %s (%d)",
			         usb_transfer_get_type_name(usb_transfer->type, 0), usb_transfer,
			         usb_transfer->usb_stack->base.name, usb_get_error_name(rc), rc);
		} else {
			tv.tv_sec = 0;
			tv.tv_usec = 0;

			start = time(NULL);
			now = start;

			// FIXME: don't wait 1sec per transfer
			while (!usb_transfer->completed && now >= start && now < start + 1) {
				rc = libusb_handle_events_timeout(usb_transfer->usb_stack->context, &tv);

				if (rc < 0) {
					log_error("Could not handle USB events: %s (%d)",
					          usb_get_error_name(rc), rc);
				}

				now = time(NULL);
			}

			if (!usb_transfer->completed) {
				log_warn("Attempt to cancel pending %s transfer %p for %s timed out",
				         usb_transfer_get_type_name(usb_transfer->type, 0), usb_transfer,
				         usb_transfer->usb_stack->base.name);
			}
		}
	}

	if (!usb_transfer->submitted) {
		libusb_free_transfer(usb_transfer->handle);
	} else {
		log_warn("Leaking pending %s transfer %p for %s",
		         usb_transfer_get_type_name(usb_transfer->type, 0), usb_transfer,
		         usb_transfer->usb_stack->base.name);
	}
}

int usb_transfer_submit(USBTransfer *usb_transfer) {
	uint8_t end_point;
	int length;
	int rc;

	if (usb_transfer->submitted) {
		log_error("%s transfer %p is already submitted for %s",
		          usb_transfer_get_type_name(usb_transfer->type, 1), usb_transfer,
		          usb_transfer->usb_stack->base.name);

		return -1;
	}

	switch (usb_transfer->type) {
	case USB_TRANSFER_TYPE_READ:
		end_point = LIBUSB_ENDPOINT_IN + USB_BRICK_ENDPOINT_IN;
		length = sizeof(Packet);

		break;

	case USB_TRANSFER_TYPE_WRITE:
		end_point = LIBUSB_ENDPOINT_OUT + USB_BRICK_ENDPOINT_OUT;
		length = usb_transfer->packet.header.length;

		break;

	default:
		log_error("Transfer for %s has invalid type",
		          usb_transfer->usb_stack->base.name);

		return -1;
	}

	usb_transfer->submitted = 1;

	libusb_fill_bulk_transfer(usb_transfer->handle,
	                          usb_transfer->usb_stack->device_handle,
	                          end_point,
	                          (unsigned char *)&usb_transfer->packet,
	                          length,
	                          usb_transfer_wrapper,
	                          usb_transfer,
	                          0);

	rc = libusb_submit_transfer(usb_transfer->handle);

	if (rc < 0) {
		log_error("Could not submit %s transfer %p to %s: %s (%d)",
		          usb_transfer_get_type_name(usb_transfer->type, 0), usb_transfer,
		          usb_transfer->usb_stack->base.name, usb_get_error_name(rc), rc);

		usb_transfer->submitted = 0;

		return -1;
	}

	log_debug("Submitted %s transfer %p for %u bytes to %s",
	          usb_transfer_get_type_name(usb_transfer->type, 0), usb_transfer, length,
	          usb_transfer->usb_stack->base.name);

	return 0;
}
