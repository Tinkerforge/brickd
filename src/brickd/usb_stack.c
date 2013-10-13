/*
 * brickd
 * Copyright (C) 2012-2013 Matthias Bolte <matthias@tinkerforge.com>
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
 * care of sending an receiving packets over USB.
 */

#include <errno.h>
#include <string.h>

#include "usb_stack.h"

#include "array.h"
#include "hardware.h"
#include "log.h"
#include "network.h"
#include "usb.h"
#include "usb_transfer.h"
#include "utils.h"

#define LOG_CATEGORY LOG_CATEGORY_USB

#define MAX_READ_TRANSFERS 5
#define MAX_WRITE_TRANSFERS 5
#define MAX_QUEUED_WRITES 256

static void usb_stack_read_callback(USBTransfer *transfer) {
	const char *message = NULL;
	char base58[MAX_BASE58_STR_SIZE];

	if (transfer->handle->actual_length < (int)sizeof(PacketHeader)) {
		log_error("Read transfer %p returned response with incomplete header (actual: %u < minimum: %d) from %s",
		          transfer, transfer->handle->actual_length, (int)sizeof(PacketHeader),
		          transfer->stack->base.name);

		return;
	}

	if (transfer->handle->actual_length != transfer->packet.header.length) {
		log_error("Read transfer %p returned response with length mismatch (actual: %u != expected: %u) from %s",
		          transfer, transfer->handle->actual_length, transfer->packet.header.length,
		          transfer->stack->base.name);

		return;
	}

	if (!packet_header_is_valid_response(&transfer->packet.header, &message)) {
		log_debug("Got invalid response (U: %s, L: %u, F: %u, S: %u, E: %u) from %s: %s",
		          base58_encode(base58, uint32_from_le(transfer->packet.header.uid)),
		          transfer->packet.header.length,
		          transfer->packet.header.function_id,
		          packet_header_get_sequence_number(&transfer->packet.header),
		          packet_header_get_error_code(&transfer->packet.header),
		          transfer->stack->base.name,
		          message);

		return;
	}

	if (packet_header_get_sequence_number(&transfer->packet.header) == 0) {
		log_debug("Got %scallback (U: %s, L: %u, F: %u) from %s",
		          packet_get_callback_type(&transfer->packet),
		          base58_encode(base58, uint32_from_le(transfer->packet.header.uid)),
		          transfer->packet.header.length,
		          transfer->packet.header.function_id,
		          transfer->stack->base.name);
	} else {
		log_debug("Got response (U: %s, L: %u, F: %u, S: %u, E: %u) from %s",
		          base58_encode(base58, uint32_from_le(transfer->packet.header.uid)),
		          transfer->packet.header.length,
		          transfer->packet.header.function_id,
		          packet_header_get_sequence_number(&transfer->packet.header),
		          packet_header_get_error_code(&transfer->packet.header),
		          transfer->stack->base.name);
	}

	if (stack_add_uid(&transfer->stack->base, transfer->packet.header.uid) < 0) {
		return;
	}

	network_dispatch_response(&transfer->packet);
}

static void usb_stack_write_callback(USBTransfer *transfer) {
	Packet *packet;
	char base58[MAX_BASE58_STR_SIZE];

	if (transfer->stack->write_queue.count > 0) {
		packet = array_get(&transfer->stack->write_queue, 0);

		memcpy(&transfer->packet, packet, packet->header.length);

		if (usb_transfer_submit(transfer) < 0) {
			log_error("Could not send queued request (U: %s, L: %u, F: %u, S: %u, R: %u) to %s: %s (%d)",
			          base58_encode(base58, uint32_from_le(transfer->packet.header.uid)),
			          transfer->packet.header.length,
			          transfer->packet.header.function_id,
			          packet_header_get_sequence_number(&transfer->packet.header),
			          packet_header_get_response_expected(&transfer->packet.header),
			          transfer->stack->base.name,
			          get_errno_name(errno), errno);

			return;
		}

		array_remove(&transfer->stack->write_queue, 0, NULL);

		log_debug("Sent queued request (U: %s, L: %u, F: %u, S: %u, R: %u) to %s",
		          base58_encode(base58, uint32_from_le(transfer->packet.header.uid)),
		          transfer->packet.header.length,
		          transfer->packet.header.function_id,
		          packet_header_get_sequence_number(&transfer->packet.header),
		          packet_header_get_response_expected(&transfer->packet.header),
		          transfer->stack->base.name);

		log_info("Handled queued request for %s, %d request(s) left in write queue",
		         transfer->stack->base.name,
		         transfer->stack->write_queue.count);
	}
}

