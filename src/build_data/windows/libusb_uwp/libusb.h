/*
 * brickd
 * Copyright (C) 2016-2017 Matthias Bolte <matthias@tinkerforge.com>
 *
 * libusb.h: Emulating libusb API for Universal Windows Platform
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

#ifndef BRICKD_LIBUSB_H
#define BRICKD_LIBUSB_H

#include <stdint.h>
#include <windows.h>

#ifdef interface
#undef interface
#endif

typedef int ssize_t;

#ifdef __cplusplus
extern "C" {
#endif

#define LIBUSB_ENDPOINT_DIR_MASK 0x80

enum libusb_endpoint_direction {
	LIBUSB_ENDPOINT_IN = 0x80,
	LIBUSB_ENDPOINT_OUT = 0x00
};

struct libusb_device_descriptor {
	uint16_t idVendor;
	uint16_t idProduct;
	uint16_t bcdDevice;
	uint8_t iManufacturer;
	uint8_t iProduct;
	uint8_t iSerialNumber;
};

struct libusb_endpoint_descriptor {
	uint8_t bEndpointAddress;
};

struct libusb_interface_descriptor {
	uint8_t bInterfaceNumber;
	uint8_t bNumEndpoints;
	const struct libusb_endpoint_descriptor *endpoint;
};

struct libusb_interface {
	const struct libusb_interface_descriptor *altsetting;
	int num_altsetting;
};

struct libusb_config_descriptor {
	uint8_t bNumInterfaces;
	const struct libusb_interface *interface;
};

typedef struct _libusb_context libusb_context;
typedef struct _libusb_device libusb_device;
typedef struct _libusb_device_handle libusb_device_handle;

enum libusb_error {
	LIBUSB_SUCCESS = 0,
	LIBUSB_ERROR_IO = -1,
	LIBUSB_ERROR_INVALID_PARAM = -2,
	LIBUSB_ERROR_ACCESS = -3,
	LIBUSB_ERROR_NO_DEVICE = -4,
	LIBUSB_ERROR_NOT_FOUND = -5,
	LIBUSB_ERROR_BUSY = -6,
	LIBUSB_ERROR_TIMEOUT = -7,
	LIBUSB_ERROR_OVERFLOW = -8,
	LIBUSB_ERROR_PIPE = -9,
	LIBUSB_ERROR_INTERRUPTED = -10,
	LIBUSB_ERROR_NO_MEM = -11,
	LIBUSB_ERROR_NOT_SUPPORTED = -12,
	LIBUSB_ERROR_OTHER = -99,
};

enum libusb_transfer_status {
	LIBUSB_TRANSFER_COMPLETED,
	LIBUSB_TRANSFER_ERROR,
	LIBUSB_TRANSFER_TIMED_OUT,
	LIBUSB_TRANSFER_CANCELLED,
	LIBUSB_TRANSFER_STALL,
	LIBUSB_TRANSFER_NO_DEVICE,
	LIBUSB_TRANSFER_OVERFLOW,
};

enum libusb_transfer_type {
	LIBUSB_TRANSFER_TYPE_CONTROL = 0,
	LIBUSB_TRANSFER_TYPE_ISOCHRONOUS = 1,
	LIBUSB_TRANSFER_TYPE_BULK = 2,
	LIBUSB_TRANSFER_TYPE_INTERRUPT = 3,
	LIBUSB_TRANSFER_TYPE_BULK_STREAM = 4
};

struct libusb_transfer;

typedef void (*libusb_transfer_callback)(struct libusb_transfer *transfer);

struct libusb_transfer {
	libusb_device_handle *dev_handle;
	unsigned char endpoint;
	unsigned char type;
	unsigned int timeout;
	enum libusb_transfer_status status;
	int length;
	int actual_length;
	libusb_transfer_callback callback;
	void *user_data;
	unsigned char *buffer;
};

enum libusb_log_level {
	LIBUSB_LOG_LEVEL_NONE = 0,
	LIBUSB_LOG_LEVEL_ERROR,
	LIBUSB_LOG_LEVEL_WARNING,
	LIBUSB_LOG_LEVEL_INFO,
	LIBUSB_LOG_LEVEL_DEBUG,
};

struct libusb_pollfd {
	int fd;
	short events;
};

typedef void (*libusb_pollfd_added_callback)(int fd, short events, void *user_data);
typedef void (*libusb_pollfd_removed_callback)(int fd, void *user_data);

typedef void (*libusb_log_callback)(libusb_context *ctx, enum libusb_log_level level,
                                    const char *function, const char *format,
                                    va_list args);

int libusb_init(libusb_context **ctx);
void libusb_exit(libusb_context *ctx);
void libusb_set_debug(libusb_context *ctx, int level);

ssize_t libusb_get_device_list(libusb_context *ctx, libusb_device ***list);
void libusb_free_device_list(libusb_device **list, int unref_devices);
libusb_device *libusb_ref_device(libusb_device *dev);
void libusb_unref_device(libusb_device *dev);

int libusb_get_device_descriptor(libusb_device *dev,
                                 struct libusb_device_descriptor *desc);
int libusb_get_config_descriptor(libusb_device *dev, uint8_t config_index,
                                 struct libusb_config_descriptor **config);
void libusb_free_config_descriptor(struct libusb_config_descriptor *config);

uint8_t libusb_get_bus_number(libusb_device *dev);
uint8_t libusb_get_device_address(libusb_device *dev);

int libusb_open(libusb_device *dev, libusb_device_handle **handle);
void libusb_close(libusb_device_handle *dev_handle);
libusb_device *libusb_get_device(libusb_device_handle *dev_handle);

int libusb_claim_interface(libusb_device_handle *dev, int interface_number);
int libusb_release_interface(libusb_device_handle *dev, int interface_number);

struct libusb_transfer *libusb_alloc_transfer(int iso_packets);
int libusb_submit_transfer(struct libusb_transfer *transfer);
int libusb_cancel_transfer(struct libusb_transfer *transfer);
void libusb_free_transfer(struct libusb_transfer *transfer);
void libusb_fill_bulk_transfer(struct libusb_transfer *transfer,
                               libusb_device_handle *dev_handle,
                               unsigned char endpoint, unsigned char *buffer,
                               int length, libusb_transfer_callback callback,
                               void *user_data, unsigned int timeout);

int libusb_get_string_descriptor_ascii(libusb_device_handle *dev_handle,
                                       uint8_t desc_index, unsigned char *data,
                                       int length);

int libusb_handle_events_timeout(libusb_context *ctx, struct timeval *tv);
int libusb_pollfds_handle_timeouts(libusb_context *ctx);

const struct libusb_pollfd **libusb_get_pollfds(libusb_context *ctx);
void libusb_free_pollfds(const struct libusb_pollfd **pollfds);
void libusb_set_pollfd_notifiers(libusb_context *ctx,
                                 libusb_pollfd_added_callback added_callback,
                                 libusb_pollfd_removed_callback removed_callback,
                                 void *user_data);

void libusb_set_log_callback(libusb_log_callback callback);

#ifdef __cplusplus
}
#endif

#endif // BRICKD_LIBUSB_H
