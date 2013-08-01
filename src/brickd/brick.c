/*
 * brickd
 * Copyright (C) 2012-2013 Matthias Bolte <matthias@tinkerforge.com>
 *
 * brick.c: Brick specific functions
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

#include "brick.h"

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
		          transfer->brick->product, transfer->brick->serial_number);

		return;
	}

	if (transfer->handle->actual_length != transfer->packet.header.length) {
		log_error("Read transfer %p returned response with length mismatch (actual: %u != expected: %u) from %s [%s]",
		          transfer, transfer->handle->actual_length, transfer->packet.header.length,
		          transfer->brick->product, transfer->brick->serial_number);

		return;
	}

	if (!packet_header_is_valid_response(&transfer->packet.header, &message)) {
		log_debug("Got invalid response (U: %s, L: %u, F: %u, S: %u, E: %u) from %s [%s]: %s",
		          base58_encode(base58, uint32_from_le(transfer->packet.header.uid)),
		          transfer->packet.header.length,
		          transfer->packet.header.function_id,
		          packet_header_get_sequence_number(&transfer->packet.header),
		          packet_header_get_error_code(&transfer->packet.header),
		          transfer->brick->product, transfer->brick->serial_number,
		          message);

		return;
	}

	if (packet_header_get_sequence_number(&transfer->packet.header) == 0) {
		log_debug("Got %scallback (U: %s, L: %u, F: %u) from %s [%s]",
		          packet_get_callback_type(&transfer->packet),
		          base58_encode(base58, uint32_from_le(transfer->packet.header.uid)),
		          transfer->packet.header.length,
		          transfer->packet.header.function_id,
		          transfer->brick->product, transfer->brick->serial_number);
	} else {
		log_debug("Got response (U: %s, L: %u, F: %u, S: %u, E: %u) from %s [%s]",
		          base58_encode(base58, uint32_from_le(transfer->packet.header.uid)),
		          transfer->packet.header.length,
		          transfer->packet.header.function_id,
		          packet_header_get_sequence_number(&transfer->packet.header),
		          packet_header_get_error_code(&transfer->packet.header),
		          transfer->brick->product, transfer->brick->serial_number);
	}

	if (brick_add_uid(transfer->brick, transfer->packet.header.uid) < 0) {
		return;
	}

	network_dispatch_packet(&transfer->packet);
}

static void write_transfer_callback(Transfer *transfer) {
	Packet *packet;
	char base58[MAX_BASE58_STR_SIZE];

	if (transfer->brick->write_queue.count > 0) {
		packet = array_get(&transfer->brick->write_queue, 0);

		memcpy(&transfer->packet, packet, packet->header.length);

		if (transfer_submit(transfer) < 0) {
			log_error("Could not send queued request (U: %s, L: %u, F: %u, S: %u, R: %u) to %s [%s]: %s (%d)",
			          base58_encode(base58, uint32_from_le(transfer->packet.header.uid)),
			          transfer->packet.header.length,
			          transfer->packet.header.function_id,
			          packet_header_get_sequence_number(&transfer->packet.header),
			          packet_header_get_response_expected(&transfer->packet.header),
			          transfer->brick->product, transfer->brick->serial_number,
			          get_errno_name(errno), errno);

			return;
		}

		array_remove(&transfer->brick->write_queue, 0, NULL);

		log_debug("Sent queued request (U: %s, L: %u, F: %u, S: %u, R: %u) to %s [%s]",
		          base58_encode(base58, uint32_from_le(transfer->packet.header.uid)),
		          transfer->packet.header.length,
		          transfer->packet.header.function_id,
		          packet_header_get_sequence_number(&transfer->packet.header),
		          packet_header_get_response_expected(&transfer->packet.header),
		          transfer->brick->product, transfer->brick->serial_number);

		log_info("Handled queued request for %s [%s], %d request(s) left in write queue",
		         transfer->brick->product, transfer->brick->serial_number,
		         transfer->brick->write_queue.count);
	}
}

int brick_create(Brick *brick, uint8_t bus_number, uint8_t device_address) {
	int phase = 0;
	int rc;
	libusb_device **devices;
	libusb_device *device;
	int i = 0;
	Transfer *transfer;

	log_debug("Creating Brick from USB device (bus: %u, device: %u)",
	          bus_number, device_address);

	brick->bus_number = bus_number;
	brick->device_address = device_address;

	brick->context = NULL;
	brick->device = NULL;
	brick->device_handle = NULL;

	// initialize per-device libusb context
	if (usb_create_context(&brick->context) < 0) {
		goto cleanup;
	}

	phase = 1;

	// find device
	rc = libusb_get_device_list(brick->context, &devices);

	if (rc < 0) {
		log_error("Could not get USB device list: %s (%d)",
		          get_libusb_error_name(rc), rc);

		goto cleanup;
	}

	for (device = devices[0]; device != NULL; device = devices[i++]) {
		if (brick->bus_number == libusb_get_bus_number(device) &&
			brick->device_address == libusb_get_device_address(device)) {
			brick->device = libusb_ref_device(device);
			break;
		}
	}

	libusb_free_device_list(devices, 1);

	if (brick->device == NULL) {
		log_error("Could not find USB device (bus: %u, device: %u)",
		          brick->bus_number, brick->device_address);

		goto cleanup;
	}

	phase = 2;

	// get device descriptor
	rc = libusb_get_device_descriptor(brick->device, &brick->device_descriptor);

	if (rc < 0) {
		log_error("Could not get device descriptor for USB device (bus: %u, device: %u): %s (%d)",
		          brick->bus_number, brick->device_address,
		          get_libusb_error_name(rc), rc);

		goto cleanup;
	}

	// open device
	rc = libusb_open(brick->device, &brick->device_handle);

	if (rc < 0) {
		log_error("Could not open USB device (bus: %u, device: %u): %s (%d)",
		          brick->bus_number, brick->device_address,
		          get_libusb_error_name(rc), rc);

		goto cleanup;
	}

	phase = 3;

	// reset device
	rc = libusb_reset_device(brick->device_handle);

	if (rc < 0) {
		log_error("Could not reset USB device (bus: %u, device: %u): %s (%d)",
		          brick->bus_number, brick->device_address,
		          get_libusb_error_name(rc), rc);

		goto cleanup;
	}

	// set device configuration
	rc = libusb_set_configuration(brick->device_handle, USB_CONFIGURATION);

	if (rc < 0) {
		log_error("Could set USB device configuration (bus: %u, device: %u): %s (%d)",
		          brick->bus_number, brick->device_address,
		          get_libusb_error_name(rc), rc);

		goto cleanup;
	}

	// claim device interface
	rc = libusb_claim_interface(brick->device_handle, USB_INTERFACE);

	if (rc < 0) {
		log_error("Could not claim USB device interface (bus: %u, device: %u): %s (%d)",
		          brick->bus_number, brick->device_address,
		          get_libusb_error_name(rc), rc);

		goto cleanup;
	}

	phase = 4;

	// get product string descriptor
	rc = libusb_get_string_descriptor_ascii(brick->device_handle,
	                                        brick->device_descriptor.iProduct,
	                                        (unsigned char *)brick->product,
	                                        sizeof(brick->product));

	if (rc < 0) {
		log_error("Could not get product string descriptor for USB device (bus: %u, device: %u): %s (%d)",
		          brick->bus_number, brick->device_address,
		          get_libusb_error_name(rc), rc);

		goto cleanup;
	}

	// get serial number string descriptor
	rc = libusb_get_string_descriptor_ascii(brick->device_handle,
	                                        brick->device_descriptor.iSerialNumber,
	                                        (unsigned char *)brick->serial_number,
	                                        sizeof(brick->serial_number));

	if (rc < 0) {
		log_error("Could not get serial number string descriptor for USB device (bus: %u, device: %u): %s (%d)",
		          brick->bus_number, brick->device_address,
		          get_libusb_error_name(rc), rc);

		goto cleanup;
	}

	// allocate and submit read transfers
	if (array_create(&brick->read_transfers, MAX_READ_TRANSFERS,
	                 sizeof(Transfer), 1) < 0) {
		log_error("Could not create read transfer array: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	for (i = 0; i < MAX_READ_TRANSFERS; ++i) {
		transfer = array_append(&brick->read_transfers);

		if (transfer == NULL) {
			log_error("Could not append to read transfer array: %s (%d)",
			          get_errno_name(errno), errno);

			goto cleanup;
		}

		if (transfer_create(transfer, brick, TRANSFER_TYPE_READ,
		                    read_transfer_callback) < 0) {
			array_remove(&brick->read_transfers,
			             brick->read_transfers.count -1, NULL);

			goto cleanup;
		}

		if (transfer_submit(transfer) < 0) {
			goto cleanup;
		}
	}

	phase = 5;

	// allocate write transfers
	if (array_create(&brick->write_transfers, MAX_WRITE_TRANSFERS,
	                 sizeof(Transfer), 1) < 0) {
		log_error("Could not create write transfer array: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	for (i = 0; i < MAX_WRITE_TRANSFERS; ++i) {
		transfer = array_append(&brick->write_transfers);

		if (transfer == NULL) {
			log_error("Could not append to write transfer array: %s (%d)",
			          get_errno_name(errno), errno);

			goto cleanup;
		}

		if (transfer_create(transfer, brick, TRANSFER_TYPE_WRITE,
		                    write_transfer_callback) < 0) {
			array_remove(&brick->write_transfers,
			             brick->write_transfers.count -1, NULL);

			goto cleanup;
		}
	}

	phase = 6;

	if (array_create(&brick->uids, 32, sizeof(uint32_t), 1) < 0) {
		log_error("Could not create UID array: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 7;

	if (array_create(&brick->write_queue, 32, sizeof(Packet), 1) < 0) {
		log_error("Could not create write queue array: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 8;

cleanup:
	switch (phase) { // no breaks, all cases fall through intentionally
	case 7:
		array_destroy(&brick->uids, NULL);

	case 6:
		array_destroy(&brick->write_transfers, (FreeFunction)transfer_destroy);

	case 5:
		array_destroy(&brick->read_transfers, (FreeFunction)transfer_destroy);

	case 4:
		libusb_release_interface(brick->device_handle, USB_INTERFACE);

	case 3:
		libusb_close(brick->device_handle);

	case 2:
		libusb_unref_device(brick->device);

	case 1:
		usb_destroy_context(brick->context);

	default:
		break;
	}

	return phase == 8 ? 0 : -1;
}

void brick_destroy(Brick *brick) {
	array_destroy(&brick->write_queue, NULL);

	array_destroy(&brick->uids, NULL);

	array_destroy(&brick->read_transfers, (FreeFunction)transfer_destroy);
	array_destroy(&brick->write_transfers, (FreeFunction)transfer_destroy);

	libusb_release_interface(brick->device_handle, USB_INTERFACE);

	libusb_close(brick->device_handle);

	libusb_unref_device(brick->device);

	usb_destroy_context(brick->context);

	log_debug("Destroyed %s [%s] of USB device (bus: %u, device: %u)",
	          brick->product, brick->serial_number,
	          brick->bus_number, brick->device_address);
}

int brick_add_uid(Brick *brick, uint32_t uid /* always little endian */) {
	int i;
	uint32_t known_uid;
	uint32_t *new_uid;

	for (i = 0; i < brick->uids.count; ++i) {
		known_uid = *(uint32_t *)array_get(&brick->uids, i);

		if (known_uid == uid) {
			return 0;
		}
	}

	new_uid = array_append(&brick->uids);

	if (new_uid == NULL) {
		log_error("Could not append to UID array: %s (%d)",
		          get_errno_name(errno), errno);

		return -1;
	}

	*new_uid = uid;

	return 0;
}

