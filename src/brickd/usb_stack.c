/*
 * brickd
 * Copyright (C) 2012-2021 Matthias Bolte <matthias@tinkerforge.com>
 * Copyright (C) 2014 Olaf Lüke <olaf@tinkerforge.com>
 *
 * usb_stack.c: USB stack specific functions
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

/*
 * this is a specific implementation of the generic Stack type for USB. it
 * handles USB device lookup based on bus number and device address and takes
 * care of sending and receiving packets over USB.
 */

#include <errno.h>
#include <string.h>

#include <daemonlib/array.h>
#include <daemonlib/log.h>
#include <daemonlib/utils.h>

#include "usb_stack.h"

#include "hardware.h"
#include "network.h"
#include "usb.h"
#include "usb_transfer.h"

static LogSource _log_source = LOG_SOURCE_INITIALIZER;

#define MAX_READ_TRANSFERS 10
#define MAX_WRITE_TRANSFERS 10
#define MAX_QUEUED_WRITES 32768
#define PENDING_ERROR_TIMER_DELAY 1000000 // 1 second in microseconds

static void usb_stack_handle_pending_error(void *opaque) {
	USBStack *usb_stack = opaque;
	int i;
	USBTransfer *usb_transfer;
	bool read_stall = false;
	bool write_stall = false;
	int rc;

	if (usb_stack->expecting_removal) {
		return;
	}

	// check read transfers
	for (i = 0; i < usb_stack->read_transfers.count; ++i) {
		usb_transfer = array_get(&usb_stack->read_transfers, i);

		if (usb_transfer->pending_error == USB_TRANSFER_PENDING_ERROR_STALL) {
			read_stall = true;
		}

		usb_transfer_clear_pending_error(usb_transfer);
	}

	// check write transfers
	for (i = 0; i < usb_stack->write_transfers.count; ++i) {
		usb_transfer = array_get(&usb_stack->write_transfers, i);

		if (usb_transfer->pending_error == USB_TRANSFER_PENDING_ERROR_STALL) {
			write_stall = true;
		}

		usb_transfer_clear_pending_error(usb_transfer);
	}

	// clear read endpoint stall
	if (read_stall) {
		rc = libusb_clear_halt(usb_stack->device_handle, usb_stack->endpoint_in);

		if (rc == LIBUSB_ERROR_NO_DEVICE) {
			log_debug("Not trying to clear read endpoint stall for %s, device got removed",
			          usb_stack->base.name);

			usb_stack->expecting_removal = true;
		} else if (rc < 0) {
			log_warn("Could not clear read endpoint stall for %s: %s (%d)",
			         usb_stack->base.name, usb_get_error_name(rc), rc);

			goto reopen;
		}

		log_info("Cleared read endpoint stall for %s", usb_stack->base.name);
	}

	// clear write endpoint stall
	if (write_stall) {
		rc = libusb_clear_halt(usb_stack->device_handle, usb_stack->endpoint_out);

		if (rc == LIBUSB_ERROR_NO_DEVICE) {
			log_debug("Not trying to clear write endpoint stall for %s, device got removed",
			          usb_stack->base.name);

			usb_stack->expecting_removal = true;
		} else if (rc < 0) {
			log_warn("Could not clear write endpoint stall for %s: %s (%d)",
			         usb_stack->base.name, usb_get_error_name(rc), rc);

			goto reopen;
		}

		log_info("Cleared write endpoint stall for %s", usb_stack->base.name);
	}

	// submit stalled read transfers
	for (i = 0; i < usb_stack->read_transfers.count; ++i) {
		usb_transfer = array_get(&usb_stack->read_transfers, i);

		if (usb_transfer_is_submittable(usb_transfer)) {
			usb_transfer_submit(usb_transfer);
		}
	}

	return;

reopen:
	log_warn("Reopening %s to recover from stalled transfer(s)", usb_stack->base.name);

	usb_reopen(usb_stack);
}

