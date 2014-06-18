/*
 * brickd
 * Copyright (C) 2012-2014 Matthias Bolte <matthias@tinkerforge.com>
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

#define LOG_CATEGORY LOG_CATEGORY_USB

#define MAX_READ_TRANSFERS 10
#define MAX_WRITE_TRANSFERS 10
#define MAX_QUEUED_WRITES 32768

static void usb_stack_read_callback(USBTransfer *usb_transfer) {
	const char *message = NULL;
	char packet_signature[PACKET_MAX_SIGNATURE_LENGTH];

	if (usb_transfer->handle->actual_length < (int)sizeof(PacketHeader)) {
		log_error("Read transfer %p returned response with incomplete header (actual: %u < minimum: %d) from %s",
		          usb_transfer, usb_transfer->handle->actual_length, (int)sizeof(PacketHeader),
		          usb_transfer->usb_stack->base.name);

		return;
	}

	if (usb_transfer->handle->actual_length != usb_transfer->packet.header.length) {
		log_error("Read transfer %p returned response with length mismatch (actual: %u != expected: %u) from %s",
		          usb_transfer, usb_transfer->handle->actual_length, usb_transfer->packet.header.length,
		          usb_transfer->usb_stack->base.name);

		return;
	}

	if (!packet_header_is_valid_response(&usb_transfer->packet.header, &message)) {
		log_debug("Got invalid response (%s) from %s: %s",
		          packet_get_response_signature(packet_signature, &usb_transfer->packet),
		          usb_transfer->usb_stack->base.name,
		          message);

		return;
	}

	if (packet_header_get_sequence_number(&usb_transfer->packet.header) == 0) {
		log_debug("Got %scallback (%s) from %s",
		          packet_get_callback_type(&usb_transfer->packet),
		          packet_get_callback_signature(packet_signature, &usb_transfer->packet),
		          usb_transfer->usb_stack->base.name);
	} else {
		log_debug("Got response (%s) from %s",
		          packet_get_response_signature(packet_signature, &usb_transfer->packet),
		          usb_transfer->usb_stack->base.name);
	}

	if (stack_add_uid(&usb_transfer->usb_stack->base, usb_transfer->packet.header.uid) < 0) {
		return;
	}

	network_dispatch_response(&usb_transfer->packet);
}

static void usb_stack_write_callback(USBTransfer *usb_transfer) {
	Packet *request;
	char packet_signature[PACKET_MAX_SIGNATURE_LENGTH];

	if (usb_transfer->usb_stack->write_queue.count > 0) {
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

		log_debug("Sent queued request (%s) to %s, %d request(s) left in write queue",
		          packet_get_request_signature(packet_signature, &usb_transfer->packet),
		          usb_transfer->usb_stack->base.name,
		          usb_transfer->usb_stack->write_queue.count);
	}
}

static int usb_stack_dispatch_request(USBStack *usb_stack, Packet *request) {
	int i;
	USBTransfer *usb_transfer;
	Packet *queued_request;
	char packet_signature[PACKET_MAX_SIGNATURE_LENGTH];
	uint32_t requests_to_drop;

	// find free write transfer
	for (i = 0; i < usb_stack->write_transfers.count; ++i) {
		usb_transfer = array_get(&usb_stack->write_transfers, i);

		if (usb_transfer->submitted) {
			continue;
		}

		memcpy(&usb_transfer->packet, request, request->header.length);

		if (usb_transfer_submit(usb_transfer) < 0) {
			// FIXME: how to handle a failed submission, try to re-submit?

			continue;
		}

		return 0;
	}

	// no free write transfer available, push it to write queue
	log_debug("Could not find a free write transfer for %s, pushing request to write queue (count: %d +1)",
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
		log_error("Could not push request (%s) to write queue for %s, discarding request: %s (%d)",
		          packet_get_request_signature(packet_signature, request),
		          usb_stack->base.name,
		          get_errno_name(errno), errno);

		return -1;
	}

	memcpy(queued_request, request, request->header.length);

	return 0;
}

int usb_stack_create(USBStack *usb_stack, uint8_t bus_number, uint8_t device_address) {
	int phase = 0;
	int rc;
	libusb_device **devices;
	libusb_device *device;
	struct libusb_device_descriptor descriptor;
	int i = 0;
	char preliminary_name[STACK_MAX_NAME_LENGTH];
	int retries = 0;
	USBTransfer *usb_transfer;

	log_debug("Acquiring USB device (bus: %u, device: %u)",
	          bus_number, device_address);

	usb_stack->bus_number = bus_number;
	usb_stack->device_address = device_address;

	usb_stack->context = NULL;
	usb_stack->device_handle = NULL;
	usb_stack->dropped_requests = 0;
	usb_stack->connected = 1;
	usb_stack->active = 0;

	// create stack base
	snprintf(preliminary_name, sizeof(preliminary_name),
	         "USB device (bus: %u, device: %u)", bus_number, device_address);

	if (stack_create(&usb_stack->base, preliminary_name,
	                 (StackDispatchRequestFunction)usb_stack_dispatch_request) < 0) {
		log_error("Could not create base stack for %s: %s (%d)",
		          preliminary_name, get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 1;

	// initialize per-device libusb context
	if (usb_create_context(&usb_stack->context) < 0) {
		goto cleanup;
	}

	phase = 2;

	// find device
	rc = libusb_get_device_list(usb_stack->context, &devices);

	if (rc < 0) {
		log_error("Could not get USB device list: %s (%d)",
		          usb_get_error_name(rc), rc);

		goto cleanup;
	}

	for (device = devices[0]; device != NULL; device = devices[++i]) {
		if (libusb_get_bus_number(device) != usb_stack->bus_number ||
		    libusb_get_device_address(device) != usb_stack->device_address) {
			continue;
		}

		rc = libusb_get_device_descriptor(device, &descriptor);

		if (rc < 0) {
			log_warn("Could not get device descriptor for %s, ignoring USB device: %s (%d)",
			         usb_stack->base.name, usb_get_error_name(rc), rc);

			continue;
		}

		if (descriptor.idVendor != USB_BRICK_VENDOR_ID ||
		    descriptor.idProduct != USB_BRICK_PRODUCT_ID) {
			log_warn("Found non-Brick USB device (bus: %u, device: %u, vendor: 0x%04X, product: 0x%04X) with address collision, ignoring USB device",
			         usb_stack->bus_number, usb_stack->device_address,
			         descriptor.idVendor, descriptor.idProduct);

			continue;
		}

		if (descriptor.bcdDevice < USB_BRICK_DEVICE_RELEASE) {
			log_warn("%s has protocol 1.0 firmware, ignoring USB device",
			         usb_stack->base.name);

			continue;
		}

		// open device
		rc = libusb_open(device, &usb_stack->device_handle);

		if (rc < 0) {
			log_warn("Could not open %s, ignoring USB device: %s (%d)",
			         usb_stack->base.name, usb_get_error_name(rc), rc);

			continue;
		}

		break;
	}

	libusb_free_device_list(devices, 1);

	if (usb_stack->device_handle == NULL) {
		log_error("Could not find %s", usb_stack->base.name);

		goto cleanup;
	}

	phase = 3;

	// get interface endpoints
	rc = usb_get_interface_endpoints(usb_stack->device_handle,
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
	          USB_BRICK_INTERFACE, usb_stack->base.name);

	rc = libusb_claim_interface(usb_stack->device_handle, USB_BRICK_INTERFACE);

	if (rc < 0) {
		// claiming the interface might fail on Linux because the uevent for
		// a USB device arrival was received before the USB subsystem had time
		// to create the USBFS entry for the new device. retry to claim the
		// interface but sleep in between tries so the USB subsystem has time
		// to do something
		while (rc < 0 && retries < 10) {
			millisleep(10);

			++retries;

			rc = libusb_claim_interface(usb_stack->device_handle, USB_BRICK_INTERFACE);
		}

		if (rc < 0) {
			log_error("Could not claim interface of %s after %d retry(s): %s (%d)",
			          usb_stack->base.name, retries, usb_get_error_name(rc), rc);

			goto cleanup;
		}

		log_debug("Claimed interface %d of %s after %d retry(s)",
		          USB_BRICK_INTERFACE, usb_stack->base.name, retries);
	} else {
		log_debug("Claimed interface %d of %s at first try",
		          USB_BRICK_INTERFACE, usb_stack->base.name);
	}

	phase = 4;

	// update stack name
	if (usb_get_device_name(usb_stack->device_handle, usb_stack->base.name,
	                        sizeof(usb_stack->base.name)) < 0) {
		goto cleanup;
	}

	log_debug("Got display name for %s: %s",
	          preliminary_name, usb_stack->base.name);

	// allocate and submit read transfers
	if (array_create(&usb_stack->read_transfers, MAX_READ_TRANSFERS,
	                 sizeof(USBTransfer), 1) < 0) {
		log_error("Could not create read transfer array for %s: %s (%d)",
		          usb_stack->base.name, get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 5;

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

	phase = 6;

	// allocate write transfers
	if (array_create(&usb_stack->write_transfers, MAX_WRITE_TRANSFERS,
	                 sizeof(USBTransfer), 1) < 0) {
		log_error("Could not create write transfer array for %s: %s (%d)",
		          usb_stack->base.name, get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 7;

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
	usb_stack->active = 1;

	if (hardware_add_stack(&usb_stack->base) < 0) {
		goto cleanup;
	}

	phase = 8;

cleanup:
	switch (phase) { // no breaks, all cases fall through intentionally
	case 7:
		array_destroy(&usb_stack->write_transfers, (FreeFunction)usb_transfer_destroy);

	case 6:
		queue_destroy(&usb_stack->write_queue, NULL);

	case 5:
		array_destroy(&usb_stack->read_transfers, (FreeFunction)usb_transfer_destroy);

	case 4:
		libusb_release_interface(usb_stack->device_handle, USB_BRICK_INTERFACE);

	case 3:
		libusb_close(usb_stack->device_handle);

	case 2:
		usb_destroy_context(usb_stack->context);

	case 1:
		stack_destroy(&usb_stack->base);

	default:
		break;
	}

	return phase == 8 ? 0 : -1;
}

void usb_stack_destroy(USBStack *usb_stack) {
	char name[STACK_MAX_NAME_LENGTH];

	usb_stack->active = 0;

	hardware_remove_stack(&usb_stack->base);

	array_destroy(&usb_stack->read_transfers, (FreeFunction)usb_transfer_destroy);
	array_destroy(&usb_stack->write_transfers, (FreeFunction)usb_transfer_destroy);

	queue_destroy(&usb_stack->write_queue, NULL);

	libusb_release_interface(usb_stack->device_handle, USB_BRICK_INTERFACE);

	libusb_close(usb_stack->device_handle);

	usb_destroy_context(usb_stack->context);

	string_copy(name, usb_stack->base.name, sizeof(name));

	stack_destroy(&usb_stack->base);

	log_debug("Released USB device (bus: %u, device: %u), was %s",
	          usb_stack->bus_number, usb_stack->device_address, name);
}

void usb_stack_announce_disconnect(USBStack *usb_stack) {
	int k;
	uint32_t uid; // always little endian
	EnumerateCallback enumerate_callback;

	for (k = 0; k < usb_stack->base.uids.count; ++k) {
		uid = *(uint32_t *)array_get(&usb_stack->base.uids, k);

		memset(&enumerate_callback, 0, sizeof(enumerate_callback));

		enumerate_callback.header.uid = uid;
		enumerate_callback.header.length = sizeof(enumerate_callback);
		enumerate_callback.header.function_id = CALLBACK_ENUMERATE;
		packet_header_set_sequence_number(&enumerate_callback.header, 0);
		packet_header_set_response_expected(&enumerate_callback.header, 1);

		base58_encode(enumerate_callback.uid, uint32_from_le(uid));
		enumerate_callback.enumeration_type = ENUMERATION_TYPE_DISCONNECTED;

		log_debug("Sending enumerate-disconnected callback (uid: %s)",
		          enumerate_callback.uid);

		network_dispatch_response((Packet *)&enumerate_callback);
	}
}
