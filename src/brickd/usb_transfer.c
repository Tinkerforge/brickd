/*
 * brickd
 * Copyright (C) 2012-2021 Matthias Bolte <matthias@tinkerforge.com>
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

#include <stdlib.h>
#include <libusb.h>

#include <daemonlib/log.h>
#include <daemonlib/packet.h>

#include "usb_transfer.h"

#include "stack.h"
#include "usb.h"

static LogSource _log_source = LOG_SOURCE_INITIALIZER;

static uint32_t _next_submission = 1;

#define MAX_BUFFER_LENGTH 1024

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

void usb_transfer_finish(struct libusb_transfer *handle) {
	USBTransfer *usb_transfer = handle->user_data;

	if (usb_transfer == NULL) {
		log_warn("Abandoned USB transfer (handle: %p) finished: %s (%d)",
		         handle, usb_transfer_get_status_name(handle->status), handle->status);

		free(handle->buffer);
		libusb_free_transfer(handle);

		return;
	}

	if (!usb_transfer->submitted) {
		log_error("%s transfer %p (handle: %p, submission: %u) returned from %s, but was not submitted before",
		          usb_transfer_get_type_name(usb_transfer->type, true),
		          usb_transfer, handle, usb_transfer->submission,
		          usb_transfer->usb_stack->base.name);

		return;
	}

	usb_transfer->submitted = false;
	--usb_transfer->usb_stack->pending_transfers;

	if (handle->status == LIBUSB_TRANSFER_CANCELLED) {
		log_debug("%s transfer %p (handle: %p, submission: %u) for %s was cancelled%s",
		          usb_transfer_get_type_name(usb_transfer->type, true),
		          usb_transfer, handle, usb_transfer->submission,
		          usb_transfer->usb_stack->base.name,
		          !usb_transfer->usb_stack->expecting_removal
		          ? ", marking device as about to be removed"
		          : "");

		usb_transfer->usb_stack->expecting_removal = true;

		return;
	} else if (handle->status == LIBUSB_TRANSFER_NO_DEVICE) {
		log_debug("%s transfer %p (handle: %p, submission: %u) for %s was aborted, device got removed",
		          usb_transfer_get_type_name(usb_transfer->type, true),
		          usb_transfer, handle, usb_transfer->submission,
		          usb_transfer->usb_stack->base.name);

		usb_transfer->usb_stack->expecting_removal = true;

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
			usb_transfer->usb_stack->expecting_removal = true;

			log_debug("%s transfer %p (handle: %p, submission: %u) for %s aborted by stall condition as expected before device removal",
			          usb_transfer_get_type_name(usb_transfer->type, true),
			          usb_transfer, handle, usb_transfer->submission,
			          usb_transfer->usb_stack->base.name);
		} else {
			log_debug("%s transfer %p (handle: %p, submission: %u) for %s aborted by stall condition",
			          usb_transfer_get_type_name(usb_transfer->type, true),
			          usb_transfer, handle, usb_transfer->submission,
			          usb_transfer->usb_stack->base.name);

			usb_transfer->pending_error = USB_TRANSFER_PENDING_ERROR_STALL;

			// in most cases a transfer will stall as a result of unplugging the
			// USB device. use a 1 second timer to delay the recovery process to
			// avoid trying to access an already unplugged USB device.
			usb_stack_start_pending_error_timer(usb_transfer->usb_stack);
		}

		return;
	} else if (handle->status == LIBUSB_TRANSFER_ERROR) {
		log_debug("%s transfer %p (handle: %p, submission: %u) returned with an unspecified error from %s",
		          usb_transfer_get_type_name(usb_transfer->type, true), usb_transfer,
		          handle, usb_transfer->submission, usb_transfer->usb_stack->base.name);

		usb_transfer->pending_error = USB_TRANSFER_PENDING_ERROR_UNSPECIFIED;

		// in some cases a transfer will fail as a result of unplugging the
		// USB device. use a 1 second timer to delay the recovery process to
		// avoid trying to access an already unplugged USB device.
		usb_stack_start_pending_error_timer(usb_transfer->usb_stack);

		return;
	} else if (handle->status != LIBUSB_TRANSFER_COMPLETED) {
		log_warn("%s transfer %p (handle: %p, submission: %u) returned with an error from %s: %s (%d)",
		         usb_transfer_get_type_name(usb_transfer->type, true), usb_transfer,
		         handle, usb_transfer->submission, usb_transfer->usb_stack->base.name,
		         usb_transfer_get_status_name(handle->status), handle->status);
	} else {
		log_packet_debug("%s transfer %p (handle: %p, submission: %u) returned successfully from %s%s",
		                 usb_transfer_get_type_name(usb_transfer->type, true),
		                 usb_transfer, handle, usb_transfer->submission,
		                 usb_transfer->usb_stack->base.name,
		                 usb_transfer->cancelled
		                 ? ", but it was cancelled in the meantime"
		                 : (usb_transfer->usb_stack->expecting_removal
		                    ? ", but the corresponding USB device is about to be removed"
		                    : ""));

		if (usb_transfer->cancelled || usb_transfer->usb_stack->expecting_removal) {
			return;
		}

		if (usb_transfer->function != NULL) {
			usb_transfer->function(usb_transfer);
		}
	}

	usb_transfer->submission = 0;

	if (usb_transfer->type == USB_TRANSFER_TYPE_READ &&
	    usb_transfer_is_submittable(usb_transfer)) {
		usb_transfer_submit(usb_transfer);
	}
}

#if defined _WIN32 && !defined BRICKD_UWP_BUILD

extern void LIBUSB_CALL usb_transfer_callback(struct libusb_transfer *handle);

#else

static void LIBUSB_CALL usb_transfer_callback(struct libusb_transfer *handle) {
	usb_transfer_finish(handle);
}

#endif

int usb_transfer_create(USBTransfer *usb_transfer, USBStack *usb_stack,
                        USBTransferType type, USBTransferFunction function) {
	struct libusb_transfer *handle;
	uint8_t *buffer;

	handle = libusb_alloc_transfer(0);

	if (handle == NULL) {
		log_error("Could not allocate libusb %s transfer for %s",
		          usb_transfer_get_type_name(type, false), usb_stack->base.name);

		return -1;
	}

	buffer = malloc(MAX_BUFFER_LENGTH);

	if (buffer == NULL) {
		log_error("Could not allocate buffer for %s transfer for %s",
		          usb_transfer_get_type_name(type, false), usb_stack->base.name);

		libusb_free_transfer(handle);

		return -1;
	}

	usb_transfer->usb_stack = usb_stack;
	usb_transfer->type = type;
	usb_transfer->submitted = false;
	usb_transfer->cancelled = false;
	usb_transfer->function = function;
	usb_transfer->handle = handle;
	usb_transfer->buffer = buffer;
	usb_transfer->submission = 0;
	usb_transfer->pending_error = USB_TRANSFER_PENDING_ERROR_NONE;

	return 0;
}

void usb_transfer_destroy(USBTransfer *usb_transfer) {
	log_debug("Destroying %s%s transfer %p (handle: %p, submission: %u, cancelled: %d) for %s",
	          usb_transfer->submitted ? "pending ": "",
	          usb_transfer_get_type_name(usb_transfer->type, false), usb_transfer,
	          usb_transfer->handle, usb_transfer->submission, usb_transfer->cancelled ? 1 : 0,
	          usb_transfer->usb_stack->base.name);

	if (usb_transfer->submitted && !usb_transfer->cancelled) {
		usb_transfer_cancel(usb_transfer);
	}

	if (!usb_transfer->submitted) {
		free(usb_transfer->buffer);
		libusb_free_transfer(usb_transfer->handle);
	} else {
		log_warn("Abandoning pending %s transfer %p (handle: %p, submission: %u) for %s",
		         usb_transfer_get_type_name(usb_transfer->type, false), usb_transfer,
		         usb_transfer->handle, usb_transfer->submission, usb_transfer->usb_stack->base.name);

		usb_transfer->handle->user_data = NULL;
		usb_transfer->handle = NULL;
	}
}

bool usb_transfer_is_submittable(USBTransfer *usb_transfer) {
	return !usb_transfer->submitted &&
	       !usb_transfer->cancelled &&
	       usb_transfer->pending_error == USB_TRANSFER_PENDING_ERROR_NONE &&
	       !usb_transfer->usb_stack->expecting_removal;
}

int usb_transfer_submit(USBTransfer *usb_transfer) {
	uint8_t endpoint;
	int length;
	int rc;

	if (usb_transfer->submitted) {
		log_error("%s transfer %p (handle: %p, submission: %u) for %s is already submitted",
		          usb_transfer_get_type_name(usb_transfer->type, true), usb_transfer,
		          usb_transfer->handle, usb_transfer->submission, usb_transfer->usb_stack->base.name);

		return -1;
	}

	if (usb_transfer->cancelled) {
		log_error("%s transfer %p (handle: %p, submission: %u) for %s is already cancelled",
		          usb_transfer_get_type_name(usb_transfer->type, true), usb_transfer,
		          usb_transfer->handle, usb_transfer->submission, usb_transfer->usb_stack->base.name);

		return -1;
	}

	if (usb_transfer->pending_error != USB_TRANSFER_PENDING_ERROR_NONE) {
		log_error("%s transfer %p (handle: %p, submission: %u) for %s has a pending error",
		          usb_transfer_get_type_name(usb_transfer->type, true), usb_transfer,
		          usb_transfer->handle, usb_transfer->submission, usb_transfer->usb_stack->base.name);

		return -1;
	}

	if (usb_transfer->usb_stack->expecting_removal) {
		log_error("%s transfer %p (handle: %p, submission: %u) for %s that is about to be removed",
		          usb_transfer_get_type_name(usb_transfer->type, true), usb_transfer,
		          usb_transfer->handle, usb_transfer->submission, usb_transfer->usb_stack->base.name);

		return -1;
	}

	switch (usb_transfer->type) {
	case USB_TRANSFER_TYPE_READ:
		endpoint = usb_transfer->usb_stack->endpoint_in;
		length = MAX_BUFFER_LENGTH;
		break;

	case USB_TRANSFER_TYPE_WRITE:
		endpoint = usb_transfer->usb_stack->endpoint_out;
		length = ((Packet *)usb_transfer->buffer)->header.length;
		break;

	default:
		log_error("Transfer for %s has invalid type",
		          usb_transfer->usb_stack->base.name);

		return -1;
	}

	usb_transfer->submitted = true;
	usb_transfer->submission = _next_submission++;

	if (_next_submission == 0) {
		_next_submission = 1;
	}

	libusb_fill_bulk_transfer(usb_transfer->handle,
	                          usb_transfer->usb_stack->device_handle,
	                          endpoint,
	                          usb_transfer->buffer,
	                          length,
	                          usb_transfer_callback,
	                          usb_transfer,
	                          0);

	rc = libusb_submit_transfer(usb_transfer->handle);

	if (rc < 0) {
		log_error("Could not submit %s transfer %p (handle: %p, submission: %u) to %s: %s (%d)",
		          usb_transfer_get_type_name(usb_transfer->type, false), usb_transfer,
		          usb_transfer->handle, usb_transfer->submission,
		          usb_transfer->usb_stack->base.name, usb_get_error_name(rc), rc);

		usb_transfer->submitted = false;

		return -1;
	}

	++usb_transfer->usb_stack->pending_transfers;

	log_packet_debug("Submitted %s transfer %p (handle: %p, submission: %u) for %u bytes to %s",
	                 usb_transfer_get_type_name(usb_transfer->type, false),
	                 usb_transfer, usb_transfer->handle, usb_transfer->submission,
	                 length, usb_transfer->usb_stack->base.name);

	return 0;
}

void usb_transfer_cancel(USBTransfer *usb_transfer) {
	int rc;

	if (!usb_transfer->submitted) {
		log_error("Trying to cancel %s transfer %p (handle: %p) for %s that was not submitted before",
		          usb_transfer_get_type_name(usb_transfer->type, false),
		          usb_transfer, usb_transfer->handle, usb_transfer->usb_stack->base.name);

		return;
	}

	if (usb_transfer->cancelled) {
		log_error("Trying to cancel %s transfer %p (handle: %p) for %s that was already cancelled",
		          usb_transfer_get_type_name(usb_transfer->type, false),
		          usb_transfer, usb_transfer->handle, usb_transfer->usb_stack->base.name);

		return;
	}

	usb_transfer->cancelled = true;

	// if the device got unplugged and this transfer is being cancelled because
	// of that then this transfer might just have finished as a result of the
	// device being unplugged, but the transfer callback might not have been
	// fully executed yet. especially on Windows with its asynchronous libusb
	// event handling performed in an extra thread. to minimize the duration of
	// the race condition window handle USB events again to make sure that the
	// transfer callback has had a chance to be fully executed and mark this
	// transfer as finished.
	usb_handle_events();

	if (!usb_transfer->submitted) {
		return;
	}

	log_debug("Cancelling pending %s transfer %p (handle: %p, submission: %u) for %s",
	          usb_transfer_get_type_name(usb_transfer->type, false), usb_transfer,
	          usb_transfer->handle, usb_transfer->submission,
	          usb_transfer->usb_stack->base.name);

	// cancellation might fail on Windows because of the asynchronous libusb
	// event handling performed in an extra thread. it can happen that the
	// transfer is actually not submitted anymore but the transfer callback has
	// not been fully executed yet. therefore, this USBTransfer might still have
	// its submitted flag set. in this case cancellation wasn't necessary as the
	// transfer was already finished. but distinguishing this situation from a
	// real error is difficult. therefore, all errors are reported here.
	rc = libusb_cancel_transfer(usb_transfer->handle);

	if (rc < 0) {
		log_warn("Could not cancel pending %s transfer %p (handle: %p, submission: %u) for %s: %s (%d)",
		         usb_transfer_get_type_name(usb_transfer->type, false), usb_transfer,
		         usb_transfer->handle, usb_transfer->submission,
		         usb_transfer->usb_stack->base.name, usb_get_error_name(rc), rc);
	}

	// give cancellation a chance to finish now, regardless of the cancellation
	// seeming successful or not
	usb_handle_events();
}

void usb_transfer_clear_pending_error(USBTransfer *usb_transfer) {
	if (usb_transfer->pending_error == USB_TRANSFER_PENDING_ERROR_STALL) {
		log_warn("%s transfer %p (handle: %p, submission: %u) for %s aborted by stall condition",
		         usb_transfer_get_type_name(usb_transfer->type, true),
		         usb_transfer, usb_transfer->handle, usb_transfer->submission,
		         usb_transfer->usb_stack->base.name);
	} else if (usb_transfer->pending_error == USB_TRANSFER_PENDING_ERROR_UNSPECIFIED) {
		log_warn("%s transfer %p (handle: %p, submission: %u) returned with an unspecified error from %s",
		         usb_transfer_get_type_name(usb_transfer->type, true), usb_transfer,
		         usb_transfer->handle, usb_transfer->submission, usb_transfer->usb_stack->base.name);
	}

	usb_transfer->submission = 0;
	usb_transfer->pending_error = USB_TRANSFER_PENDING_ERROR_NONE;
}
