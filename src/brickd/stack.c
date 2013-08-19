/*
 * brickd
 * Copyright (C) 2012-2013 Matthias Bolte <matthias@tinkerforge.com>
 *
 * stack.c: Stack specific functions
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

#include <errno.h>
#include <libusb.h>
#include <string.h>

#include "stack.h"

#include "log.h"
#include "network.h"
#include "transfer.h"
#include "usb.h"
#include "utils.h"

#define LOG_CATEGORY LOG_CATEGORY_USB

#define MAX_READ_TRANSFERS 5
#define MAX_WRITE_TRANSFERS 5
#define MAX_QUEUED_WRITES 256

static void read_transfer_callback(Transfer *transfer) {
	const char *message = NULL;
	char base58[MAX_BASE58_STR_SIZE];

	if (transfer->handle->actual_length < (int)sizeof(PacketHeader)) {
		log_error("Read transfer %p returned response with incomplete header (actual: %u < minimum: %d) from %s [%s]",
		          transfer, transfer->handle->actual_length, (int)sizeof(PacketHeader),
		          transfer->stack->product, transfer->stack->serial_number);

		return;
	}

	if (transfer->handle->actual_length != transfer->packet.header.length) {
		log_error("Read transfer %p returned response with length mismatch (actual: %u != expected: %u) from %s [%s]",
		          transfer, transfer->handle->actual_length, transfer->packet.header.length,
		          transfer->stack->product, transfer->stack->serial_number);

		return;
	}

	if (!packet_header_is_valid_response(&transfer->packet.header, &message)) {
		log_debug("Got invalid response (U: %s, L: %u, F: %u, S: %u, E: %u) from %s [%s]: %s",
		          base58_encode(base58, uint32_from_le(transfer->packet.header.uid)),
		          transfer->packet.header.length,
		          transfer->packet.header.function_id,
		          packet_header_get_sequence_number(&transfer->packet.header),
		          packet_header_get_error_code(&transfer->packet.header),
		          transfer->stack->product, transfer->stack->serial_number,
		          message);

		return;
	}

	if (packet_header_get_sequence_number(&transfer->packet.header) == 0) {
		log_debug("Got %scallback (U: %s, L: %u, F: %u) from %s [%s]",
		          packet_get_callback_type(&transfer->packet),
		          base58_encode(base58, uint32_from_le(transfer->packet.header.uid)),
		          transfer->packet.header.length,
		          transfer->packet.header.function_id,
		          transfer->stack->product, transfer->stack->serial_number);
	} else {
		log_debug("Got response (U: %s, L: %u, F: %u, S: %u, E: %u) from %s [%s]",
		          base58_encode(base58, uint32_from_le(transfer->packet.header.uid)),
		          transfer->packet.header.length,
		          transfer->packet.header.function_id,
		          packet_header_get_sequence_number(&transfer->packet.header),
		          packet_header_get_error_code(&transfer->packet.header),
		          transfer->stack->product, transfer->stack->serial_number);
	}

	if (stack_add_uid(transfer->stack, transfer->packet.header.uid) < 0) {
		return;
	}

	network_dispatch_packet(&transfer->packet);
}

static void write_transfer_callback(Transfer *transfer) {
	Packet *packet;
	char base58[MAX_BASE58_STR_SIZE];

	if (transfer->stack->write_queue.count > 0) {
		packet = array_get(&transfer->stack->write_queue, 0);

		memcpy(&transfer->packet, packet, packet->header.length);

		if (transfer_submit(transfer) < 0) {
			log_error("Could not send queued request (U: %s, L: %u, F: %u, S: %u, R: %u) to %s [%s]: %s (%d)",
			          base58_encode(base58, uint32_from_le(transfer->packet.header.uid)),
			          transfer->packet.header.length,
			          transfer->packet.header.function_id,
			          packet_header_get_sequence_number(&transfer->packet.header),
			          packet_header_get_response_expected(&transfer->packet.header),
			          transfer->stack->product, transfer->stack->serial_number,
			          get_errno_name(errno), errno);

			return;
		}

		array_remove(&transfer->stack->write_queue, 0, NULL);

		log_debug("Sent queued request (U: %s, L: %u, F: %u, S: %u, R: %u) to %s [%s]",
		          base58_encode(base58, uint32_from_le(transfer->packet.header.uid)),
		          transfer->packet.header.length,
		          transfer->packet.header.function_id,
		          packet_header_get_sequence_number(&transfer->packet.header),
		          packet_header_get_response_expected(&transfer->packet.header),
		          transfer->stack->product, transfer->stack->serial_number);

		log_info("Handled queued request for %s [%s], %d request(s) left in write queue",
		         transfer->stack->product, transfer->stack->serial_number,
		         transfer->stack->write_queue.count);
	}
}

int stack_create(Stack *stack, uint8_t bus_number, uint8_t device_address) {
	int phase = 0;
	int rc;
	libusb_device **devices;
	libusb_device *device;
	int i = 0;
	Transfer *transfer;

	log_debug("Acquiring USB device (bus: %u, device: %u)",
	          bus_number, device_address);

	stack->bus_number = bus_number;
	stack->device_address = device_address;

	stack->context = NULL;
	stack->device = NULL;
	stack->device_handle = NULL;

	// initialize per-device libusb context
	if (usb_create_context(&stack->context) < 0) {
		goto cleanup;
	}

	phase = 1;

	// find device
	rc = libusb_get_device_list(stack->context, &devices);

	if (rc < 0) {
		log_error("Could not get USB device list: %s (%d)",
		          get_libusb_error_name(rc), rc);

		goto cleanup;
	}

	for (device = devices[0]; device != NULL; device = devices[i++]) {
		if (stack->bus_number == libusb_get_bus_number(device) &&
			stack->device_address == libusb_get_device_address(device)) {
			stack->device = libusb_ref_device(device);
			break;
		}
	}

	libusb_free_device_list(devices, 1);

	if (stack->device == NULL) {
		log_error("Could not find USB device (bus: %u, device: %u)",
		          stack->bus_number, stack->device_address);

		goto cleanup;
	}

	phase = 2;

	// get device descriptor
	rc = libusb_get_device_descriptor(stack->device, &stack->device_descriptor);

	if (rc < 0) {
		log_error("Could not get device descriptor for USB device (bus: %u, device: %u): %s (%d)",
		          stack->bus_number, stack->device_address,
		          get_libusb_error_name(rc), rc);

		goto cleanup;
	}

	// open device
	rc = libusb_open(stack->device, &stack->device_handle);

	if (rc < 0) {
		log_error("Could not open USB device (bus: %u, device: %u): %s (%d)",
		          stack->bus_number, stack->device_address,
		          get_libusb_error_name(rc), rc);

		goto cleanup;
	}

	phase = 3;

	// reset device
	rc = libusb_reset_device(stack->device_handle);

	if (rc < 0) {
		log_error("Could not reset USB device (bus: %u, device: %u): %s (%d)",
		          stack->bus_number, stack->device_address,
		          get_libusb_error_name(rc), rc);

		goto cleanup;
	}

	// set device configuration
	rc = libusb_set_configuration(stack->device_handle, USB_CONFIGURATION);

	if (rc < 0) {
		log_error("Could set USB device configuration (bus: %u, device: %u): %s (%d)",
		          stack->bus_number, stack->device_address,
		          get_libusb_error_name(rc), rc);

		goto cleanup;
	}

	// claim device interface
	rc = libusb_claim_interface(stack->device_handle, USB_INTERFACE);

	if (rc < 0) {
		log_error("Could not claim USB device interface (bus: %u, device: %u): %s (%d)",
		          stack->bus_number, stack->device_address,
		          get_libusb_error_name(rc), rc);

		goto cleanup;
	}

	phase = 4;

	// get product string descriptor
	rc = libusb_get_string_descriptor_ascii(stack->device_handle,
	                                        stack->device_descriptor.iProduct,
	                                        (unsigned char *)stack->product,
	                                        sizeof(stack->product));

	if (rc < 0) {
		log_error("Could not get product string descriptor for USB device (bus: %u, device: %u): %s (%d)",
		          stack->bus_number, stack->device_address,
		          get_libusb_error_name(rc), rc);

		goto cleanup;
	}

	// get serial number string descriptor
	rc = libusb_get_string_descriptor_ascii(stack->device_handle,
	                                        stack->device_descriptor.iSerialNumber,
	                                        (unsigned char *)stack->serial_number,
	                                        sizeof(stack->serial_number));

	if (rc < 0) {
		log_error("Could not get serial number string descriptor for USB device (bus: %u, device: %u): %s (%d)",
		          stack->bus_number, stack->device_address,
		          get_libusb_error_name(rc), rc);

		goto cleanup;
	}

	// allocate and submit read transfers
	if (array_create(&stack->read_transfers, MAX_READ_TRANSFERS,
	                 sizeof(Transfer), 1) < 0) {
		log_error("Could not create read transfer array: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	for (i = 0; i < MAX_READ_TRANSFERS; ++i) {
		transfer = array_append(&stack->read_transfers);

		if (transfer == NULL) {
			log_error("Could not append to read transfer array: %s (%d)",
			          get_errno_name(errno), errno);

			goto cleanup;
		}

		if (transfer_create(transfer, stack, TRANSFER_TYPE_READ,
		                    read_transfer_callback) < 0) {
			array_remove(&stack->read_transfers,
			             stack->read_transfers.count -1, NULL);

			goto cleanup;
		}

		if (transfer_submit(transfer) < 0) {
			goto cleanup;
		}
	}

	phase = 5;

	// allocate write transfers
	if (array_create(&stack->write_transfers, MAX_WRITE_TRANSFERS,
	                 sizeof(Transfer), 1) < 0) {
		log_error("Could not create write transfer array: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	for (i = 0; i < MAX_WRITE_TRANSFERS; ++i) {
		transfer = array_append(&stack->write_transfers);

		if (transfer == NULL) {
			log_error("Could not append to write transfer array: %s (%d)",
			          get_errno_name(errno), errno);

			goto cleanup;
		}

		if (transfer_create(transfer, stack, TRANSFER_TYPE_WRITE,
		                    write_transfer_callback) < 0) {
			array_remove(&stack->write_transfers,
			             stack->write_transfers.count -1, NULL);

			goto cleanup;
		}
	}

	phase = 6;

	if (array_create(&stack->uids, 32, sizeof(uint32_t), 1) < 0) {
		log_error("Could not create UID array: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 7;

	if (array_create(&stack->write_queue, 32, sizeof(Packet), 1) < 0) {
		log_error("Could not create write queue array: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 8;

cleanup:
	switch (phase) { // no breaks, all cases fall through intentionally
	case 7:
		array_destroy(&stack->uids, NULL);

	case 6:
		array_destroy(&stack->write_transfers, (FreeFunction)transfer_destroy);

	case 5:
		array_destroy(&stack->read_transfers, (FreeFunction)transfer_destroy);

	case 4:
		libusb_release_interface(stack->device_handle, USB_INTERFACE);

	case 3:
		libusb_close(stack->device_handle);

	case 2:
		libusb_unref_device(stack->device);

	case 1:
		usb_destroy_context(stack->context);

	default:
		break;
	}

	return phase == 8 ? 0 : -1;
}

void stack_destroy(Stack *stack) {
	array_destroy(&stack->write_queue, NULL);

	array_destroy(&stack->uids, NULL);

	array_destroy(&stack->read_transfers, (FreeFunction)transfer_destroy);
	array_destroy(&stack->write_transfers, (FreeFunction)transfer_destroy);

	libusb_release_interface(stack->device_handle, USB_INTERFACE);

	libusb_close(stack->device_handle);

	libusb_unref_device(stack->device);

	usb_destroy_context(stack->context);

	log_debug("Released USB device (bus: %u, device: %u), was %s [%s]",
	          stack->bus_number, stack->device_address,
	          stack->product, stack->serial_number);
}

int stack_add_uid(Stack *stack, uint32_t uid /* always little endian */) {
	int i;
	uint32_t known_uid;
	uint32_t *new_uid;

	for (i = 0; i < stack->uids.count; ++i) {
		known_uid = *(uint32_t *)array_get(&stack->uids, i);

		if (known_uid == uid) {
			return 0;
		}
	}

	new_uid = array_append(&stack->uids);

	if (new_uid == NULL) {
		log_error("Could not append to UID array: %s (%d)",
		          get_errno_name(errno), errno);

		return -1;
	}

	*new_uid = uid;

	return 0;
}