static void usb_stack_read_callback(USBTransfer *usb_transfer) {
	const char *message = NULL;
	char packet_dump[PACKET_MAX_DUMP_LENGTH];
	char packet_signature[PACKET_MAX_SIGNATURE_LENGTH];
	int packet_buffer_used = usb_transfer->handle->actual_length;

	// check if packet is too short
	if (packet_buffer_used < (int)sizeof(PacketHeader)) {
		// there is a problem with the first USB transfer send by the RED
		// Brick. if the first USB transfer was queued to the A10s USB hardware
		// before the USB OTG connection got established then the payload of
		// the USB transfer is mangled. the first 12 bytes are overwritten with
		// a fixed pattern that starts with 0xA1/0xAA. to deal with this problem
		// the RED Brick sends a USB transfer with one byte payload before
		// sending anything else. this short response with 0xA1/0xAA as payload
		// is detected here and dropped
		if (usb_transfer->usb_stack->expecting_short_Ax_response &&
		    packet_buffer_used == 1 &&
		    (usb_transfer->packet_buffer[0] == 0xA1 ||
		     usb_transfer->packet_buffer[0] == 0xAA)) {
			usb_transfer->usb_stack->expecting_short_Ax_response = false;

			log_debug("Read transfer %p returned expected short 0x%02X response from %s, dropping response",
			          usb_transfer, usb_transfer->packet_buffer[0],
			          usb_transfer->usb_stack->base.name);
		} else {
			log_error("Read transfer %p returned response (packet: %s) with incomplete header (actual: %u < minimum: %d) from %s",
			          usb_transfer,
			          packet_get_dump(packet_dump, &usb_transfer->packet, packet_buffer_used),
			          packet_buffer_used,
			          (int)sizeof(PacketHeader),
			          usb_transfer->usb_stack->base.name);
		}

		return;
	}

	// only the first response from the RED Brick is expected to be a short
	// 0xA1/0xAA response. after the first non-short response arrived stop
	// expecting a short response
	usb_transfer->usb_stack->expecting_short_Ax_response = false;

	while (packet_buffer_used > 0) {
		// check if packet is too short
		if (packet_buffer_used < (int)sizeof(PacketHeader)) {
			log_error("Read transfer %p returned response (packet: %s) with incomplete header (actual: %u < minimum: %d) from %s",
			          usb_transfer,
			          packet_get_dump(packet_dump, &usb_transfer->packet, packet_buffer_used),
			          packet_buffer_used,
			          (int)sizeof(PacketHeader),
			          usb_transfer->usb_stack->base.name);

			return;
		}

		// check if packet is a valid response
		if (!packet_header_is_valid_response(&usb_transfer->packet.header, &message)) {
			log_error("Received invalid response (packet: %s) from %s: %s",
			          packet_get_dump(packet_dump, &usb_transfer->packet, packet_buffer_used),
			          usb_transfer->usb_stack->base.name,
			          message);

			return;
		}

		// check if packet is complete
		if (packet_buffer_used < usb_transfer->packet.header.length) {
			log_error("Read transfer %p returned incomplete response (packet: %s, actual: %u != expected: %u) from %s",
			          usb_transfer,
			          packet_get_dump(packet_dump, &usb_transfer->packet, packet_buffer_used),
			          packet_buffer_used,
			          usb_transfer->packet.header.length,
			          usb_transfer->usb_stack->base.name);

			return;
		}

		log_packet_debug("Received %s (%s) from %s",
		                 packet_get_response_type(&usb_transfer->packet),
		                 packet_get_response_signature(packet_signature, &usb_transfer->packet),
		                 usb_transfer->usb_stack->base.name);

#ifdef DAEMONLIB_WITH_PACKET_TRACE
		usb_transfer->packet.trace_id = packet_get_next_response_trace_id();
#endif

		packet_add_trace(&usb_transfer->packet);

		if (stack_add_recipient(&usb_transfer->usb_stack->base,
		                        usb_transfer->packet.header.uid, 0) < 0) {
			return;
		}

		network_dispatch_response(&usb_transfer->packet);

		memmove(usb_transfer->packet_buffer,
		        usb_transfer->packet_buffer + usb_transfer->packet.header.length,
		        packet_buffer_used - usb_transfer->packet.header.length);

		packet_buffer_used -= usb_transfer->packet.header.length;
	}
}