int brick_knows_uid(Brick *brick, uint32_t uid /* always little endian */) {
	int i;
	uint32_t known_uid;

	for (i = 0; i < brick->uids.count; ++i) {
		known_uid = *(uint32_t *)array_get(&brick->uids, i);

		if (known_uid == uid) {
			return 1;
		}
	}

	return 0;
}

int brick_dispatch_packet(Brick *brick, Packet *packet, int force) {
	int i;
	Transfer *transfer;
	int submitted = 0;
	Packet *queued_packet;
	int rc = -1;

	if (force || brick_knows_uid(brick, packet->header.uid)) {
		for (i = 0; i < brick->write_transfers.count; ++i) {
			transfer = array_get(&brick->write_transfers, i);

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
			if (brick->write_queue.count >= MAX_QUEUED_WRITES) {
				log_warn("Dropping %d item(s) from write queue array of %s [%s]",
				         brick->write_queue.count - MAX_QUEUED_WRITES + 1,
				         brick->product, brick->serial_number);

				while (brick->write_queue.count >= MAX_QUEUED_WRITES) {
					array_remove(&brick->write_queue, 0, NULL);
				}
			}

			queued_packet = array_append(&brick->write_queue);

			if (queued_packet == NULL) {
				log_error("Could not append to write queue array of %s [%s]: %s (%d)",
				          brick->product, brick->serial_number,
				          get_errno_name(errno), errno);

				goto cleanup;
			}

			log_warn("Could not find a free write transfer for %s [%s], put request into write queue (count: %d)",
			         brick->product, brick->serial_number,
			         brick->write_queue.count);

			memcpy(queued_packet, packet, packet->header.length);

			submitted = 1;
		} else {
			if (force) {
				log_debug("Forced to sent request to %s [%s]",
				          brick->product, brick->serial_number);
			} else {
				log_debug("Sent request to %s [%s]",
				          brick->product, brick->serial_number);
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
