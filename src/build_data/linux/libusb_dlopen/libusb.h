/*
 * brickd
 * Copyright (C) 2017, 2019-2021 Matthias Bolte <matthias@tinkerforge.com>
 *
 * libusb.h: dlopen wrapper for libusb API
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
#include <stdlib.h>
#include <sys/types.h> // for ssize_t
#include <sys/time.h> // for struct timeval

#define LIBUSB_API_VERSION 0x01000104 // 1.0.20

#define LIBUSB_ENDPOINT_DIR_MASK 0x80

enum libusb_endpoint_direction {
	LIBUSB_ENDPOINT_IN = 0x80,
	LIBUSB_ENDPOINT_OUT = 0x00
};

struct libusb_device_descriptor {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint16_t bcdUSB;
	uint8_t bDeviceClass;
	uint8_t bDeviceSubClass;
	uint8_t bDeviceProtocol;
	uint8_t bMaxPacketSize0;
	uint16_t idVendor;
	uint16_t idProduct;
	uint16_t bcdDevice;
	uint8_t iManufacturer;
	uint8_t iProduct;
	uint8_t iSerialNumber;
	uint8_t bNumConfigurations;
};

struct libusb_endpoint_descriptor {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint8_t bEndpointAddress;
	uint8_t bmAttributes;
	uint16_t wMaxPacketSize;
	uint8_t bInterval;
	uint8_t bRefresh;
	uint8_t bSynchAddress;
	const unsigned char *extra;
	int extra_length;
};

struct libusb_interface_descriptor {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint8_t bInterfaceNumber;
	uint8_t bAlternateSetting;
	uint8_t bNumEndpoints;
	uint8_t bInterfaceClass;
	uint8_t bInterfaceSubClass;
	uint8_t bInterfaceProtocol;
	uint8_t iInterface;
	const struct libusb_endpoint_descriptor *endpoint;
	const unsigned char *extra;
	int extra_length;
};

struct libusb_interface {
	const struct libusb_interface_descriptor *altsetting;
	int num_altsetting;
};

struct libusb_config_descriptor {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint16_t wTotalLength;
	uint8_t bNumInterfaces;
	uint8_t bConfigurationValue;
	uint8_t iConfiguration;
	uint8_t bmAttributes;
	uint8_t MaxPower;
	const struct libusb_interface *interface;
	const unsigned char *extra;
	int extra_length;
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
	LIBUSB_ERROR_OTHER = -99
};

enum libusb_transfer_status {
	LIBUSB_TRANSFER_COMPLETED,
	LIBUSB_TRANSFER_ERROR,
	LIBUSB_TRANSFER_TIMED_OUT,
	LIBUSB_TRANSFER_CANCELLED,
	LIBUSB_TRANSFER_STALL,
	LIBUSB_TRANSFER_NO_DEVICE,
	LIBUSB_TRANSFER_OVERFLOW
};

enum libusb_transfer_type {
	LIBUSB_TRANSFER_TYPE_CONTROL = 0,
	LIBUSB_TRANSFER_TYPE_ISOCHRONOUS = 1,
	LIBUSB_TRANSFER_TYPE_BULK = 2,
	LIBUSB_TRANSFER_TYPE_INTERRUPT = 3
};

struct libusb_iso_packet_descriptor {
	unsigned int length;
	unsigned int actual_length;
	enum libusb_transfer_status status;
};

struct libusb_transfer;

typedef void (*libusb_transfer_callback)(struct libusb_transfer *transfer);

struct libusb_transfer {
	libusb_device_handle *dev_handle;
	uint8_t flags;
	unsigned char endpoint;
	unsigned char type;
	unsigned int timeout;
	enum libusb_transfer_status status;
	int length;
	int actual_length;
	libusb_transfer_callback callback;
	void *user_data;
	unsigned char *buffer;
	int num_iso_packets;
	struct libusb_iso_packet_descriptor iso_packet_desc
#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 199901L)
	[] // valid C99 code
#else
	[0] // non-standard, but usually working code
#endif
	;
};

enum libusb_capability {
	LIBUSB_CAP_HAS_CAPABILITY = 0x0000,
	LIBUSB_CAP_HAS_HOTPLUG = 0x0001,
	LIBUSB_CAP_HAS_HID_ACCESS = 0x0100,
	LIBUSB_CAP_SUPPORTS_DETACH_KERNEL_DRIVER = 0x0101
};

enum libusb_log_level {
	LIBUSB_LOG_LEVEL_NONE = 0,
	LIBUSB_LOG_LEVEL_ERROR,
	LIBUSB_LOG_LEVEL_WARNING,
	LIBUSB_LOG_LEVEL_INFO,
	LIBUSB_LOG_LEVEL_DEBUG
};

enum libusb_log_cb_mode { // 1.0.23
	LIBUSB_LOG_CB_GLOBAL = 1 << 0,
	LIBUSB_LOG_CB_CONTEXT = 1 << 1
};

struct libusb_pollfd {
	int fd;
	short events;
};

typedef int libusb_hotplug_callback_handle;

typedef enum {
	LIBUSB_HOTPLUG_NO_FLAGS = 0,
	LIBUSB_HOTPLUG_ENUMERATE = 1 << 0,
} libusb_hotplug_flag;

typedef enum {
	LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED = 0x01,
	LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT = 0x02,
} libusb_hotplug_event;

#define LIBUSB_HOTPLUG_MATCH_ANY -1

typedef void (*libusb_log_cb)(libusb_context *ctx, enum libusb_log_level level, const char *str); // 1.0.23

typedef int (*libusb_hotplug_callback_fn)(libusb_context *ctx, libusb_device *device, libusb_hotplug_event event, void *user_data);

typedef void (*libusb_pollfd_added_callback)(int fd, short events, void *user_data);
typedef void (*libusb_pollfd_removed_callback)(int fd, void *user_data);

typedef int (*libusb_init_t)(libusb_context **ctx);
typedef void (*libusb_exit_t)(libusb_context *ctx);
typedef void (*libusb_set_debug_t)(libusb_context *ctx, int level);
typedef void (*libusb_set_log_cb_t)(libusb_context *ctx, libusb_log_cb cb, int mode); // 1.0.23
typedef int (*libusb_has_capability_t)(uint32_t capability);

typedef ssize_t (*libusb_get_device_list_t)(libusb_context *ctx, libusb_device ***list);
typedef void (*libusb_free_device_list_t)(libusb_device **list, int unref_devices);
typedef libusb_device *(*libusb_ref_device_t)(libusb_device *dev);
typedef void (*libusb_unref_device_t)(libusb_device *dev);

typedef int (*libusb_get_device_descriptor_t)(libusb_device *dev, struct libusb_device_descriptor *desc);
typedef int (*libusb_get_config_descriptor_t)(libusb_device *dev, uint8_t config_index, struct libusb_config_descriptor **config);
typedef void (*libusb_free_config_descriptor_t)(struct libusb_config_descriptor *config);

typedef uint8_t (*libusb_get_bus_number_t)(libusb_device *dev);
typedef uint8_t (*libusb_get_device_address_t)(libusb_device *dev);

typedef int (*libusb_open_t)(libusb_device *dev, libusb_device_handle **handle);
typedef void (*libusb_close_t)(libusb_device_handle *dev_handle);
typedef libusb_device *(*libusb_get_device_t)(libusb_device_handle *dev_handle);

typedef int (*libusb_claim_interface_t)(libusb_device_handle *dev, int interface_number);
typedef int (*libusb_release_interface_t)(libusb_device_handle *dev, int interface_number);

typedef int (*libusb_clear_halt_t)(libusb_device_handle *dev_handle, unsigned char endpoint);

typedef struct libusb_transfer *(*libusb_alloc_transfer_t)(int iso_packets);
typedef int (*libusb_submit_transfer_t)(struct libusb_transfer *transfer);
typedef int (*libusb_cancel_transfer_t)(struct libusb_transfer *transfer);
typedef void (*libusb_free_transfer_t)(struct libusb_transfer *transfer);

typedef int (*libusb_get_string_descriptor_ascii_t)(libusb_device_handle *dev_handle, uint8_t desc_index, unsigned char *data, int length);

typedef int (*libusb_handle_events_timeout_t)(libusb_context *ctx, struct timeval *tv);

typedef const struct libusb_pollfd **(*libusb_get_pollfds_t)(libusb_context *ctx);
typedef void (*libusb_free_pollfds_t)(const struct libusb_pollfd **pollfds);
typedef void (*libusb_set_pollfd_notifiers_t)(libusb_context *ctx, libusb_pollfd_added_callback added_callback, libusb_pollfd_removed_callback removed_callback, void *user_data);

typedef int (*libusb_hotplug_register_callback_t)(libusb_context *ctx, int events, int flags, int vendor_id, int product_id, int dev_class, libusb_hotplug_callback_fn cb_fn, void *user_data, libusb_hotplug_callback_handle *callback_handle);
typedef void (*libusb_hotplug_deregister_callback_t)(libusb_context *ctx, libusb_hotplug_callback_handle callback_handle);

extern libusb_init_t libusb_init;
extern libusb_exit_t libusb_exit;
extern libusb_set_debug_t libusb_set_debug;
extern libusb_set_log_cb_t libusb_set_log_cb; // 1.0.23
extern libusb_has_capability_t libusb_has_capability;

extern libusb_get_device_list_t libusb_get_device_list;
extern libusb_free_device_list_t libusb_free_device_list;
extern libusb_ref_device_t libusb_ref_device;
extern libusb_unref_device_t libusb_unref_device;

extern libusb_get_device_descriptor_t libusb_get_device_descriptor;
extern libusb_get_config_descriptor_t libusb_get_config_descriptor;
extern libusb_free_config_descriptor_t libusb_free_config_descriptor;

extern libusb_get_bus_number_t libusb_get_bus_number;
extern libusb_get_device_address_t libusb_get_device_address;

extern libusb_open_t libusb_open;
extern libusb_close_t libusb_close;
extern libusb_get_device_t libusb_get_device;

extern libusb_claim_interface_t libusb_claim_interface;
extern libusb_release_interface_t libusb_release_interface;

extern libusb_clear_halt_t libusb_clear_halt;

extern libusb_alloc_transfer_t libusb_alloc_transfer;
extern libusb_submit_transfer_t libusb_submit_transfer;
extern libusb_cancel_transfer_t libusb_cancel_transfer;
extern libusb_free_transfer_t libusb_free_transfer;

static inline void libusb_fill_bulk_transfer(struct libusb_transfer *transfer,
                                             libusb_device_handle *dev_handle,
                                             unsigned char endpoint,
                                             unsigned char *buffer, int length,
                                             libusb_transfer_callback callback,
                                             void *user_data, unsigned int timeout)
{
	transfer->dev_handle = dev_handle;
	transfer->endpoint = endpoint;
	transfer->type = LIBUSB_TRANSFER_TYPE_BULK;
	transfer->timeout = timeout;
	transfer->buffer = buffer;
	transfer->length = length;
	transfer->user_data = user_data;
	transfer->callback = callback;
}

extern libusb_get_string_descriptor_ascii_t libusb_get_string_descriptor_ascii;

extern libusb_handle_events_timeout_t libusb_handle_events_timeout;

extern libusb_get_pollfds_t libusb_get_pollfds;
extern libusb_free_pollfds_t libusb_free_pollfds;
extern libusb_set_pollfd_notifiers_t libusb_set_pollfd_notifiers;

extern libusb_hotplug_register_callback_t libusb_hotplug_register_callback;
extern libusb_hotplug_deregister_callback_t libusb_hotplug_deregister_callback;

int libusb_dlopen(void);
void libusb_dlclose(void);

#endif // BRICKD_LIBUSB_H
