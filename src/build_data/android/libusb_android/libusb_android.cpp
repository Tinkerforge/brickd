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

typedef struct {
	struct libusb_device_descriptor device;
	struct libusb_config_descriptor config;
} usbi_descriptor;

typedef struct {
	struct libusb_transfer transfer;
	Node node;
	bool submitted;
	bool triggered;
	bool completed;
	uint32_t sequence_number;
    jobject request;
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
	int event_pipe[2];
	struct libusb_pollfd event_pollfd;
	Node dev_handle_sentinel;
};

struct _libusb_device_handle {
	Node node;
	libusb_device *dev;
	jobject connection; // UsbDeviceConnection
	Node read_itransfer_sentinel;
	Node write_itransfer_sentinel;
};

static libusb_log_callback _log_callback;

static std::map<std::string, uint16_t> _fake_device_addresses;
static uint32_t _next_read_itransfer_sequence_number;
static uint32_t _next_write_itransfer_sequence_number;

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
    jstring string = reinterpret_cast<jstring>(android_env->GetObjectField(obj, fid)); // FIXME: check result

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
    interface_infos = reinterpret_cast<jobjectArray>(usbi_get_object_field(device_info, "interfaceInfos",
                                                                           "[Lcom/tinkerforge/brickd/USBInterfaceInfo;", false));

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
        endpoint_addresses = reinterpret_cast<jintArray>(usbi_get_object_field(interface_info, "endpointAddresses", "[I", false));
        endpoint_addresses_elements = android_env->GetIntArrayElements(endpoint_addresses, nullptr);// FIXME: check result

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
    jint res;
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
    device_infos = reinterpret_cast<jobjectArray>(android_env->CallObjectMethod(android_service, get_device_list_mid));
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

static int usbi_get_string_descriptor(libusb_device_handle *dev_handle,
                                      uint8_t desc_index, uint16_t language_id,
                                      unsigned char *data, int length) {
    libusb_context *ctx = dev_handle->dev->ctx;
	jmethodID get_string_descriptor_mid;
    jbyteArray result;
    jbyte *result_elements;
    int result_length;
    int i;

    get_string_descriptor_mid = android_env->GetMethodID(android_env->GetObjectClass(android_service), "getStringDescriptor",
                                                         "(Landroid/hardware/usb/UsbDeviceConnection;III)[B"); // FIXME: check result
    result = reinterpret_cast<jbyteArray>(android_env->CallObjectMethod(android_service, get_string_descriptor_mid, dev_handle->connection, desc_index, language_id, length)); // FIXME: check result

    if (result == nullptr) {
        usbi_log_error(ctx, "Could not get string descriptor");

        return LIBUSB_ERROR_OTHER; // FIXME: use better error code
    }

    result_elements = android_env->GetByteArrayElements(result, nullptr);// FIXME: check result
    result_length = android_env->GetArrayLength(result);

    // NOTE: assumes result_length <= length
    for (i = 0; i < result_length; ++i) {
        data[i] = (uint8_t)result_elements[i];
    }

    android_env->ReleaseByteArrayElements(result, result_elements, 0);

	return result_length;
}
/*
static void usbi_set_transfer_status(struct libusb_transfer *transfer,
                                     IAsyncOperation<size_t> ^operation,
                                     AsyncStatus status) {
	int hresult;

	if (status == AsyncStatus::Error) {
		hresult = operation->ErrorCode.Value;

		if (HRESULT_CODE(hresult) == ERROR_DEVICE_NOT_CONNECTED ||
			HRESULT_CODE(hresult) == ERROR_DEV_NOT_EXIST) {
			transfer->status = LIBUSB_TRANSFER_NO_DEVICE;
		} else {
			transfer->status = LIBUSB_TRANSFER_ERROR; // FIXME
		}
	} else if (status == AsyncStatus::Canceled) {
		transfer->status = LIBUSB_TRANSFER_CANCELLED;
	} else if (status == AsyncStatus::Completed) {
		try {
			transfer->actual_length = operation->GetResults();
			transfer->status = LIBUSB_TRANSFER_COMPLETED;
		} catch (...) { // FIXME: too generic
			transfer->actual_length = 0;
			transfer->status = LIBUSB_TRANSFER_ERROR; // FIXME
		}
	} else {
		transfer->status = LIBUSB_TRANSFER_ERROR; // FIXME
	}
}*/

