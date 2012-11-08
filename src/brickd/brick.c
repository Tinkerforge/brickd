/*
 * brickd
 * Copyright (C) 2012 Matthias Bolte <matthias@tinkerforge.com>
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

static void read_transfer_callback(Transfer *transfer) {
	log_debug("Read transfer returned: %s (%d)",
	          get_libusb_transfer_status_name(transfer->handle->status),
	          transfer->handle->status);

	// FIXME: need to avoid or deal with the case that a transfer can return
	// when the brick object is already dead. actually brick destroy should
	// probably cancle and wait for all transfers to return

	if (transfer->handle->status != LIBUSB_TRANSFER_COMPLETED) {
		// FIXME
		return;
	}

	if (transfer->handle->actual_length < (int)sizeof(PacketHeader)) {
		log_error("Read transfer returned packet with incomplete header from %s [%s]",
		          transfer->brick->product, transfer->brick->serial_number);

		goto resubmit;
	}

	if (transfer->handle->actual_length != transfer->packet.header.length) {
		log_error("Read transfer returned packet with length mismatch (%u != %u) from %s [%s]",
		          transfer->handle->actual_length, transfer->packet.header.length,
		          transfer->brick->product, transfer->brick->serial_number);

		goto resubmit;
	}

	log_debug("Got packet (uid: %u, length: %u, function_id: %u) from %s [%s]",
	          transfer->packet.header.uid,
	          transfer->packet.header.length,
	          transfer->packet.header.function_id,
	          transfer->brick->product, transfer->brick->serial_number);

	if (brick_add_uid(transfer->brick, transfer->packet.header.uid) < 0) {
		goto resubmit;
	}

	network_dispatch_packet(&transfer->packet);

resubmit:
	transfer_submit(transfer);
}

int brick_create(Brick *brick, libusb_context *context,
                 uint8_t bus_number, uint8_t device_address) {
	int rc;
	libusb_device **devices;
	libusb_device *device;
	int i = 0;
	Transfer *transfer;

	log_debug("Creating Brick from USB device (bus %u, device %u)",
	          bus_number, device_address);

	brick->bus_number = bus_number;
	brick->device_address = device_address;

	brick->context = context;
	brick->device = NULL;
	brick->device_handle = NULL;

	// initialize per-device libusb context
	/*rc = libusb_init(&brick->context);

	if (rc < 0) {
		log_error("Could not initialize per-device libusb context: %s (%d)",
		          get_libusb_error_name(rc), rc);
		brick_destroy(brick);
		return -1;
	}*/

	// find device
	rc = libusb_get_device_list(brick->context, &devices);

	if (rc < 0) {
		log_error("Could not get USB device list: %s (%d)",
		          get_libusb_error_name(rc), rc);

		brick_destroy(brick); // FIXME: don't use

		return -1;
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
		log_error("Could not find USB device (bus %u, device %u)",
		          brick->bus_number, brick->device_address);

		brick_destroy(brick); // FIXME: don't use

		return -1;
	}

	// get device descriptor
	rc = libusb_get_device_descriptor(brick->device, &brick->device_descriptor);

	if (rc < 0) {
		log_error("Could not get device descriptor for USB device (bus %u, device %u): %s (%d)",
		          brick->bus_number, brick->device_address,
		          get_libusb_error_name(rc), rc);

		brick_destroy(brick); // FIXME: don't use

		return -1;
	}

	// open device
	rc = libusb_open(brick->device, &brick->device_handle);

	if (rc < 0) {
		log_error("Could not open USB device (bus %d, device %d): %s (%d)",
		          brick->bus_number, brick->device_address,
		          get_libusb_error_name(rc), rc);

		brick_destroy(brick); // FIXME: don't use

		return -1;
	}

	// reset device
	rc = libusb_reset_device(brick->device_handle);

	if (rc < 0) {
		log_error("Could not reset USB device (bus %d, device %d): %s (%d)",
		          brick->bus_number, brick->device_address,
		          get_libusb_error_name(rc), rc);

		brick_destroy(brick); // FIXME: don't use

		return -1;
	}

	// set device configuration
	rc = libusb_set_configuration(brick->device_handle, USB_CONFIGURATION);

	if (rc < 0) {
		log_error("Could set USB device configuration (bus %d, device %d): %s (%d)",
		          brick->bus_number, brick->device_address,
		          get_libusb_error_name(rc), rc);

		brick_destroy(brick); // FIXME: don't use

		return -1;
	}

	// claim device interface
	rc = libusb_claim_interface(brick->device_handle, USB_INTERFACE);

	if (rc < 0) {
		log_error("Could not claim USB device interface (bus %d, device %d): %s (%d)",
		          brick->bus_number, brick->device_address,
		          get_libusb_error_name(rc), rc);

		brick_destroy(brick); // FIXME: don't use

		return -1;
	}

	// get product string descriptor
	rc = libusb_get_string_descriptor_ascii(brick->device_handle,
	                                        brick->device_descriptor.iProduct,
	                                        (unsigned char *)brick->product,
	                                        sizeof(brick->product));

	if (rc < 0) {
		log_error("Could not get product string descriptor for USB device (bus %d, device %d): %s (%d)",
		          brick->bus_number, brick->device_address,
		          get_libusb_error_name(rc), rc);

		brick_destroy(brick); // FIXME: don't use

		return -1;
	}

	// get serial number string descriptor
	rc = libusb_get_string_descriptor_ascii(brick->device_handle,
	                                        brick->device_descriptor.iSerialNumber,
	                                        (unsigned char *)brick->serial_number,
	                                        sizeof(brick->serial_number));

	if (rc < 0) {
		log_error("Could not get serial number string descriptor for USB device (bus %d, device %d): %s (%d)",
		          brick->bus_number, brick->device_address,
		          get_libusb_error_name(rc), rc);

		brick_destroy(brick); // FIXME: don't use

		return -1;
	}

	// allocate and submit read transfers
	if (array_create(&brick->read_transfers, 5, sizeof(Transfer)) < 0) {
		log_error("Could not create read transfer array: %s (%d)",
		          get_errno_name(errno), errno);

		brick_destroy(brick); // FIXME: don't use

		return -1;
	}

	for (i = 0; i < 5; ++i) {
		// FIXME: need to destroy already created, transfers in case of an error
		transfer = array_append(&brick->read_transfers);

		if (transfer == NULL) {
			log_error("Could not append to the read transfer array: %s (%d)",
			          get_errno_name(errno), errno);

			brick_destroy(brick); // FIXME: don't use

			return -1;
		}

		if (transfer_create(transfer, brick, TRANSFER_READ,
		                    read_transfer_callback) < 0) {
			brick_destroy(brick); // FIXME: don't use

			return -1;
		}

		if (transfer_submit(transfer) < 0) {
			brick_destroy(brick); // FIXME: don't use

			return -1;
		}
	}

	// allocate write transfers
	if (array_create(&brick->write_transfers, 5, sizeof(Transfer)) < 0) {
		log_error("Could not create write transfer array: %s (%d)",
		          get_errno_name(errno), errno);

		brick_destroy(brick); // FIXME: don't use

		return -1;
	}

	for (i = 0; i < 5; ++i) {
		// FIXME: need to destroy already created, transfers in case of an error
		transfer = array_append(&brick->write_transfers);

		if (transfer == NULL) {
			log_error("Could not append to the write transfer array: %s (%d)",
			          get_errno_name(errno), errno);

			brick_destroy(brick); // FIXME: don't use

			return -1;
		}

		if (transfer_create(transfer, brick, TRANSFER_WRITE, NULL) < 0) {
			brick_destroy(brick); // FIXME: don't use

			return -1;
		}
	}

	if (array_create(&brick->uids, 32, sizeof(uint32_t)) < 0) {
		log_error("Could not create UID array: %s (%d)",
		          get_errno_name(errno), errno);

		brick_destroy(brick); // FIXME: don't use

		return -1;
	}

	return 0;
}

void brick_destroy(Brick *brick) {
	// FIXME: free uids array
	// FIXME: cancel and free transfers

	if (brick->device != NULL) {
		libusb_unref_device(brick->device);
		brick->device = NULL;
	}

	if (brick->device_handle != NULL) {
		libusb_close(brick->device_handle);
		brick->device_handle = NULL;
	}

	/*if (brick->context != NULL) {
		libusb_exit(brick->context);
		brick->context = NULL;
	}*/
}

int brick_add_uid(Brick *brick, uint32_t uid) {
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

int brick_has_uid(Brick *brick, uint32_t uid) {
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
