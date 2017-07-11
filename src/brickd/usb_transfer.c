/*
 * brickd
 * Copyright (C) 2012-2014, 2016-2017 Matthias Bolte <matthias@tinkerforge.com>
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
#include <time.h>

#include <daemonlib/log.h>

#include "usb_transfer.h"

#include "stack.h"
#include "usb.h"

static LogSource _log_source = LOG_SOURCE_INITIALIZER;

static const char *usb_transfer_get_type_name(USBTransferType type, bool upper) {
	switch (type) {
	case USB_TRANSFER_TYPE_READ:  return upper ? "Read" : "read";
	case USB_TRANSFER_TYPE_WRITE: return upper ? "Write" : "write";

	default:                      return upper ? "<Unknown>" : "<unknown>";
	}
}

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
		log_error("%s transfer %p (%p) returned from %s, but was not submitted before",
		          usb_transfer_get_type_name(usb_transfer->type, true),
		          usb_transfer, handle, usb_transfer->usb_stack->base.name);

		return;
	}

	usb_transfer->submitted = false;

	if (handle->status == LIBUSB_TRANSFER_CANCELLED) {
		log_debug("%s transfer %p (%p) for %s was cancelled%s",
		          usb_transfer_get_type_name(usb_transfer->type, true),
		          usb_transfer, handle, usb_transfer->usb_stack->base.name,
		          !usb_transfer->usb_stack->expecting_disconnect
		          ? ", marking device as about to be removed"
		          : "");

		usb_transfer->usb_stack->expecting_disconnect = true;

		return;
	} else if (handle->status == LIBUSB_TRANSFER_NO_DEVICE) {
		usb_transfer->usb_stack->expecting_disconnect = true;

		log_debug("%s transfer %p (%p) for %s was aborted, device got disconnected",
		          usb_transfer_get_type_name(usb_transfer->type, true),
		          usb_transfer, handle, usb_transfer->usb_stack->base.name);

		return;
	} else if (handle->status == LIBUSB_TRANSFER_STALL) {
		// unplugging a RED Brick from Windows results in a stalled transfer
		// followed by read transfers returning garbage data and transfer
		// submission errors. all this happens before the unplug event for
		// the device is received. avoid logging pointless error messages
		// about garbage data and transfer submission errors by detecting this
		// condition here and deactivating the device
		if (usb_transfer->usb_stack->expecting_read_stall_before_removal &&
		    usb_transfer->type == USB_TRANSFER_TYPE_READ) {
			usb_transfer->usb_stack->expecting_read_stall_before_removal = false;
			usb_transfer->usb_stack->expecting_disconnect = true;

			log_debug("%s transfer %p (%p) for %s got stalled as expected before device removal, marking device as about to be removed",
			          usb_transfer_get_type_name(usb_transfer->type, true),
			          usb_transfer, handle, usb_transfer->usb_stack->base.name);
		} else {
			log_warn("%s transfer %p (%p) for %s got stalled",
			         usb_transfer_get_type_name(usb_transfer->type, true),
			         usb_transfer, handle, usb_transfer->usb_stack->base.name);

			// FIXME: maybe use libusb_clear_halt to clear halt condition?
		}

		return;
	} else if (handle->status != LIBUSB_TRANSFER_COMPLETED) {
		log_warn("%s transfer %p (%p) returned with an error from %s: %s (%d)",
		         usb_transfer_get_type_name(usb_transfer->type, true), usb_transfer,
		         handle, usb_transfer->usb_stack->base.name,
		         usb_transfer_get_status_name(handle->status), handle->status);
	} else {
		log_packet_debug("%s transfer %p (%p) returned successfully from %s%s",
		                 usb_transfer_get_type_name(usb_transfer->type, true),
		                 usb_transfer, handle, usb_transfer->usb_stack->base.name,
		                 usb_transfer->cancelled
		                 ? ", but it was cancelled in the meantime"
		                 : (usb_transfer->usb_stack->expecting_disconnect
		                    ? ", but the corresponding USB device is about to be removed"
		                    : ""));

		if (usb_transfer->cancelled ||
		    usb_transfer->usb_stack->expecting_disconnect) {
			return;
		}

		if (usb_transfer->function != NULL) {
			usb_transfer->function(usb_transfer);
		}
	}

	if (usb_transfer->type == USB_TRANSFER_TYPE_READ &&
	    !usb_transfer->cancelled &&
	    !usb_transfer->usb_stack->expecting_disconnect) {
		usb_transfer_submit(usb_transfer);
	}
}

int usb_transfer_create(USBTransfer *usb_transfer, USBStack *usb_stack,
                        USBTransferType type, USBTransferFunction function) {
	usb_transfer->usb_stack = usb_stack;
	usb_transfer->type = type;
	usb_transfer->submitted = false;
	usb_transfer->cancelled = false;
	usb_transfer->function = function;
	usb_transfer->handle = libusb_alloc_transfer(0);

	if (usb_transfer->handle == NULL) {
		log_error("Could not allocate libusb %s transfer for %s",
		          usb_transfer_get_type_name(usb_transfer->type, false),
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

	log_debug("Destroying %s transfer %p (%p) for %s",
	          usb_transfer_get_type_name(usb_transfer->type, false), usb_transfer,
	          usb_transfer->handle, usb_transfer->usb_stack->base.name);

	if (usb_transfer->submitted) {
		usb_transfer->cancelled = true;

		rc = libusb_cancel_transfer(usb_transfer->handle);

		// if libusb_cancel_transfer fails with LIBUSB_ERROR_NO_DEVICE if the
		// device was disconnected before the transfer could be cancelled. but
		// the transfer might be cancelled anyway and we need to wait for the
		// transfer to complete. this can result in waiting for a transfer that
		// might not complete anymore. but if we don't wait for the transfer to
		// complete if it actually will complete then the libusb_device_handle
		// might be closed before the transfer completes. this results in a
		// crash by NULL pointer dereference because libusb assumes that the
		// libusb_device_handle is not closed as long as there are submitted
		// transfers.
		if (rc < 0 && rc != LIBUSB_ERROR_NO_DEVICE) {
			log_warn("Could not cancel pending %s transfer %p (%p) for %s: %s (%d)",
			         usb_transfer_get_type_name(usb_transfer->type, false), usb_transfer,
			         usb_transfer->handle, usb_transfer->usb_stack->base.name,
			         usb_get_error_name(rc), rc);
		} else {
			tv.tv_sec = 0;
			tv.tv_usec = 0;

			start = time(NULL);
			now = start;

			// FIXME: don't wait 1 second per transfer
			while (usb_transfer->submitted && now >= start && now < start + 1) {
				rc = libusb_handle_events_timeout(usb_transfer->usb_stack->context, &tv);

				if (rc < 0) {
					log_error("Could not handle USB events during %s transfer %p (%p) cancellation for %s: %s (%d)",
					          usb_transfer_get_type_name(usb_transfer->type, false),
					          usb_transfer, usb_transfer->handle,
					          usb_transfer->usb_stack->base.name,
					          usb_get_error_name(rc), rc);
				}

				now = time(NULL);
			}

			if (usb_transfer->submitted) {
				log_warn("Attempt to cancel pending %s transfer %p (%p) for %s timed out",
				         usb_transfer_get_type_name(usb_transfer->type, false), usb_transfer,
				         usb_transfer->handle, usb_transfer->usb_stack->base.name);
			}
		}
	}

	if (!usb_transfer->submitted) {
		libusb_free_transfer(usb_transfer->handle);
	} else {
		log_warn("Leaking pending %s transfer %p (%p) for %s",
		         usb_transfer_get_type_name(usb_transfer->type, false), usb_transfer,
		         usb_transfer->handle, usb_transfer->usb_stack->base.name);
	}
}

int usb_transfer_submit(USBTransfer *usb_transfer) {
	uint8_t endpoint;
	int length;
	int rc;

	if (usb_transfer->submitted) {
		log_error("%s transfer %p (%p) is already submitted for %s",
		          usb_transfer_get_type_name(usb_transfer->type, true), usb_transfer,
		          usb_transfer->handle, usb_transfer->usb_stack->base.name);

		return -1;
	}

	switch (usb_transfer->type) {
	case USB_TRANSFER_TYPE_READ:
		endpoint = usb_transfer->usb_stack->endpoint_in;
		length = sizeof(Packet);

		break;

	case USB_TRANSFER_TYPE_WRITE:
		endpoint = usb_transfer->usb_stack->endpoint_out;
		length = usb_transfer->packet.header.length;

		break;

	default:
		log_error("Transfer for %s has invalid type",
		          usb_transfer->usb_stack->base.name);

		return -1;
	}

	usb_transfer->submitted = true;

	libusb_fill_bulk_transfer(usb_transfer->handle,
	                          usb_transfer->usb_stack->device_handle,
	                          endpoint,
	                          (unsigned char *)&usb_transfer->packet,
	                          length,
	                          usb_transfer_wrapper,
	                          usb_transfer,
	                          0);

	rc = libusb_submit_transfer(usb_transfer->handle);

	if (rc < 0) {
		log_error("Could not submit %s transfer %p (%p) to %s: %s (%d)",
		          usb_transfer_get_type_name(usb_transfer->type, false), usb_transfer,
		          usb_transfer->handle, usb_transfer->usb_stack->base.name,
		          usb_get_error_name(rc), rc);

		usb_transfer->submitted = false;

		return -1;
	}

	log_packet_debug("Submitted %s transfer %p (%p) for %u bytes to %s",
	                 usb_transfer_get_type_name(usb_transfer->type, false),
	                 usb_transfer, usb_transfer->handle, length,
	                 usb_transfer->usb_stack->base.name);

	return 0;
}