int libusb_init(libusb_context **ctx_ptr) {
    jclass usb_cls;
    jmethodID usb_ctor;
    jobject usb;
	libusb_context *ctx;

	if (ctx_ptr == nullptr) {
		return LIBUSB_ERROR_INVALID_PARAM; // FIXME: no default context support
	}

    ctx = (libusb_context *)calloc(1, sizeof(libusb_context));

	if (ctx == nullptr) {
		return LIBUSB_ERROR_NO_MEM;
	}

	usbi_log_debug(ctx, "Creating context %p", ctx);

	if (pipe(ctx->event_pipe) < 0) {
		usbi_log_error(ctx, "Could not create transfer pipe for context %p: %s (%d)",
		               ctx, get_errno_name(errno), errno);

		free(ctx);

		return LIBUSB_ERROR_OTHER;
	}

	ctx->event_pollfd.fd = ctx->event_pipe[0];
	ctx->event_pollfd.events = POLLIN;

	node_reset(&ctx->dev_handle_sentinel);

	*ctx_ptr = ctx;

	return LIBUSB_SUCCESS;
}

// NOTE: assumes that no transfers are pending
void libusb_exit(libusb_context *ctx) {
	if (ctx == nullptr) {
		return; // FIXME: no default context support
	}

	usbi_log_debug(ctx, "Destroying context %p", ctx);

	close(ctx->event_pipe[0]);
	close(ctx->event_pipe[1]);

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

	if (ctx == nullptr) {
		return nullptr; // FIXME: no default context support
	}

	pollfds = (const struct libusb_pollfd **)calloc(2, sizeof(struct libusb_pollfd *));

	if (pollfds == nullptr) {
		return nullptr;
	}

	pollfds[0] = &ctx->event_pollfd;
	pollfds[1] = nullptr;

	return pollfds;
}

void libusb_free_pollfds(const struct libusb_pollfd **pollfds) {
	free(pollfds);
}

void libusb_set_pollfd_notifiers(libusb_context *ctx,
                                 libusb_pollfd_added_callback added_callback,
                                 libusb_pollfd_removed_callback removed_callback,
                                 void *user_data) {
	(void)ctx;
	(void)added_callback;
	(void)removed_callback;
	(void)user_data;
}