static void usb_stack_write_callback(USBTransfer *usb_transfer) {
	Packet *request;
	char packet_signature[PACKET_MAX_SIGNATURE_LENGTH];

	if (!usb_transfer->usb_stack->expecting_removal &&
	    usb_transfer->usb_stack->write_queue.count > 0) {
		request = queue_peek(&usb_transfer->usb_stack->write_queue);

		memcpy(&usb_transfer->packet, request, request->header.length);

		if (usb_transfer_submit(usb_transfer) < 0) {
			log_error("Could not send queued request (%s) to %s: %s (%d)",
			          packet_get_request_signature(packet_signature, &usb_transfer->packet),
			          usb_transfer->usb_stack->base.name,
			          get_errno_name(errno), errno);

			return;
		}

		queue_pop(&usb_transfer->usb_stack->write_queue, NULL);

		log_packet_debug("Sent queued request (%s) to %s, %d request(s) left in write queue",
		                 packet_get_request_signature(packet_signature, &usb_transfer->packet),
		                 usb_transfer->usb_stack->base.name,
		                 usb_transfer->usb_stack->write_queue.count);
	}
}

static int usb_stack_dispatch_request(Stack *stack, Packet *request,
                                      Recipient *recipient) {
	USBStack *usb_stack = (USBStack *)stack;
	int i;
	USBTransfer *usb_transfer;
	Packet *queued_request;
	char packet_signature[PACKET_MAX_SIGNATURE_LENGTH];
	uint32_t requests_to_drop;

	(void)recipient;

	if (usb_stack->expecting_removal) {
		log_debug("Cannot dispatch request (%s) to %s that is about to be removed, dropping request",
		          packet_get_request_signature(packet_signature, request),
		          usb_stack->base.name);

		return 0;
	}

	// find free write transfer
	for (i = 0; i < usb_stack->write_transfers.count; ++i) {
		usb_transfer = array_get(&usb_stack->write_transfers, i);

		if (!usb_transfer_is_submittable(usb_transfer)) {
			continue;
		}

		memcpy(&usb_transfer->packet, request, request->header.length);

		if (usb_transfer_submit(usb_transfer) < 0) {
			// FIXME: how to handle a failed submission, try to re-submit?

			continue;
		}

		return 0;
	}

	// no free write transfer available, push request to write queue
	log_packet_debug("Could not find a free write transfer for %s, pushing request to write queue (count: %d + 1)",
	                 usb_stack->base.name, usb_stack->write_queue.count);

	if (usb_stack->write_queue.count >= MAX_QUEUED_WRITES) {
		requests_to_drop = usb_stack->write_queue.count - MAX_QUEUED_WRITES + 1;

		log_warn("Write queue for %s is full, dropping %u queued request(s), %u + %u dropped in total",
		         usb_stack->base.name, requests_to_drop,
		         usb_stack->dropped_requests, requests_to_drop);

		usb_stack->dropped_requests += requests_to_drop;

		while (usb_stack->write_queue.count >= MAX_QUEUED_WRITES) {
			queue_pop(&usb_stack->write_queue, NULL);
		}
	}

	queued_request = queue_push(&usb_stack->write_queue);

	if (queued_request == NULL) {
		log_error("Could not push request (%s) to write queue for %s, dropping request: %s (%d)",
		          packet_get_request_signature(packet_signature, request),
		          usb_stack->base.name,
		          get_errno_name(errno), errno);

		return -1;
	}

	memcpy(queued_request, request, request->header.length);

	return 0;
}

