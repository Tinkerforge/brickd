/*
 * brickd
 * Copyright (C) 2018 Matthias Bolte <matthias@tinkerforge.com>
 *
 * libusb_android.cpp: Emulating libusb API for Android
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

// https://developer.android.com/guide/topics/connectivity/usb/host

#include <string>
#include <map>
#include <jni.h>
#include <errno.h>
#include <unistd.h>
#include <poll.h>
#include <stdbool.h>
#include <sys/ioctl.h>

extern "C" {

#include <daemonlib/macros.h>
#include <daemonlib/node.h>
#include <daemonlib/utils.h>

}

#include "libusb.h"

#define USBI_STRING_MANUFACTURER 1
#define USBI_STRING_PRODUCT 2
#define USBI_STRING_SERIAL_NUMBER 3

#define USBI_DESCRIPTOR_TYPE_STRING 0x03

#define USBI_USBFS_URB_TYPE_BULK 3

typedef struct  {
	unsigned int length;
	unsigned int actual_length;
	unsigned int status;
} usbfs_iso_packet_desc;

typedef struct {
	unsigned char type;
	unsigned char endpoint;
	int status;
	unsigned int flags;
	void *buffer;
	int buffer_length;
	int actual_length;
	int start_frame;
	union {
		int number_of_packets;
		unsigned int stream_id;
	};
	int error_count;
	unsigned int signr;
	void *user_context;
	usbfs_iso_packet_desc iso_frame_desc[0];
} usbfs_urb;

typedef struct {
	uint8_t bmRequestType;
	uint8_t bRequest;
	uint16_t wValue;
	uint16_t wIndex;
	uint16_t wLength;
	uint32_t timeout; // in milliseconds
	void *data;
} usbfs_control_transfer;

#define IOCTL_USBFS_CONTROL _IOWR('U', 0, usbfs_control_transfer)
#define IOCTL_USBFS_CLAIMINTF _IOR('U', 15, unsigned int)
#define IOCTL_USBFS_RELEASEINTF _IOR('U', 16, unsigned int)
#define IOCTL_USBFS_SUBMITURB _IOR('U', 10, usbfs_urb)
#define IOCTL_USBFS_DISCARDURB _IO('U', 11)
#define IOCTL_USBFS_REAPURBNDELAY _IOW('U', 13, void *)

typedef struct {
	struct libusb_device_descriptor device;
	struct libusb_config_descriptor config;
} usbi_descriptor;

typedef struct {
	struct libusb_transfer transfer;
	usbfs_urb urb;
	bool submitted;
} usbi_transfer;

struct _libusb_device {
	Node node;
	libusb_context *ctx;
	int ref_count;
	char *name;
	jobject device; // UsbDevice
	uint8_t bus_number;
	uint8_t device_address;
	usbi_descriptor descriptor;
};

struct _libusb_context {
	Node dev_handle_sentinel;
	int dev_handle_count;
	libusb_pollfd_added_callback pollfd_added_callback;
	libusb_pollfd_removed_callback pollfd_removed_callback;
	void *pollfd_user_data;
};

struct _libusb_device_handle {
	Node node;
	libusb_device *dev;
	struct libusb_pollfd pollfd;
};

static libusb_log_callback _log_callback;

static std::map<std::string, uint16_t> _fake_device_addresses;

JNIEnv *android_env = NULL;
jobject android_service = NULL;

// NOTE: assumes _log_callback is not nullptr
static void usbi_log_message(libusb_context *ctx, enum libusb_log_level level,
                             const char *function, const char *format, ...) {
	va_list args;

	va_start(args, format);

	_log_callback(ctx, level, function, format, args);

	va_end(args);
}

#define usbi_log_message_checked(ctx, level, ...) \
	do { \
		if (_log_callback != nullptr) { \
			usbi_log_message(ctx, level, __FUNCTION__, __VA_ARGS__); \
		} \
	} while (0)

#define usbi_log_error(ctx, ...)   usbi_log_message_checked(ctx, LIBUSB_LOG_LEVEL_ERROR, __VA_ARGS__)
#define usbi_log_warning(ctx, ...) usbi_log_message_checked(ctx, LIBUSB_LOG_LEVEL_WARNING, __VA_ARGS__)
#define usbi_log_info(ctx, ...)    usbi_log_message_checked(ctx, LIBUSB_LOG_LEVEL_INFO, __VA_ARGS__)
#define usbi_log_debug(ctx, ...)   usbi_log_message_checked(ctx, LIBUSB_LOG_LEVEL_DEBUG, __VA_ARGS__)

static char *usbi_string_convert_ascii(jstring string) {
	int length = android_env->GetStringLength(string);
	const jchar *data;
	int i;
	char *ascii = (char *)calloc(length + 1, 1);

	if (ascii == nullptr) {
		errno = ENOMEM;

		return nullptr;
	}

	data = android_env->GetStringChars(string, NULL); // FIXME: could this fail?

	for (i = 0; i < length; ++i) {
		if (data[i] < 32 || data[i] > 126) {
			ascii[i] = '?';
		} else {
			ascii[i] = (char) data[i];
		}
	}

	android_env->ReleaseStringChars(string, data);

	return ascii;
}

static jobject usbi_get_object_field(jobject obj, const char *name, const char *type, bool add_ref) {
	jclass cls = android_env->GetObjectClass(obj); // FIXME: check result
	jfieldID fid = android_env->GetFieldID(cls, name, type); // FIXME: check result
	jobject result = android_env->GetObjectField(obj, fid); // FIXME: check result

	if (add_ref) {
		result = android_env->NewGlobalRef(result); // FIXME: check result
	}

	return result;
}

static void usbi_get_int_field(jobject obj, const char *name, int *result) {
	jclass cls = android_env->GetObjectClass(obj); // FIXME: check result
	jfieldID fid = android_env->GetFieldID(cls, name, "I"); // FIXME: check result

	*result = android_env->GetIntField(obj, fid);
}

static char *usbi_get_string_field(jobject obj, const char *name) {
	jclass cls = android_env->GetObjectClass(obj); // FIXME: check result
	jfieldID fid = android_env->GetFieldID(cls, name, "Ljava/lang/String;"); // FIXME: check result
	jstring string = (jstring)android_env->GetObjectField(obj, fid); // FIXME: check result

	return usbi_string_convert_ascii(string);
}

static void usbi_get_fake_device_address(const char *name, uint8_t *bus_number,
                                         uint8_t *device_address) {
	auto iter = _fake_device_addresses.find(name);
	uint16_t value;

	if (iter != _fake_device_addresses.end()) {
		value = iter->second;
	} else {
		// FIXME: after 65536 different IDs this will start to reuse bus numbers
		//        and device addresses. this will probably never be a problem
		value = (uint16_t)(_fake_device_addresses.size() % 0xFFFF);

		_fake_device_addresses[name] = value;
	}

	*bus_number = (uint8_t)((value >> 8) & 0xFF);
	*device_address = (uint8_t)(value & 0xFF);
}

static void usbi_free_interface_descriptor(struct libusb_config_descriptor *config) {
	int i;
	const struct libusb_interface *interface;
	int a;
	const struct libusb_interface_descriptor *descriptor;

	for (i = 0; i < config->bNumInterfaces; ++i) {
		interface = &config->interface[i];

		for (a = 0; a < interface->num_altsetting; ++a) {
			descriptor = &interface->altsetting[a];

			free((void *)descriptor->endpoint);
		}

		free((void *)interface->altsetting);
	}

	free((void *)config->interface);
}

static int usbi_get_config_descriptor(libusb_context *ctx, jobject device_info,
                                      struct libusb_config_descriptor *config) {
	int num_interfaces;
	unsigned int i;
	jobjectArray interface_infos;
	jobject interface_info;
	struct libusb_interface *iface;
	int numEndpoints;
	struct libusb_interface_descriptor *desc;
	unsigned int e;
	jintArray endpoint_addresses;
	jint *endpoint_addresses_elements;
	struct libusb_endpoint_descriptor *endpoint;

	usbi_get_int_field(device_info, "numInterfaces", &num_interfaces); // FIXME: check result

	config->bNumInterfaces = (uint8_t)num_interfaces;
	config->interface = (struct libusb_interface *)calloc(config->bNumInterfaces,
	                                                      sizeof(struct libusb_interface));

	if (config->interface == nullptr) {
		usbi_log_error(ctx, "Could not allocate interface");

		return LIBUSB_ERROR_NO_MEM;
	}

	// FIXME: check result
	interface_infos = (jobjectArray)usbi_get_object_field(device_info, "interfaceInfos",
	                                                      "[Lcom/tinkerforge/brickd/USBInterfaceInfo;", false);

	for (i = 0; i < config->bNumInterfaces; ++i) {
		interface_info = android_env->GetObjectArrayElement(interface_infos, i);// FIXME: check result

		iface = (struct libusb_interface *)&config->interface[i];
		iface->num_altsetting = 1;
		iface->altsetting = (struct libusb_interface_descriptor *)calloc(iface->num_altsetting,
		                                                                 sizeof(struct libusb_interface_descriptor));

		if (iface->altsetting == nullptr) {
			usbi_log_error(ctx, "Could not allocate interface descriptor");

			usbi_free_interface_descriptor(config);

			return LIBUSB_ERROR_NO_MEM;
		}

		usbi_get_int_field(interface_info, "numEndpoints", &numEndpoints); // FIXME: check result

		desc = (struct libusb_interface_descriptor *)iface->altsetting;
		desc->bInterfaceNumber = 0;
		desc->bNumEndpoints = (uint8_t)numEndpoints;
		desc->endpoint = (struct libusb_endpoint_descriptor *)calloc(desc->bNumEndpoints,
		                                                             sizeof(struct libusb_interface_descriptor));

		if (desc->endpoint == nullptr) {
			usbi_log_error(ctx, "Could not allocate endpoint descriptor");

			usbi_free_interface_descriptor(config);

			return LIBUSB_ERROR_NO_MEM;
		}

		// FIXME: check result
		endpoint_addresses = (jintArray)usbi_get_object_field(interface_info, "endpointAddresses", "[I", false);
		endpoint_addresses_elements = android_env->GetIntArrayElements(endpoint_addresses, nullptr); // FIXME: check result

		for (e = 0; e < desc->bNumEndpoints; ++e) {
			endpoint = (struct libusb_endpoint_descriptor *)&desc->endpoint[e];
			endpoint->bEndpointAddress = (uint8_t)endpoint_addresses_elements[e];
		}

		android_env->ReleaseIntArrayElements(endpoint_addresses, endpoint_addresses_elements, 0);
	}

	return LIBUSB_SUCCESS;
}

static int usbi_get_descriptor(libusb_context *ctx, jobject device_info,
                               usbi_descriptor *descriptor) {
	int rc;
	int vendor_id;
	int product_id;

	usbi_get_int_field(device_info, "vendorID", &vendor_id); // FIXME: check result
	usbi_get_int_field(device_info, "productID", &product_id); // FIXME: check result

	descriptor->device.idVendor = (uint16_t)vendor_id;
	descriptor->device.idProduct = (uint16_t)product_id;
	descriptor->device.bcdDevice = 0x0110; // FIXME: Android doesn't provide this information
	descriptor->device.iManufacturer = USBI_STRING_MANUFACTURER;
	descriptor->device.iProduct = USBI_STRING_PRODUCT;
	descriptor->device.iSerialNumber = USBI_STRING_SERIAL_NUMBER;

	rc = usbi_get_config_descriptor(ctx, device_info, &descriptor->config);

	if (rc < 0) {
		return rc;
	}

	return LIBUSB_SUCCESS;
}

static int usbi_create_device(libusb_context *ctx, jobject device_info, libusb_device **dev_ptr) {
	libusb_device *dev = (libusb_device *)calloc(1, sizeof(libusb_device));
	int rc;

	if (dev == nullptr) {
		usbi_log_error(ctx, "Could not allocate device");

		return LIBUSB_ERROR_NO_MEM;
	}

	node_reset(&dev->node);

	dev->ctx = ctx;
	dev->ref_count = 1;
	dev->device = usbi_get_object_field(device_info, "device", "Landroid/hardware/usb/UsbDevice;", true); // FIXME: check result
	dev->name = usbi_get_string_field(device_info, "name");

	if (dev->name == nullptr) {
		usbi_log_error(ctx, "Could not get device name");

		android_env->DeleteGlobalRef(dev->device);
		free(dev);

		return LIBUSB_ERROR_NO_MEM;
	}

	usbi_get_fake_device_address(dev->name, &dev->bus_number, &dev->device_address); // FIXME

	rc = usbi_get_descriptor(ctx, device_info, &dev->descriptor);

	if (rc < 0) {
		free(dev->name);
		free(dev);

		return rc;
	}

	usbi_log_debug(ctx, "Created device %p (context: %p, name: %s)",
	               dev, ctx, dev->name);

	*dev_ptr = dev;

	return LIBUSB_SUCCESS;
}

static void usbi_free_device(libusb_device *dev) {
	libusb_context *ctx = dev->ctx;

	usbi_log_debug(ctx, "Destroying device %p (context: %p, name: %s)",
	               dev, ctx, dev->name);

	usbi_free_interface_descriptor(&dev->descriptor.config);

	android_env->DeleteGlobalRef(dev->device);

	free(dev->name);
	free(dev);
}

static int usbi_get_device_list(libusb_context *ctx, Node *sentinel) {
	int rc;
	jmethodID get_device_list_mid;
	jobjectArray device_infos;
	int device_infos_length;
	int i;
	jobject device_info;
	int vendor_id;
	int product_id;
	libusb_device *dev;
	int length = 0;

	get_device_list_mid = android_env->GetMethodID(android_env->GetObjectClass(android_service), "getDeviceList",
	                                               "()[Lcom/tinkerforge/brickd/USBDeviceInfo;"); // FIXME: check result
	device_infos = (jobjectArray)android_env->CallObjectMethod(android_service, get_device_list_mid);
	device_infos_length = android_env->GetArrayLength(device_infos);

	for (i = 0; i < device_infos_length; ++i) {
		device_info = android_env->GetObjectArrayElement(device_infos, i);

		usbi_get_int_field(device_info, "vendorID", &vendor_id); // FIXME: check result
		usbi_get_int_field(device_info, "productID", &product_id); // FIXME: check result

		if (vendor_id == 0x16D0 && (product_id == 0x063D || product_id == 0x09E5)) {
			rc = usbi_create_device(ctx, device_info, &dev);

			if (rc < 0) {
				return rc;
			}

			node_insert_before(sentinel, &dev->node);
			++length;
		}
	}

	return length;
}

int libusb_init(libusb_context **ctx_ptr) {
	libusb_context *ctx;

	if (ctx_ptr == nullptr) {
		return LIBUSB_ERROR_INVALID_PARAM; // FIXME: no default context support
	}

    ctx = (libusb_context *)calloc(1, sizeof(libusb_context));

	if (ctx == nullptr) {
		return LIBUSB_ERROR_NO_MEM;
	}

	usbi_log_debug(ctx, "Creating context %p", ctx);

	node_reset(&ctx->dev_handle_sentinel);

    ctx->dev_handle_count = 0;

	*ctx_ptr = ctx;

	return LIBUSB_SUCCESS;
}

// NOTE: assumes that no transfers are pending
void libusb_exit(libusb_context *ctx) {
	if (ctx == nullptr) {
		return; // FIXME: no default context support
	}

	usbi_log_debug(ctx, "Destroying context %p", ctx);

	free(ctx);
}

void libusb_set_debug(libusb_context *ctx, int level) {
	(void)ctx;
	(void)level;
}

int libusb_pollfds_handle_timeouts(libusb_context *ctx) {
	(void)ctx;

	return 1;
}

const struct libusb_pollfd **libusb_get_pollfds(libusb_context *ctx) {
	const struct libusb_pollfd **pollfds;
	Node *dev_handle_node;
	libusb_device_handle *dev_handle;
	int i = 0;

	if (ctx == nullptr) {
		return nullptr; // FIXME: no default context support
	}

	pollfds = (const struct libusb_pollfd **)calloc(ctx->dev_handle_count + 1, sizeof(struct libusb_pollfd *));

	if (pollfds == nullptr) {
		return nullptr;
	}

	dev_handle_node = ctx->dev_handle_sentinel.next;

	while (dev_handle_node != &ctx->dev_handle_sentinel) {
		dev_handle = containerof(dev_handle_node, libusb_device_handle, node);

		pollfds[i++] = &dev_handle->pollfd;

		dev_handle_node = dev_handle_node->next;
	}

	pollfds[ctx->dev_handle_count] = nullptr;

	return pollfds;
}

void libusb_free_pollfds(const struct libusb_pollfd **pollfds) {
	free(pollfds);
}

void libusb_set_pollfd_notifiers(libusb_context *ctx,
                                 libusb_pollfd_added_callback added_callback,
                                 libusb_pollfd_removed_callback removed_callback,
                                 void *user_data) {
	ctx->pollfd_added_callback = added_callback;
	ctx->pollfd_removed_callback = removed_callback;
	ctx->pollfd_user_data = user_data;
}

int libusb_handle_events_timeout(libusb_context *ctx, struct timeval *tv) {
	struct pollfd *pollfds;
	int count = 0;
	Node *dev_handle_node;
	libusb_device_handle *dev_handle;
	int i;
	int ready;
	int rc;
	usbfs_urb *urb;
	usbi_transfer *itransfer;
	struct libusb_transfer *transfer;

	if (ctx == nullptr) {
		return LIBUSB_ERROR_INVALID_PARAM; // FIXME: no default context support
	}

	if (tv->tv_sec != 0 || tv->tv_usec != 0) {
		return LIBUSB_ERROR_INVALID_PARAM; // FIXME: no timeout support
	}

	pollfds = (struct pollfd *)calloc(ctx->dev_handle_count, sizeof(struct pollfd));

	if (pollfds == nullptr) {
		return LIBUSB_ERROR_NO_MEM;
	}

	usbi_log_debug(ctx, "Handling events");

	dev_handle_node = ctx->dev_handle_sentinel.next;
	i = 0;

	while (dev_handle_node != &ctx->dev_handle_sentinel) {
		dev_handle = containerof(dev_handle_node, libusb_device_handle, node);

		pollfds[i].fd = dev_handle->pollfd.fd;
		pollfds[i].events = dev_handle->pollfd.events;
		pollfds[i].revents = 0;

		dev_handle_node = dev_handle_node->next;
		++i;
	}

	ready = poll(pollfds, ctx->dev_handle_count, 0);

	if (ready < 0) {
		if (errno_interrupted()) {
			rc = LIBUSB_ERROR_INTERRUPTED;
		} else {
			rc = LIBUSB_ERROR_IO;
		}

		usbi_log_error(ctx, "Count not poll on event source(s): %s (%d)",
		               get_errno_name(errno), errno);

		free(pollfds);

		return rc;
	}

	dev_handle_node = ctx->dev_handle_sentinel.next;
	i = 0;

	while (dev_handle_node != &ctx->dev_handle_sentinel) {
		dev_handle = containerof(dev_handle_node, libusb_device_handle, node);

		if (pollfds[i].revents != 0) {
			if ((pollfds[i].revents & POLLERR) != 0) {
				usbi_log_error(ctx, "POLLERR reported for device %s", // FIXME
				               dev_handle->dev->name);

				continue;
			}

			rc = ioctl(pollfds[i].fd, IOCTL_USBFS_REAPURBNDELAY, &urb);

			if (rc < 0) {
				if (errno_interrupted()) {
					rc = LIBUSB_ERROR_INTERRUPTED;
				} else if (errno == ENODEV) {
					rc = LIBUSB_ERROR_NO_DEVICE;
				} else {
					rc = LIBUSB_ERROR_IO;
				}

				usbi_log_error(ctx, "Count not reap URB for device %s: %s (%d)",
				               dev_handle->dev->name, get_errno_name(errno), errno);

				free(pollfds);

				return rc; // FIXME: make this non-fatal
			}

			itransfer = (usbi_transfer *)urb->user_context;
			transfer = &itransfer->transfer;

			itransfer->submitted = false;

			if (urb->status == -ENOENT) {
				transfer->status = LIBUSB_TRANSFER_CANCELLED;
			} else if (urb->status == -ENODEV || urb->status == -ESHUTDOWN) {
				transfer->status = LIBUSB_TRANSFER_NO_DEVICE;
			} else if (urb->status == -EPIPE) {
				transfer->status = LIBUSB_TRANSFER_STALL;
			} else if (urb->status == -EOVERFLOW) {
				transfer->status = LIBUSB_TRANSFER_OVERFLOW;
			} else if (urb->status != 0) {
				transfer->status = LIBUSB_TRANSFER_ERROR;
			} else {
				transfer->status = LIBUSB_TRANSFER_COMPLETED;
			}

			transfer->actual_length = urb->actual_length;

			usbi_log_debug(ctx, "Triggering callback for %s transfer %p (urb-status: %d)",
			               (LIBUSB_ENDPOINT_IN & transfer->endpoint) != 0 ? "read" : "write",
			               transfer, urb->status);

			transfer->callback(transfer); // might free or submit transfer

			libusb_unref_device(dev_handle->dev);

			++count;
		}

		dev_handle_node = dev_handle_node->next;
		++i;
	}

	free(pollfds);

	usbi_log_debug(ctx, "Handled %d event(s)", count);

	return LIBUSB_SUCCESS;
}

ssize_t libusb_get_device_list(libusb_context *ctx, libusb_device ***list) {
	Node sentinel;
	int length = 0;
	int rc;
	libusb_device **devs;
	Node *dev_node;
	int i;
	libusb_device *dev;
	Node *dev_node_next;

	if (ctx == nullptr) {
		return LIBUSB_ERROR_INVALID_PARAM; // FIXME: no default context support
	}

	node_reset(&sentinel);

	rc = usbi_get_device_list(ctx, &sentinel);

	if (rc < 0) {
		goto error;
	}

	length += rc;

	// create output list
	devs = (libusb_device **)calloc(length + 1, sizeof(libusb_device *));

	if (devs == nullptr) {
		usbi_log_error(ctx, "Could not allocate device list");

		rc = LIBUSB_ERROR_NO_MEM;

		goto error;
	}

	dev_node = sentinel.next;

	for (i = 0; dev_node != &sentinel; ++i) {
		devs[i] = containerof(dev_node, libusb_device, node);
		dev_node = dev_node->next;
	}

	for (i = 0; i < length; ++i) {
		node_reset(&devs[i]->node);
	}

	devs[length] = nullptr;
	*list = devs;

	return length;

error:
	dev_node = sentinel.next;

	while (dev_node != &sentinel) {
		dev = containerof(dev_node, libusb_device, node);
		dev_node_next = dev_node->next;

		libusb_unref_device(dev);

		dev_node = dev_node_next;
	}

	return rc;
}

void libusb_free_device_list(libusb_device **list, int unref_devices) {
	libusb_device *dev;
	int i = 0;

	if (unref_devices) {
		for (dev = list[0]; dev != nullptr; dev = list[++i]) {
			libusb_unref_device(dev);
		}
	}

	free(list);
}

libusb_device *libusb_ref_device(libusb_device *dev) {
	++dev->ref_count;

	return dev;
}

void libusb_unref_device(libusb_device *dev) {
	if (--dev->ref_count == 0) {
		usbi_free_device(dev);
	}
}

int libusb_get_device_descriptor(libusb_device *dev,
                                 struct libusb_device_descriptor *device) {
	if (dev == nullptr || device == nullptr) {
		return LIBUSB_ERROR_INVALID_PARAM;
	}

	memcpy(device, &dev->descriptor.device, sizeof(struct libusb_device_descriptor));

	return LIBUSB_SUCCESS;
}

uint8_t libusb_get_bus_number(libusb_device *dev) {
	return dev->bus_number;
}

uint8_t libusb_get_device_address(libusb_device *dev) {
	return dev->device_address;
}

int libusb_get_config_descriptor(libusb_device *dev, uint8_t config_index,
                                 struct libusb_config_descriptor **config_ptr) {
	if (config_index != 0) {
		return LIBUSB_ERROR_NOT_FOUND;
	}

	*config_ptr = &dev->descriptor.config;

	return LIBUSB_SUCCESS;
}

void libusb_free_config_descriptor(struct libusb_config_descriptor *config) {
	(void)config;

	// nothing to free, because libusb_get_config_descriptor doesn't allocate memory
}

int libusb_open(libusb_device *dev, libusb_device_handle **dev_handle_ptr) {
	libusb_context *ctx = dev->ctx;
	libusb_device_handle *dev_handle;
	jmethodID open_device_mid;
	int fd;

	dev_handle = (libusb_device_handle *)calloc(1, sizeof(libusb_device_handle));

	if (dev_handle == nullptr) {
		usbi_log_error(ctx, "Could not allocate device handle");

		return LIBUSB_ERROR_NO_MEM;
	}

	open_device_mid = android_env->GetMethodID(android_env->GetObjectClass(android_service), "openDevice",
	                                           "(Landroid/hardware/usb/UsbDevice;)I"); // FIXME: check result
	fd = android_env->CallIntMethod(android_service, open_device_mid, dev->device); // FIXME: check result

	if (fd < 0) {
		free(dev_handle);

		return LIBUSB_ERROR_NO_DEVICE;
	}

	dev_handle->dev = libusb_ref_device(dev);
	dev_handle->pollfd.fd = fd;
	dev_handle->pollfd.events = POLLOUT;

	node_insert_before(&ctx->dev_handle_sentinel, &dev_handle->node);
	++ctx->dev_handle_count;

	*dev_handle_ptr = dev_handle;

	usbi_log_debug(ctx, "Opened device %p (context: %p, name: %s, fd: %d)",
	               dev, ctx, dev->name, dev_handle->pollfd.fd);

	if (ctx->pollfd_added_callback != nullptr) {
		ctx->pollfd_added_callback(dev_handle->pollfd.fd, dev_handle->pollfd.events, ctx->pollfd_user_data);
	}

	return LIBUSB_SUCCESS;
}

void libusb_close(libusb_device_handle *dev_handle) {
	libusb_device *dev = dev_handle->dev;
	libusb_context *ctx = dev->ctx;
	jmethodID close_device_mid;

	if (ctx->pollfd_removed_callback != nullptr) {
		ctx->pollfd_removed_callback(dev_handle->pollfd.fd, ctx->pollfd_user_data);
	}

	usbi_log_debug(ctx, "Closing device %p (context: %p, name: %s, fd: %d)",
	               dev, ctx, dev->name, dev_handle->pollfd.fd);

	close_device_mid = android_env->GetMethodID(android_env->GetObjectClass(android_service),
	                                            "closeDevice", "(I)V"); // FIXME: check result
	android_env->CallVoidMethod(android_service, close_device_mid, dev_handle->pollfd.fd); // FIXME: check result

	node_remove(&dev_handle->node);
	--ctx->dev_handle_count;

	libusb_unref_device(dev_handle->dev);

	free(dev_handle);
}

libusb_device *libusb_get_device(libusb_device_handle *dev_handle) {
	return dev_handle->dev;
}

int libusb_get_string_descriptor_ascii(libusb_device_handle *dev_handle,
                                       uint8_t desc_index, unsigned char *data,
                                       int length) {
	libusb_context *ctx = dev_handle->dev->ctx;
	usbfs_control_transfer control;
	int rc;
	unsigned char buffer[255];
	uint16_t language_id;
	int d;
	int s;

	if (desc_index == 0) {
		return LIBUSB_ERROR_INVALID_PARAM;
	}

    control.bmRequestType = 0x81; // 0x81 == direction: in, type: standard, recipient: device
    control.bRequest = 0x06; // 0x06 == get-descriptor
    control.wValue = (uint16_t)(0x03 << 8) | desc_index; // 0x03 == string-descriptor
    control.wIndex = 0; // language ID
    control.wLength = (uint16_t)length;
    control.timeout = 0; // FIXME
    control.data = buffer;

	rc = ioctl(dev_handle->pollfd.fd, IOCTL_USBFS_CONTROL, &control);

	if (rc < 0) {
		if (errno == ENODEV) {
			return LIBUSB_ERROR_NO_DEVICE;
		} else {
			return LIBUSB_ERROR_OTHER;
		}
	}

	if (buffer[1] != USBI_DESCRIPTOR_TYPE_STRING) {
		return LIBUSB_ERROR_IO;
	}

	if (buffer[0] > rc) {
		return LIBUSB_ERROR_IO;
	}

	for (d = 0, s = 2; s < buffer[0]; s += 2) {
		if (d >= length - 1) {
			break;
		}

		if ((buffer[s] & 0x80) != 0 || buffer[s + 1] != 0) {
			data[d++] = '?'; // non-ASCII
		} else {
			data[d++] = buffer[s];
		}
	}

	data[d] = '\0';

	return d;
}

int libusb_claim_interface(libusb_device_handle *dev_handle, int interface_number) {
	libusb_context *ctx = dev_handle->dev->ctx;
	int rc = ioctl(dev_handle->pollfd.fd, IOCTL_USBFS_CLAIMINTF, &interface_number);

	if (rc < 0) {
		if (errno == ENOENT) {
			return LIBUSB_ERROR_NOT_FOUND;
		} else if (errno == EBUSY) {
			return LIBUSB_ERROR_BUSY;
		} else if (errno == ENODEV) {
			return LIBUSB_ERROR_NO_DEVICE;
		} else {
			usbi_log_error(ctx, "Could not claim interface %d: %s (%d)",
			               interface_number, get_errno_name(errno), errno);

			return LIBUSB_ERROR_OTHER;
		}
	}

	return LIBUSB_SUCCESS;
}

int libusb_release_interface(libusb_device_handle *dev_handle, int interface_number) {
	libusb_context *ctx = dev_handle->dev->ctx;
	int rc = ioctl(dev_handle->pollfd.fd, IOCTL_USBFS_RELEASEINTF, &interface_number);

	if (rc < 0) {
		if (errno == ENODEV) {
			return LIBUSB_ERROR_NO_DEVICE;
		} else {
			usbi_log_error(ctx, "Could not release interface %d: %s (%d)",
			               interface_number, get_errno_name(errno), errno);

			return LIBUSB_ERROR_OTHER;
		}
	}

	return LIBUSB_SUCCESS;
}

struct libusb_transfer *libusb_alloc_transfer(int iso_packets) {
	usbi_transfer *itransfer;

	if (iso_packets != 0) {
		return nullptr; // FIXME: no iso transfer support
	}

	itransfer = (usbi_transfer *)calloc(1, sizeof(usbi_transfer));

	if (itransfer == nullptr) {
		return nullptr;
	}

	itransfer->submitted = false;
	itransfer->urb.type = USBI_USBFS_URB_TYPE_BULK;
	itransfer->urb.user_context = itransfer;

	return &itransfer->transfer;
}

int libusb_submit_transfer(struct libusb_transfer *transfer) {
	usbi_transfer *itransfer = (usbi_transfer *)transfer;
	libusb_device_handle *dev_handle = transfer->dev_handle;
	libusb_context *ctx = dev_handle->dev->ctx;
	usbfs_urb *urb = &itransfer->urb;
	int rc;

	if (transfer->type != LIBUSB_TRANSFER_TYPE_BULK ||
		transfer->timeout != 0 || transfer->callback == nullptr) {
		return LIBUSB_ERROR_INVALID_PARAM;
	}

	if (itransfer->submitted) {
		return LIBUSB_ERROR_BUSY;
	}

	libusb_ref_device(dev_handle->dev);

	itransfer->submitted = true;

	urb->status = INT32_MIN;
	urb->endpoint = transfer->endpoint;
	urb->buffer = transfer->buffer;
	urb->buffer_length = transfer->length;

	rc = ioctl(dev_handle->pollfd.fd, IOCTL_USBFS_SUBMITURB, urb);

	if (rc < 0) {
		if (errno == ENODEV) {
			rc = LIBUSB_ERROR_NO_DEVICE;
		} else {
			rc = LIBUSB_ERROR_IO;
		}

		itransfer->submitted = false;

		libusb_unref_device(dev_handle->dev);

		usbi_log_error(ctx, "Could not submit %s transfer %p (length: %d): %s (%d)",
		               (LIBUSB_ENDPOINT_IN & transfer->endpoint) != 0 ? "read" : "write",
		               transfer, transfer->length, get_errno_name(errno), errno);

		free(urb);

		return rc;
	}

	return LIBUSB_SUCCESS;
}

int libusb_cancel_transfer(struct libusb_transfer *transfer) {
	usbi_transfer *itransfer = (usbi_transfer *) transfer;
	libusb_device_handle *dev_handle = transfer->dev_handle;
	libusb_context *ctx = dev_handle->dev->ctx;
	usbfs_urb *urb = &itransfer->urb;
	int rc = ioctl(dev_handle->pollfd.fd, IOCTL_USBFS_DISCARDURB, urb);

	if (rc < 0) {
		if (errno == EINVAL) {
			return LIBUSB_ERROR_NOT_FOUND;
		} else if (errno == ENODEV) {
			return LIBUSB_ERROR_NO_DEVICE;
		} else {
			usbi_log_error(ctx, "Could not cancel %s transfer %p (length: %d): %s (%d)",
			               (LIBUSB_ENDPOINT_IN & transfer->endpoint) != 0 ? "read" : "write",
			               transfer, transfer->length, get_errno_name(errno), errno);

			return LIBUSB_ERROR_OTHER;
		}
	}

	return LIBUSB_SUCCESS;
}

// NOTE: assumes that transfer is not submitted
void libusb_free_transfer(struct libusb_transfer *transfer) {
	usbi_transfer *itransfer = (usbi_transfer *)transfer;

	free(itransfer);
}

void libusb_fill_bulk_transfer(struct libusb_transfer *transfer,
                               libusb_device_handle *dev_handle,
                               unsigned char endpoint, unsigned char *buffer,
                               int length, libusb_transfer_callback callback,
                               void *user_data, unsigned int timeout) {
	transfer->dev_handle = dev_handle;
	transfer->endpoint = endpoint;
	transfer->type = LIBUSB_TRANSFER_TYPE_BULK;
	transfer->timeout = timeout;
	transfer->buffer = buffer;
	transfer->length = length;
	transfer->user_data = user_data;
	transfer->callback = callback;
}

void libusb_set_log_callback(libusb_log_callback callback) {
	_log_callback = callback;
}