int libusb_handle_events_timeout(libusb_context *ctx, struct timeval *tv) {
	int count = 0;
	Node *dev_handle_node;
	libusb_device_handle *dev_handle;
	bool sparse;
	Node *itransfer_node;
	Node *itransfer_node_next;
	usbi_transfer *itransfer;
	struct libusb_transfer *transfer;
    uint8_t byte;

	if (ctx == nullptr) {
		return LIBUSB_ERROR_INVALID_PARAM; // FIXME: no default context support
	}

	if (tv->tv_sec != 0 || tv->tv_usec != 0) {
		return LIBUSB_ERROR_INVALID_PARAM; // FIXME: no timeout support
	}

	usbi_log_debug(ctx, "Handling events");

	dev_handle_node = ctx->dev_handle_sentinel.next;

	while (dev_handle_node != &ctx->dev_handle_sentinel) {
		dev_handle = containerof(dev_handle_node, libusb_device_handle, node);
		sparse = false;
		itransfer_node = dev_handle->read_itransfer_sentinel.next;

		while (itransfer_node != &dev_handle->read_itransfer_sentinel) {
			itransfer_node_next = itransfer_node->next;
			itransfer = containerof(itransfer_node, usbi_transfer, node);
			transfer = &itransfer->transfer;

			if (!itransfer->completed) {
				sparse = true;
			} else {
				if (itransfer->triggered) {
					usbi_log_debug(ctx, "Reading from event pipe for read transfer %p [%u]",
					               transfer, itransfer->sequence_number);

					if (read(ctx->event_pipe[0], &byte, 1) != 1) {
						usbi_log_error(ctx, "read failed: %d", errno); // FIXME
					} else {
						itransfer->triggered = false;
					}
				}

				if (!sparse || transfer->status != LIBUSB_TRANSFER_COMPLETED) {
					usbi_log_debug(ctx, "Read transfer %p [%u] completed (length: %d, status: %d)",
					               transfer, itransfer->sequence_number,
					               transfer->actual_length, transfer->status);

					node_remove(&itransfer->node);

					//itransfer->load_operation->Close();

					itransfer->submitted = false;
					itransfer->completed = false;
					//itransfer->reader = nullptr;
					//itransfer->load_operation = nullptr;

					usbi_log_debug(ctx, "Triggering callback for read transfer %p [%u]",
					               transfer, itransfer->sequence_number);

					transfer->callback(transfer); // might free or submit transfer

					libusb_unref_device(dev_handle->dev);

					++count;
				}
			}

			itransfer_node = itransfer_node_next;
		}

		itransfer_node = dev_handle->write_itransfer_sentinel.next;

		while (itransfer_node != &dev_handle->write_itransfer_sentinel) {
			itransfer_node_next = itransfer_node->next;
			itransfer = containerof(itransfer_node, usbi_transfer, node);
			transfer = &itransfer->transfer;

			if (itransfer->completed) {
				if (itransfer->triggered) {
					usbi_log_debug(ctx, "Reading from event pipe for write transfer %p [%u]",
					               transfer, itransfer->sequence_number);

					if (read(ctx->event_pipe[0], &byte, 1) != 1) {
						usbi_log_error(ctx, "read failed: %d", errno); // FIXME
					} else {
						itransfer->triggered = false;
					}
				}

				node_remove(&itransfer->node);

				//itransfer->store_operation->Close();

				itransfer->submitted = false;
				itransfer->completed = false;
				//itransfer->writer = nullptr;
				//itransfer->store_operation = nullptr;

				usbi_log_debug(ctx, "Triggering callback for write transfer %p [%u]",
				               transfer, itransfer->sequence_number);

				transfer->callback(transfer); // might free or submit transfer

				libusb_unref_device(dev_handle->dev);

				++count;
			}

			itransfer_node = itransfer_node_next;
		}

		dev_handle_node = dev_handle_node->next;
	}

	usbi_log_debug(ctx, "Handled %d event(s)", count);

    if (count == 0) {
        return LIBUSB_SUCCESS;
    }

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
    jobject connection;

	dev_handle = (libusb_device_handle *)calloc(1, sizeof(libusb_device_handle));

	if (dev_handle == nullptr) {
		usbi_log_error(ctx, "Could not allocate device handle");

		return LIBUSB_ERROR_NO_MEM;
	}

    open_device_mid = android_env->GetMethodID(android_env->GetObjectClass(android_service), "openDevice",
                                               "(Landroid/hardware/usb/UsbDevice;)Landroid/hardware/usb/UsbDeviceConnection;"); // FIXME: check result
    connection = android_env->CallObjectMethod(android_service, open_device_mid, dev->device); // FIXME: check result

	/*if (connection == nullptr) { // FIXME
		free(dev_handle);

		return LIBUSB_ERROR_NO_DEVICE;
	}*/

    dev_handle->connection = android_env->NewGlobalRef(connection); // FIXME: check result
	dev_handle->dev = libusb_ref_device(dev);

	node_reset(&dev_handle->read_itransfer_sentinel);
	node_reset(&dev_handle->write_itransfer_sentinel);

	node_insert_before(&ctx->dev_handle_sentinel, &dev_handle->node);

	*dev_handle_ptr = dev_handle;

	usbi_log_debug(ctx, "Opened device %p (context: %p, name: %s)",
	               dev, ctx, dev->name);

	return LIBUSB_SUCCESS;
}