static int usb_stack_dispatch_request(USBStack *stack, Packet *request) {
	int i;
	USBTransfer *transfer;
	Packet *queued_request;

	for (i = 0; i < stack->write_transfers.count; ++i) {
		transfer = array_get(&stack->write_transfers, i);

		if (transfer->submitted) {
			continue;
		}

		memcpy(&transfer->packet, request, request->header.length);

		if (usb_transfer_submit(transfer) < 0) {
			// FIXME: how to handle a failed submission, try to re-submit?

			continue;
		}

		return 0;
	}

	if (stack->write_queue.count >= MAX_QUEUED_WRITES) {
		log_warn("Dropping %d item(s) from write queue array of %s",
		         stack->write_queue.count - MAX_QUEUED_WRITES + 1,
		         stack->base.name);

		while (stack->write_queue.count >= MAX_QUEUED_WRITES) {
			array_remove(&stack->write_queue, 0, NULL);
		}
	}

	queued_request = array_append(&stack->write_queue);

	if (queued_request == NULL) {
		log_error("Could not append to write queue array of %s: %s (%d)",
		          stack->base.name, get_errno_name(errno), errno);

		return -1;
	}

	log_warn("Could not find a free write transfer for %s, put request into write queue (count: %d)",
	         stack->base.name, stack->write_queue.count);

	memcpy(queued_request, request, request->header.length);

	return 0;
}