int stack_knows_uid(Stack *stack, uint32_t uid /* always little endian */) {
	int i;
	uint32_t known_uid;

	for (i = 0; i < stack->uids.count; ++i) {
		known_uid = *(uint32_t *)array_get(&stack->uids, i);

		if (known_uid == uid) {
			return 1;
		}
	}

	return 0;
}

int stack_dispatch_packet(Stack *stack, Packet *packet, int force) {
	int i;
	Transfer *transfer;
	int submitted = 0;
	Packet *queued_packet;
	int rc = -1;

	if (force || stack_knows_uid(stack, packet->header.uid)) {
		for (i = 0; i < stack->write_transfers.count; ++i) {
			transfer = array_get(&stack->write_transfers, i);

			if (transfer->submitted) {
				continue;
			}

			memcpy(&transfer->packet, packet, packet->header.length);

			if (transfer_submit(transfer) < 0) {
				// FIXME: how to handle a failed submission, try to re-submit?

				continue;
			}

			submitted = 1;

			break;
		}

		if (!submitted) {
			if (stack->write_queue.count >= MAX_QUEUED_WRITES) {
				log_warn("Dropping %d item(s) from write queue array of %s [%s]",
				         stack->write_queue.count - MAX_QUEUED_WRITES + 1,
				         stack->product, stack->serial_number);

				while (stack->write_queue.count >= MAX_QUEUED_WRITES) {
					array_remove(&stack->write_queue, 0, NULL);
				}
			}

			queued_packet = array_append(&stack->write_queue);

			if (queued_packet == NULL) {
				log_error("Could not append to write queue array of %s [%s]: %s (%d)",
				          stack->product, stack->serial_number,
				          get_errno_name(errno), errno);

				goto cleanup;
			}

			log_warn("Could not find a free write transfer for %s [%s], put request into write queue (count: %d)",
			         stack->product, stack->serial_number,
			         stack->write_queue.count);

			memcpy(queued_packet, packet, packet->header.length);

			submitted = 1;
		} else {
			if (force) {
				log_debug("Forced to sent request to %s [%s]",
				          stack->product, stack->serial_number);
			} else {
				log_debug("Sent request to %s [%s]",
				          stack->product, stack->serial_number);
			}
		}
	}

	rc = 0;

cleanup:
	if (submitted && rc == 0) {
		rc = 1;
	}

	return rc;
}