void libusb_close(libusb_device_handle *dev_handle) {
	libusb_device *dev = dev_handle->dev;
	libusb_context *ctx = dev->ctx;
    jmethodID close_device_mid;

	usbi_log_debug(ctx, "Closing device %p (context: %p, name: %s)",
	               dev, ctx, dev->name);


    close_device_mid = android_env->GetMethodID(android_env->GetObjectClass(android_service), "closeDevice",
                                                "(Landroid/hardware/usb/UsbDeviceConnection;)V"); // FIXME: check result

    android_env->CallVoidMethod(android_service, close_device_mid, dev_handle->connection); // FIXME: check result

	node_remove(&dev_handle->node);

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
	int rc;
	unsigned char buffer[255];
	uint16_t language_id;
	int d;
	int s;

	if (desc_index == 0) {
		return LIBUSB_ERROR_INVALID_PARAM;
	}

    // get language ID
	rc = usbi_get_string_descriptor(dev_handle, 0, 0, buffer, sizeof(buffer));

	if (rc < 0) {
		return rc;
	}

	if (rc < 4) {
		return LIBUSB_ERROR_IO;
	}

	language_id = 0;//(buffer[3] << 8) | buffer[2];

    usbi_log_info(ctx, "language_id %04X", language_id);

    // get string descriptor
	rc = usbi_get_string_descriptor(dev_handle, desc_index, language_id, buffer, sizeof(buffer));

	if (rc < 0) {
		return rc;
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
    jmethodID claim_interface_mid;
    jboolean result;

    claim_interface_mid = android_env->GetMethodID(android_env->GetObjectClass(android_service), "claimInterface",
                                       "(Landroid/hardware/usb/UsbDevice;Landroid/hardware/usb/UsbDeviceConnection;I)Z"); // FIXME: check result
    result = android_env->CallBooleanMethod(android_service, claim_interface_mid, dev_handle->dev->device, dev_handle->connection, interface_number); // FIXME: check result

    return result ? LIBUSB_SUCCESS : LIBUSB_ERROR_OTHER;
}

int libusb_release_interface(libusb_device_handle *dev_handle, int interface_number) {
    jmethodID release_interface_mid;
    jboolean result;

    release_interface_mid = android_env->GetMethodID(android_env->GetObjectClass(android_service), "releaseInterface",
                                                     "(Landroid/hardware/usb/UsbDevice;Landroid/hardware/usb/UsbDeviceConnection;I)Z"); // FIXME: check result
    result = android_env->CallBooleanMethod(android_service, release_interface_mid, dev_handle->dev->device, dev_handle->connection, interface_number); // FIXME: check result

    return result ? LIBUSB_SUCCESS : LIBUSB_ERROR_OTHER;
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
	itransfer->completed = false;
    itransfer->sequence_number = 0;
    itransfer->request = nullptr;

	return &itransfer->transfer;
}

int libusb_submit_transfer(struct libusb_transfer *transfer) {
	usbi_transfer *itransfer = (usbi_transfer *)transfer;
	libusb_device_handle *dev_handle = transfer->dev_handle;
	libusb_context *ctx = dev_handle->dev->ctx;
    jobject opaque;
    jobject buffer;
    jmethodID submit_transter_mid;
    jobject request;

	if (transfer->timeout != 0 || transfer->callback == nullptr) {
		return LIBUSB_ERROR_INVALID_PARAM;
	}

	if (itransfer->submitted) {
		return LIBUSB_ERROR_BUSY;
	}

    opaque = android_env->NewDirectByteBuffer(itransfer, sizeof(usbi_transfer)); // FIXME: check result
    buffer = android_env->NewDirectByteBuffer(transfer->buffer, transfer->length); // FIXME: check result

    libusb_ref_device(dev_handle->dev);

    itransfer->submitted = true;

    if ((LIBUSB_ENDPOINT_IN & transfer->endpoint) != 0) {
        itransfer->sequence_number = _next_read_itransfer_sequence_number++;
    } else {
        itransfer->sequence_number = _next_write_itransfer_sequence_number++;
    }

    submit_transter_mid = android_env->GetMethodID(android_env->GetObjectClass(android_service), "submitTransfer",
                                                   "(Landroid/hardware/usb/UsbDevice;Landroid/hardware/usb/UsbDeviceConnection;ILjava/nio/ByteBuffer;Ljava/nio/ByteBuffer;)Landroid/hardware/usb/UsbRequest;"); // FIXME: check result
    request = android_env->CallObjectMethod(android_service, submit_transter_mid, dev_handle->dev->device, dev_handle->connection, transfer->endpoint, buffer, opaque); // FIXME: check result

    if (request == nullptr) {
        if ((LIBUSB_ENDPOINT_IN & transfer->endpoint) != 0) {
            usbi_log_error(ctx, "Could not submit read transfer %p [%u] (length: %d): <error>", // FIXME
                           transfer, itransfer->sequence_number, transfer->length);
        } else {
            usbi_log_error(ctx, "Could not submit write transfer %p [%u] (length: %d): <error>", // FIXME
                           transfer, itransfer->sequence_number, transfer->length);
        }

        itransfer->submitted = false;

        libusb_unref_device(dev_handle->dev);

        return LIBUSB_ERROR_NO_DEVICE; // FIXME: assumes that this happend because of device hotunplug, could also be LIBUSB_ERROR_NOT_FOUND
    } else {
        itransfer->request = android_env->NewGlobalRef(request); // FIXME: check result

        if ((LIBUSB_ENDPOINT_IN & transfer->endpoint) != 0) {
            node_insert_before(&dev_handle->read_itransfer_sentinel,
                               &itransfer->node);
        } else {
            node_insert_before(&dev_handle->write_itransfer_sentinel,
                               &itransfer->node);
        }
    }

	return LIBUSB_SUCCESS;
}

int libusb_cancel_transfer(struct libusb_transfer *transfer) {
	usbi_transfer *itransfer = (usbi_transfer *)transfer;

	/*if (itransfer->load_operation != nullptr) {
		itransfer->load_operation->Cancel();
	}

	if (itransfer->store_operation != nullptr) {
		itransfer->store_operation->Cancel();
	}*/

	return LIBUSB_SUCCESS;
}

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
	transfer->timeout = timeout;
	transfer->buffer = buffer;
	transfer->length = length;
	transfer->user_data = user_data;
	transfer->callback = callback;
}