int usb_stack_create(USBStack *stack, uint8_t bus_number, uint8_t device_address) {
	int phase = 0;
	int rc;
	libusb_device **devices;
	libusb_device *device;
	int i = 0;
	char preliminary_name[MAX_STACK_NAME];
	USBTransfer *transfer;

	log_debug("Acquiring USB device (bus: %u, device: %u)",
	          bus_number, device_address);

	stack->bus_number = bus_number;
	stack->device_address = device_address;

	stack->context = NULL;
	stack->device = NULL;
	stack->device_handle = NULL;

	// create stack base
	snprintf(preliminary_name, sizeof(preliminary_name) - 1,
	         "USB device (bus: %u, device: %u)", bus_number, device_address);
	preliminary_name[sizeof(preliminary_name) - 1] = '\0';

	if (stack_create(&stack->base, preliminary_name,
	                 (DispatchRequestFunction)usb_stack_dispatch_request) < 0) {
		log_error("Could not create base stack for %s: %s (%d)",
		          preliminary_name, get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 1;

	// initialize per-device libusb context
	if (usb_create_context(&stack->context) < 0) {
		goto cleanup;
	}

	phase = 2;

	// find device
	rc = libusb_get_device_list(stack->context, &devices);

	if (rc < 0) {
		log_error("Could not get USB device list: %s (%d)",
		          usb_get_error_name(rc), rc);

		goto cleanup;
	}

	for (device = devices[0]; device != NULL; device = devices[++i]) {
		if (stack->bus_number == libusb_get_bus_number(device) &&
			stack->device_address == libusb_get_device_address(device)) {
			stack->device = libusb_ref_device(device);

			break;
		}
	}

	libusb_free_device_list(devices, 1);

	if (stack->device == NULL) {
		log_error("Could not find %s", stack->base.name);

		goto cleanup;
	}

	phase = 3;

	// open device
	rc = libusb_open(stack->device, &stack->device_handle);

	if (rc < 0) {
		log_error("Could not open %s: %s (%d)",
		          stack->base.name, usb_get_error_name(rc), rc);

		goto cleanup;
	}

	phase = 4;

	// reset device
	rc = libusb_reset_device(stack->device_handle);

	if (rc < 0) {
		log_error("Could not reset %s: %s (%d)",
		          stack->base.name, usb_get_error_name(rc), rc);

		goto cleanup;
	}

	// set device configuration
	rc = libusb_set_configuration(stack->device_handle, USB_CONFIGURATION);

	if (rc < 0) {
		log_error("Could set configuration for %s: %s (%d)",
		          stack->base.name, usb_get_error_name(rc), rc);

		goto cleanup;
	}

	// claim device interface
	rc = libusb_claim_interface(stack->device_handle, USB_INTERFACE);

	if (rc < 0) {
		log_error("Could not claim interface of %s: %s (%d)",
		          stack->base.name, usb_get_error_name(rc), rc);

		goto cleanup;
	}

	phase = 5;

	// update stack name
	if (usb_get_device_name(stack->device_handle, stack->base.name,
	                        sizeof(stack->base.name)) < 0) {
		goto cleanup;
	}

	// allocate and submit read transfers
	if (array_create(&stack->read_transfers, MAX_READ_TRANSFERS,
	                 sizeof(USBTransfer), 1) < 0) {
		log_error("Could not create read transfer array for %s: %s (%d)",
		          stack->base.name, get_errno_name(errno), errno);

		goto cleanup;
	}

	for (i = 0; i < MAX_READ_TRANSFERS; ++i) {
		transfer = array_append(&stack->read_transfers);

		if (transfer == NULL) {
			log_error("Could not append to read transfer array of %s: %s (%d)",
			          stack->base.name, get_errno_name(errno), errno);

			goto cleanup;
		}

		if (usb_transfer_create(transfer, stack, USB_TRANSFER_TYPE_READ,
		                        usb_stack_read_callback) < 0) {
			array_remove(&stack->read_transfers,
			             stack->read_transfers.count -1, NULL);

			goto cleanup;
		}

		if (usb_transfer_submit(transfer) < 0) {
			goto cleanup;
		}
	}

	phase = 6;

	// allocate write queue
	if (array_create(&stack->write_queue, 32, sizeof(Packet), 1) < 0) {
		log_error("Could not create write queue array for %s: %s (%d)",
		          stack->base.name, get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 7;

	// allocate write transfers
	if (array_create(&stack->write_transfers, MAX_WRITE_TRANSFERS,
	                 sizeof(USBTransfer), 1) < 0) {
		log_error("Could not create write transfer array for %s: %s (%d)",
		          stack->base.name, get_errno_name(errno), errno);

		goto cleanup;
	}

	for (i = 0; i < MAX_WRITE_TRANSFERS; ++i) {
		transfer = array_append(&stack->write_transfers);

		if (transfer == NULL) {
			log_error("Could not append to write transfer array of %s: %s (%d)",
			          stack->base.name, get_errno_name(errno), errno);

			goto cleanup;
		}

		if (usb_transfer_create(transfer, stack, USB_TRANSFER_TYPE_WRITE,
		                        usb_stack_write_callback) < 0) {
			array_remove(&stack->write_transfers,
			             stack->write_transfers.count -1, NULL);

			goto cleanup;
		}
	}

	phase = 8;

	// add to stacks array
	if (hardware_add_stack(&stack->base) < 0) {
		goto cleanup;
	}

	phase = 9;

cleanup:
	switch (phase) { // no breaks, all cases fall through intentionally
	case 8:
		array_destroy(&stack->write_transfers, (FreeFunction)usb_transfer_destroy);

	case 7:
		array_destroy(&stack->write_queue, NULL);

	case 6:
		array_destroy(&stack->read_transfers, (FreeFunction)usb_transfer_destroy);

	case 5:
		libusb_release_interface(stack->device_handle, USB_INTERFACE);

	case 4:
		libusb_close(stack->device_handle);

	case 3:
		libusb_unref_device(stack->device);

	case 2:
		usb_destroy_context(stack->context);

	case 1:
		stack_destroy(&stack->base);

	default:
		break;
	}

	return phase == 9 ? 0 : -1;
}

void usb_stack_destroy(USBStack *stack) {
	char name[MAX_STACK_NAME];

	hardware_remove_stack(&stack->base);

	array_destroy(&stack->read_transfers, (FreeFunction)usb_transfer_destroy);
	array_destroy(&stack->write_transfers, (FreeFunction)usb_transfer_destroy);

	array_destroy(&stack->write_queue, NULL);

	libusb_release_interface(stack->device_handle, USB_INTERFACE);

	libusb_close(stack->device_handle);

	libusb_unref_device(stack->device);

	usb_destroy_context(stack->context);

	string_copy(name, stack->base.name, sizeof(name));

	stack_destroy(&stack->base);

	log_debug("Released USB device (bus: %u, device: %u), was %s",
	          stack->bus_number, stack->device_address, name);
}