int usb_stack_create(USBStack *usb_stack, libusb_context *context, libusb_device *device, bool red_brick) {
	int phase = 0;
	int rc;
	int i = 0;
	char preliminary_name[STACK_MAX_NAME_LENGTH];
	int retries = 0;
	USBTransfer *usb_transfer;

	usb_stack->bus_number = libusb_get_bus_number(device);
	usb_stack->device_address = libusb_get_device_address(device);
	usb_stack->context = context;
	usb_stack->device = libusb_ref_device(device);

	log_debug("Acquiring USB device (bus: %u, device: %u)",
	          usb_stack->bus_number, usb_stack->device_address);

	phase = 1;

	usb_stack->device_handle = NULL;
	usb_stack->dropped_requests = 0;
	usb_stack->connected = true;
	usb_stack->red_brick = red_brick;
	usb_stack->expecting_removal = false;

	if (red_brick) {
		usb_stack->interface_number = USB_RED_BRICK_INTERFACE;
		usb_stack->expecting_short_Ax_response = true;
#ifdef _WIN32
		usb_stack->expecting_read_stall_before_removal = true;
#else
		usb_stack->expecting_read_stall_before_removal = false;
#endif
	} else {
		usb_stack->interface_number = USB_BRICK_INTERFACE;
		usb_stack->expecting_short_Ax_response = false;
		usb_stack->expecting_read_stall_before_removal = false;
	}

	// create stack base
	snprintf(preliminary_name, sizeof(preliminary_name),
	         "USB device (bus: %u, device: %u)",
	         usb_stack->bus_number, usb_stack->device_address);

	if (stack_create(&usb_stack->base, preliminary_name,
	                 usb_stack_dispatch_request) < 0) {
		log_error("Could not create base stack for %s: %s (%d)",
		          preliminary_name, get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 2;

	// open device
	rc = libusb_open(device, &usb_stack->device_handle);

	if (rc < 0) {
		log_warn("Could not open %s: %s (%d)",
		         usb_stack->base.name, usb_get_error_name(rc), rc);

		goto cleanup;
	}

	phase = 3;

	log_debug("Found %s", usb_stack->base.name);

	// get interface endpoints
	rc = usb_get_interface_endpoints(usb_stack->device_handle, usb_stack->interface_number,
	                                 &usb_stack->endpoint_in, &usb_stack->endpoint_out);

	if (rc < 0) {
		log_error("Could not get interface endpoints of %s: %s (%d)",
		          usb_stack->base.name, usb_get_error_name(rc), rc);

		goto cleanup;
	}

	log_debug("Got interface endpoints (in: 0x%02X, out: 0x%02X) for %s",
	          usb_stack->endpoint_in, usb_stack->endpoint_out, usb_stack->base.name);

	// claim device interface
	log_debug("Trying to claim interface %d of %s",
	          usb_stack->interface_number, usb_stack->base.name);

	rc = libusb_claim_interface(usb_stack->device_handle, usb_stack->interface_number);

	if (rc < 0) {
		// on Linux claiming the interface might fail because the uevent for
		// a USB device arrival was received before the USB subsystem had time
		// to create the USBFS entry for the new device
		//
		// on macOS claiming the interface might fail because something is
		// opening the USB device on hotplug to probe it. libusb will still open
		// an already open USB device but in a limited mode that doesn't allow
		// to claim the interface
		//
		// retry to claim the interface but sleep in between tries so the USB
		// subsystem has time to create the USBFS entry for the new device on
		// Linux. also reopen the USB device on macOS to getter a proper device
		// handle that allows to claim the interface
		while (rc < 0 && retries < 10) {
#ifdef __APPLE__
			libusb_close(usb_stack->device_handle);
#endif

			millisleep(50);

#ifdef __APPLE__
			rc = libusb_open(usb_stack->device, &usb_stack->device_handle);

			if (rc < 0) {
				log_error("Could not reopen %s: %s (%d)",
				          usb_stack->base.name, usb_get_error_name(rc), rc);

				goto cleanup;
			}
#endif

			rc = libusb_claim_interface(usb_stack->device_handle, usb_stack->interface_number);

			++retries;
		}

		if (rc < 0) {
			log_error("Could not claim interface of %s after %d retry(s): %s (%d)",
			          usb_stack->base.name, retries, usb_get_error_name(rc), rc);

			goto cleanup;
		}

		log_debug("Claimed interface %d of %s after %d retry(s)",
		          usb_stack->interface_number, usb_stack->base.name, retries);
	} else {
		log_debug("Claimed interface %d of %s at first try",
		          usb_stack->interface_number, usb_stack->base.name);
	}

	phase = 4;

	// update stack name
	if (usb_get_device_name(usb_stack->device_handle, usb_stack->base.name,
	                        sizeof(usb_stack->base.name)) < 0) {
		goto cleanup;
	}

	log_debug("Got display name for %s: %s",
	          preliminary_name, usb_stack->base.name);

	// create pending error timer
	if (timer_create_(&usb_stack->pending_error_timer, usb_stack_handle_pending_error, usb_stack) < 0) {
		log_error("Could not create pending error timer for %s: %s (%d)",
		          usb_stack->base.name, get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 5;

	// allocate and submit read transfers
	if (array_create(&usb_stack->read_transfers, MAX_READ_TRANSFERS,
	                 sizeof(USBTransfer), true) < 0) {
		log_error("Could not create read transfer array for %s: %s (%d)",
		          usb_stack->base.name, get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 6;

	log_debug("Submitting read transfers to %s", usb_stack->base.name);

	for (i = 0; i < MAX_READ_TRANSFERS; ++i) {
		usb_transfer = array_append(&usb_stack->read_transfers);

		if (usb_transfer == NULL) {
			log_error("Could not append to read transfer array for %s: %s (%d)",
			          usb_stack->base.name, get_errno_name(errno), errno);

			goto cleanup;
		}

		if (usb_transfer_create(usb_transfer, usb_stack, USB_TRANSFER_TYPE_READ,
		                        usb_stack_read_callback) < 0) {
			array_remove(&usb_stack->read_transfers,
			             usb_stack->read_transfers.count -1, NULL);

			goto cleanup;
		}

		if (usb_transfer_submit(usb_transfer) < 0) {
			goto cleanup;
		}
	}

	// allocate write queue
	if (queue_create(&usb_stack->write_queue, sizeof(Packet)) < 0) {
		log_error("Could not create write queue for %s: %s (%d)",
		          usb_stack->base.name, get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 7;

	// allocate write transfers
	if (array_create(&usb_stack->write_transfers, MAX_WRITE_TRANSFERS,
	                 sizeof(USBTransfer), true) < 0) {
		log_error("Could not create write transfer array for %s: %s (%d)",
		          usb_stack->base.name, get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 8;

	for (i = 0; i < MAX_WRITE_TRANSFERS; ++i) {
		usb_transfer = array_append(&usb_stack->write_transfers);

		if (usb_transfer == NULL) {
			log_error("Could not append to write transfer array for %s: %s (%d)",
			          usb_stack->base.name, get_errno_name(errno), errno);

			goto cleanup;
		}

		if (usb_transfer_create(usb_transfer, usb_stack, USB_TRANSFER_TYPE_WRITE,
		                        usb_stack_write_callback) < 0) {
			array_remove(&usb_stack->write_transfers,
			             usb_stack->write_transfers.count -1, NULL);

			goto cleanup;
		}
	}

	// add to stacks array
	if (hardware_add_stack(&usb_stack->base) < 0) {
		goto cleanup;
	}

	phase = 9;

cleanup:
	switch (phase) { // no breaks, all cases fall through intentionally
	case 8:
		array_destroy(&usb_stack->write_transfers, (ItemDestroyFunction)usb_transfer_destroy);
		// fall through

	case 7:
		queue_destroy(&usb_stack->write_queue, NULL);
		// fall through

	case 6:
		array_destroy(&usb_stack->read_transfers, (ItemDestroyFunction)usb_transfer_destroy);
		// fall through

	case 5:
		timer_destroy(&usb_stack->pending_error_timer);
		// fall through

	case 4:
		libusb_release_interface(usb_stack->device_handle, usb_stack->interface_number);
		// fall through

	case 3:
		libusb_close(usb_stack->device_handle);
		// fall through

	case 2:
		stack_destroy(&usb_stack->base);
		// fall through

	case 1:
		libusb_unref_device(usb_stack->device);
		// fall through

	default:
		break;
	}

	return phase == 9 ? 0 : -1;
}

void usb_stack_destroy(USBStack *usb_stack) {
	char name[STACK_MAX_NAME_LENGTH];

	usb_stack->expecting_removal = true;

	hardware_remove_stack(&usb_stack->base);

	array_destroy(&usb_stack->read_transfers, (ItemDestroyFunction)usb_transfer_destroy);
	array_destroy(&usb_stack->write_transfers, (ItemDestroyFunction)usb_transfer_destroy);

	timer_destroy(&usb_stack->pending_error_timer);

	queue_destroy(&usb_stack->write_queue, NULL);

	libusb_release_interface(usb_stack->device_handle, usb_stack->interface_number);

	libusb_close(usb_stack->device_handle);

	string_copy(name, sizeof(name), usb_stack->base.name, -1);

	stack_destroy(&usb_stack->base);

	libusb_unref_device(usb_stack->device);

	log_debug("Released USB device (bus: %u, device: %u), was %s",
	          usb_stack->bus_number, usb_stack->device_address, name);
}

void usb_stack_start_pending_error_timer(USBStack *usb_stack) {
	if (timer_configure(&usb_stack->pending_error_timer, PENDING_ERROR_TIMER_DELAY, 0) < 0) {
		log_error("Could not start pending error timer for %s: %s (%d)",
		          usb_stack->base.name, get_errno_name(errno), errno);

		return;
	}
}