void libusb_set_log_callback(libusb_log_callback callback) {
	_log_callback = callback;
}

extern "C" JNIEXPORT void JNICALL
Java_com_tinkerforge_brickd_MainService_transferred(JNIEnv *env, jobject /* this */, jobject opaque, jint length) {
    usbi_transfer *itransfer = (usbi_transfer *)env->GetDirectBufferAddress(opaque); // FIXME: check result
    struct libusb_transfer *transfer = &itransfer->transfer;
    libusb_context *ctx = transfer->dev_handle->dev->ctx;
    uint8_t byte = 0;

    if (length < 0) {
        transfer->status = LIBUSB_TRANSFER_ERROR; // FIXME
    } else {
        transfer->status = LIBUSB_TRANSFER_COMPLETED;
        transfer->actual_length = length;
    }

    env->DeleteGlobalRef(itransfer->request); // FIXME: check result

    itransfer->triggered = true;
    itransfer->completed = true;
    itransfer->request = nullptr;

    if ((transfer->endpoint & LIBUSB_ENDPOINT_IN) != 0) {
        usbi_log_debug(ctx, "Read transfer %p [%u] completed (length: %d, status: %d)",
                       transfer, itransfer->sequence_number,
                       transfer->actual_length, transfer->status);
    } else {
        usbi_log_debug(ctx, "Write transfer %p [%u] completed (length: %d, status: %d)",
                       transfer, itransfer->sequence_number,
                       transfer->actual_length, transfer->status);
    }

    if (write(ctx->event_pipe[1], &byte, 1) != 1) {
        usbi_log_error(ctx, "write failed: %d", errno); // FIXME
    }
}