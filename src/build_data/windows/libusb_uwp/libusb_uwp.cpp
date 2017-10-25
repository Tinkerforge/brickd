/*
 * brickd
 * Copyright (C) 2016-2017 Matthias Bolte <matthias@tinkerforge.com>
 *
 * libusb_uwp.cpp: Emulating libusb API for Universal Windows Platform
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

#include <collection.h>
#include <ppltasks.h>
#include <unordered_map>
#include <stdbool.h>

extern "C" {

#include <daemonlib/macros.h>
#include <daemonlib/node.h>
#include <daemonlib/utils.h>
#include <daemonlib/utils_uwp.h>

}

using namespace concurrency;
using namespace Platform;
using namespace Platform::Collections;
using namespace Windows::Foundation;
using namespace Windows::Devices::Enumeration;
using namespace Windows::Devices::Enumeration::Pnp;
using namespace Windows::Devices::Usb;
using namespace Windows::Storage::Streams;

#include "libusb.h"

#define USBI_MAX_FAKE_FDS 4096

#define USBI_POLLIN 0x0001
#define USBI_POLLOUT 0x0004
#define USBI_POLLERR 0x0008

#define USBI_STRING_MANUFACTURER 1
#define USBI_STRING_PRODUCT 2
#define USBI_STRING_SERIAL_NUMBER 3

struct usbi_pollfd {
	int fd;
	short events;
	short revents;
};

typedef struct {
	int ref_count;
	HANDLE semaphore;
} usbi_fake_pipe;

typedef struct {
	int fd;
	int event;
	usbi_fake_pipe *fake_pipe;
} usbi_fake_fd;

typedef struct {
	struct libusb_device_descriptor desc;
	struct libusb_config_descriptor *config;
} usbi_cached_descriptor;

typedef struct {
	struct libusb_transfer transfer;
	Node node;
	bool submitted;
	bool triggered;
	bool completed;
	uint32_t sequence_number;
	DataReader ^reader;
	DataReaderLoadOperation ^load_operation;
	DataWriter ^writer;
	DataWriterStoreOperation ^store_operation;
} usbi_transfer;

struct _libusb_device {
	Node node;
	libusb_context *ctx;
	int ref_count;
	String ^id;
	char *id_ascii;
	uint8_t bus_number;
	uint8_t device_address;
	usbi_cached_descriptor *descriptor;
	char manufacturer[128];
	String ^product;
	char *product_ascii;
	char serial_number[128];
};

struct _libusb_context {
	int event_pipe[2];
	struct libusb_pollfd event_pollfd;
	Node dev_handle_sentinel;
};

struct _libusb_device_handle {
	Node node;
	libusb_device *dev;
	UsbDevice ^device;
	Node read_itransfer_sentinel;
	Node write_itransfer_sentinel;
};

static libusb_log_function _log_function;

static usbi_fake_fd _fake_fds[USBI_MAX_FAKE_FDS];
static std::unordered_map<std::wstring, uint16_t> _fake_device_addresses;
static std::unordered_map<std::wstring, usbi_cached_descriptor *> _cached_descriptors;
static uint32_t _next_read_itransfer_sequence_number;
static uint32_t _next_write_itransfer_sequence_number;

// NOTE: assumes _log_function is not nullptr
static void usbi_log_message(libusb_context *ctx, libusb_log_level level,
                             const char *function, const char *format, ...) {
	va_list args;

	va_start(args, format);

	_log_function(ctx, level, function, format, args);

	va_end(args);
}

#define usbi_log_message_checked(ctx, level, ...) \
	do { \
		if (_log_function != nullptr) { \
			usbi_log_message(ctx, level, __FUNCTION__, __VA_ARGS__); \
		} \
	__pragma(warning(push)) \
	__pragma(warning(disable:4127)) \
	} while (0) \
	__pragma(warning(pop))

#define usbi_log_error(ctx, ...)   usbi_log_message_checked(ctx, LIBUSB_LOG_LEVEL_ERROR, __VA_ARGS__)
#define usbi_log_warning(ctx, ...) usbi_log_message_checked(ctx, LIBUSB_LOG_LEVEL_WARNING, __VA_ARGS__)
#define usbi_log_info(ctx, ...)    usbi_log_message_checked(ctx, LIBUSB_LOG_LEVEL_INFO, __VA_ARGS__)
#define usbi_log_debug(ctx, ...)   usbi_log_message_checked(ctx, LIBUSB_LOG_LEVEL_DEBUG, __VA_ARGS__)

// sets errno on error
static usbi_fake_fd *usbi_create_fake_fd(int event, usbi_fake_pipe *fake_pipe) {
	int i;
	usbi_fake_fd *fake_fd = nullptr;

	for (i = 0; i < USBI_MAX_FAKE_FDS; ++i) {
		if (_fake_fds[i].fake_pipe == nullptr) {
			fake_fd = &_fake_fds[i];
			fake_fd->fd = i;

			break;
		}
	}

	if (fake_fd == nullptr) {
		errno = EMFILE;

		return nullptr;
	}

	fake_fd->event = event;
	fake_fd->fake_pipe = fake_pipe;

	return fake_fd;
}

static void usbi_free_fake_fd(usbi_fake_fd *fake_fd) {
	fake_fd->fd = -1;
	fake_fd->event = 0;
	fake_fd->fake_pipe = nullptr;
}

static usbi_fake_fd *usbi_get_fake_fd(int fd) {
	if (fd < 0 || fd >= USBI_MAX_FAKE_FDS || _fake_fds[fd].fd != fd) {
		return nullptr;
	}

	return &_fake_fds[fd];
}

extern "C" void usbi_init(void) {
	int i;

	for (i = 0; i < USBI_MAX_FAKE_FDS; ++i) {
		_fake_fds[i].fd = -1;
	}
}

// sets errno on error
extern "C" int usbi_pipe(int fd[2]) {
	usbi_fake_pipe *fake_pipe;
	int saved_errno;
	usbi_fake_fd *fake_read_fd;
	usbi_fake_fd *fake_write_fd;

	fake_pipe = (usbi_fake_pipe *)calloc(1, sizeof(usbi_fake_pipe));

	if (fake_pipe == nullptr) {
		errno = ENOMEM;

		return -1;
	}

	fake_pipe->ref_count = 2; // one ref for each end of the pipe
	fake_pipe->semaphore = CreateSemaphore(nullptr, 0, INT32_MAX, nullptr);

	if (fake_pipe->semaphore == nullptr) {
		saved_errno = ERRNO_WINAPI_OFFSET + GetLastError();

		free(fake_pipe);

		errno = saved_errno;

		return -1;
	}

	fake_read_fd = usbi_create_fake_fd(USBI_POLLIN, fake_pipe);

	if (fake_read_fd == nullptr) {
		saved_errno = errno;

		CloseHandle(fake_pipe->semaphore);
		free(fake_pipe);

		errno = saved_errno;

		return -1;
	}

	fake_write_fd = usbi_create_fake_fd(USBI_POLLOUT, fake_pipe);

	if (fake_write_fd == nullptr) {
		saved_errno = errno;

		usbi_free_fake_fd(fake_read_fd);
		CloseHandle(fake_pipe->semaphore);
		free(fake_pipe);

		errno = saved_errno;

		return -1;
	}

	fd[0] = fake_read_fd->fd;
	fd[1] = fake_write_fd->fd;

	return 0;
}

// sets errno on error
extern "C" int usbi_close(int fd) {
	usbi_fake_fd *fake_fd;

	fake_fd = usbi_get_fake_fd(fd);

	if (fake_fd == nullptr) {
		errno = EBADF;

		return -1;
	}

	if (fake_fd->fake_pipe != nullptr) {
		if (--fake_fd->fake_pipe->ref_count == 0) {
			CloseHandle(fake_fd->fake_pipe->semaphore);
			free(fake_fd->fake_pipe);
		}
	}

	usbi_free_fake_fd(fake_fd);

	return 0;
}

// sets errno on error
extern "C" ssize_t usbi_read(int fd, void *buf, size_t count) {
	usbi_fake_fd *fake_fd;

	if (count != sizeof(uint8_t)) {
		errno = ERANGE;

		return -1;
	}

	fake_fd = usbi_get_fake_fd(fd);

	if (fake_fd == nullptr || fake_fd->event != USBI_POLLIN) {
		errno = EBADF;

		return -1;
	}

	if (WaitForSingleObject(fake_fd->fake_pipe->semaphore, INFINITE) != WAIT_OBJECT_0) {
		errno = ERRNO_WINAPI_OFFSET + GetLastError();

		return -1;
	}

	return sizeof(uint8_t);
}

// sets errno on error
extern "C" ssize_t usbi_write(int fd, const void *buf, size_t count) {
	usbi_fake_fd *fake_fd;

	if (count != sizeof(uint8_t)) {
		errno = ERANGE;

		return -1;
	}

	fake_fd = usbi_get_fake_fd(fd);

	if (fake_fd == nullptr || fake_fd->event != USBI_POLLOUT) {
		errno = EBADF;

		return -1;
	}

	ReleaseSemaphore(fake_fd->fake_pipe->semaphore, 1, nullptr);

	return sizeof(uint8_t);
}

// sets errno on error
extern "C" int usbi_poll(struct usbi_pollfd *fds, unsigned int nfds, int timeout) {
	HANDLE *handles;
	usbi_fake_fd *fake_fd;
	int ready = 0;
	unsigned int i;
	DWORD rc;

	if (nfds < 1) {
		return 0;
	}

	if (timeout >= 0) {
		errno = EINVAL;

		return -1;
	}

	handles = (HANDLE *)calloc(nfds, sizeof(HANDLE));

	if (handles == nullptr) {
		errno = ENOMEM;

		return -1;
	}

	for (i = 0; i < nfds; ++i) {
		fds[i].revents = 0;
		fake_fd = usbi_get_fake_fd(fds[i].fd);

		if (fake_fd == nullptr) {
			fds[i].revents |= USBI_POLLERR;
			errno = EBADF;

			return -1;
		}

		if ((fds[i].events != USBI_POLLIN && fds[i].events != USBI_POLLOUT) ||
		    (fds[i].events == USBI_POLLIN && fake_fd->event != USBI_POLLIN) ||
		    (fds[i].events == USBI_POLLOUT && fake_fd->event != USBI_POLLOUT)) {
			fds[i].revents |= USBI_POLLERR;
			errno = EINVAL;

			return -1;
		}

		if (fds[i].events == USBI_POLLIN) {
			handles[i] = fake_fd->fake_pipe->semaphore;
		} else { // USBI_POLLOUT
			++ready;
			fds[i].revents |= USBI_POLLOUT;
		}
	}

	if (ready == 0) {
		rc = WaitForMultipleObjects(nfds, handles, FALSE, INFINITE);

		if (rc < WAIT_OBJECT_0 || rc >= WAIT_OBJECT_0 + nfds) {
			free(handles);

			errno = EINTR;

			return -1;
		}

		i = rc - WAIT_OBJECT_0;

		// WaitForMultipleObjects decreases the counter of the semaphore. undo
		// this to allow usbi_read to decrease it again
		ReleaseSemaphore(handles[i], 1, nullptr);

		fds[i].revents = USBI_POLLIN;
		ready = 1;
	}

	free(handles);

	return ready;
}

static void usbi_get_fake_device_address(String ^id, uint8_t *bus_number,
                                         uint8_t *device_address) {
	std::wstring id_wchar(id->Data());
	auto iter = _fake_device_addresses.find(id_wchar);
	uint16_t value;

	if (iter != _fake_device_addresses.end()) {
		value = iter->second;
	} else {
		// FIXME: after 65536 different IDs this will start to reuse bus numbers
		//        and device addresses. this will probably never be a problem
		value = _fake_device_addresses.size() % 0xFFFF;

		_fake_device_addresses.insert_or_assign(id_wchar, value);
	}

	*bus_number = (value >> 8) & 0xFF;
	*device_address = value & 0xFF;
}

static void usbi_free_config_descriptor(struct libusb_config_descriptor *config) {
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
	free(config);
}

static int usbi_create_config_descriptor(libusb_context *ctx, UsbDevice ^device,
                                         struct libusb_config_descriptor **config_ptr) {
	struct libusb_config_descriptor *config;
	unsigned int i;
	UsbInterface ^interface;
	struct libusb_interface *iface;
	int s;
	UsbInterfaceSetting ^setting;
	struct libusb_interface_descriptor *desc;
	unsigned int e;
	UsbBulkInEndpointDescriptor ^endpoint_in;
	struct libusb_endpoint_descriptor *endpoint;
	UsbBulkOutEndpointDescriptor ^endpoint_out;

	config = (struct libusb_config_descriptor *)calloc(1, sizeof(struct libusb_config_descriptor));

	if (config == nullptr) {
		usbi_log_error(ctx, "Could not allocate config descriptor");

		return LIBUSB_ERROR_NO_MEM;
	}

	config->bNumInterfaces = device->Configuration->UsbInterfaces->Size;
	config->interface = (struct libusb_interface *)calloc(config->bNumInterfaces,
	                                                      sizeof(struct libusb_interface));

	if (config->interface == nullptr) {
		usbi_free_config_descriptor(config);
		usbi_log_error(ctx, "Could not allocate interface");

		return LIBUSB_ERROR_NO_MEM;
	}

	for (i = 0; i < config->bNumInterfaces; ++i) {
		interface = device->Configuration->UsbInterfaces->GetAt(i);
		iface = (struct libusb_interface *)&config->interface[i];
		iface->num_altsetting = interface->InterfaceSettings->Size;
		iface->altsetting = (struct libusb_interface_descriptor *)calloc(iface->num_altsetting,
		                                                                 sizeof(struct libusb_interface_descriptor));

		if (iface->altsetting == nullptr) {
			usbi_free_config_descriptor(config);
			usbi_log_error(ctx, "Could not allocate interface descriptor");

			return LIBUSB_ERROR_NO_MEM;
		}

		for (s = 0; s < iface->num_altsetting; ++s) {
			setting = interface->InterfaceSettings->GetAt(s);
			desc = (struct libusb_interface_descriptor *)&iface->altsetting[s];

			desc->bInterfaceNumber = setting->InterfaceDescriptor->InterfaceNumber;
			desc->bNumEndpoints = setting->BulkInEndpoints->Size + setting->BulkOutEndpoints->Size;
			desc->endpoint = (struct libusb_endpoint_descriptor *)calloc(desc->bNumEndpoints,
			                                                             sizeof(struct libusb_interface_descriptor));

			if (desc->endpoint == nullptr) {
				usbi_free_config_descriptor(config);
				usbi_log_error(ctx, "Could not allocate endpoint descriptor");

				return LIBUSB_ERROR_NO_MEM;
			}

			for (e = 0; e < setting->BulkInEndpoints->Size; ++e) {
				endpoint_in = setting->BulkInEndpoints->GetAt(e);
				endpoint = (struct libusb_endpoint_descriptor *)&desc->endpoint[e];

				endpoint->bEndpointAddress = LIBUSB_ENDPOINT_IN | endpoint_in->EndpointNumber;
			}

			for (e = 0; e < setting->BulkOutEndpoints->Size; ++e) {
				endpoint_out = setting->BulkOutEndpoints->GetAt(e);
				endpoint = (struct libusb_endpoint_descriptor *)&desc->endpoint[setting->BulkInEndpoints->Size + e];

				endpoint->bEndpointAddress = LIBUSB_ENDPOINT_OUT | endpoint_out->EndpointNumber;
			}
		}
	}

	*config_ptr = config;

	return LIBUSB_SUCCESS;
}

static int usbi_get_cached_descriptor(libusb_context *ctx, String ^id,
                                      usbi_cached_descriptor **descriptor_ptr) {
	std::wstring id_wchar(id->Data());
	auto iter = _cached_descriptors.find(id_wchar);
	int rc = LIBUSB_SUCCESS;
	int *rc_ptr = &rc;

	if (iter != _cached_descriptors.end()) {
		*descriptor_ptr = iter->second;
	} else {
		create_task(UsbDevice::FromIdAsync(id))
		.then([ctx, id, rc_ptr, descriptor_ptr](task<UsbDevice ^> previous) {
			UsbDevice ^device;
			char *id_ascii;
			usbi_cached_descriptor *descriptor;

			try {
				device = previous.get();
			} catch (...) { // FIXME: too generic
				id_ascii = string_convert_ascii(id);

				usbi_log_warning(ctx, "Could not open device %s: <exception>", // FIXME
				                 id_ascii != nullptr ? id_ascii : "<unknown>");

				free(id_ascii);

				*rc_ptr = LIBUSB_ERROR_NO_DEVICE;

				return;
			}

			if (device == nullptr) {
				id_ascii = string_convert_ascii(id);

				usbi_log_warning(ctx, "Could not open device %s",
				                 id_ascii != nullptr ? id_ascii : "<unknown>");

				free(id_ascii);

				*rc_ptr = LIBUSB_ERROR_NO_DEVICE;

				return;
			}

			descriptor = (usbi_cached_descriptor *)calloc(1, sizeof(usbi_cached_descriptor));

			if (descriptor == nullptr) {
				usbi_log_error(ctx, "Could not allocate cached descriptor");

				*rc_ptr = LIBUSB_ERROR_NO_MEM;

				return;
			}

			descriptor->desc.idVendor = device->DeviceDescriptor->VendorId;
			descriptor->desc.idProduct = device->DeviceDescriptor->ProductId;
			descriptor->desc.bcdDevice = device->DeviceDescriptor->BcdDeviceRevision;
			descriptor->desc.iManufacturer = USBI_STRING_MANUFACTURER;
			descriptor->desc.iProduct = USBI_STRING_PRODUCT;
			descriptor->desc.iSerialNumber = USBI_STRING_SERIAL_NUMBER;

			*rc_ptr = usbi_create_config_descriptor(ctx, device, &descriptor->config);

			if (*rc_ptr < 0) {
				return;
			}

			*descriptor_ptr = descriptor;
		}).wait();

		if (rc == LIBUSB_SUCCESS) {
			_cached_descriptors.insert_or_assign(id_wchar, *descriptor_ptr);
		}
	}

	return rc;
}

static int usbi_create_device(libusb_context *ctx, DeviceInformation ^info,
                              libusb_device **dev_ptr) {
	libusb_device *dev = (libusb_device *)calloc(1, sizeof(libusb_device));
	int rc;
	const char *brick_id_prefix1 = "\\\\?\\USB#";
	const char *brick_id_prefix2 = "VID_16D0&PID_063D#";
	const char *red_brick_id_prefix2 = "VID_16D0&PID_09E5#";
	char *uid_start = nullptr;
	char *uid_end = nullptr;

	if (dev == nullptr) {
		usbi_log_error(ctx, "Could not allocate device");

		return LIBUSB_ERROR_NO_MEM;
	}

	node_reset(&dev->node);

	dev->ctx = ctx;
	dev->ref_count = 1;
	dev->id = info->Id;
	dev->id_ascii = string_convert_ascii(info->Id);

	if (dev->id_ascii == nullptr) {
		usbi_log_error(ctx, "Could not convert device identifier");

		free(dev);

		return LIBUSB_ERROR_NO_MEM;
	}

	usbi_get_fake_device_address(info->Id, &dev->bus_number, &dev->device_address);

	rc = usbi_get_cached_descriptor(ctx, info->Id, &dev->descriptor);

	if (rc < 0) {
		free(dev->id_ascii);
		free(dev);

		return rc;
	}

	string_copy(dev->manufacturer, sizeof(dev->manufacturer), "Tinkerforge GmbH", -1); // FIXME

	dev->product = info->Name;
	dev->product_ascii = string_convert_ascii(info->Name);

	if (dev->product_ascii == nullptr) {
		usbi_log_error(ctx, "Could not convert device name");

		free(dev->id_ascii);
		free(dev);

		return LIBUSB_ERROR_NO_MEM;
	}

	// parse UID from device ID expecting this format:
	// \\?\USB#VID_16D0&PID_063D#6xD12f#{dee824ef-729b-4a0e-9c14-b7117d33a817}
	if (strlen(dev->id_ascii) > strlen(brick_id_prefix1)) {
		if (strncasecmp(dev->id_ascii + strlen(brick_id_prefix1), brick_id_prefix2,
		                strlen(brick_id_prefix2)) == 0) {
			uid_start = dev->id_ascii + strlen(brick_id_prefix1) + strlen(brick_id_prefix2);
		} else if (strncasecmp(dev->id_ascii + strlen(brick_id_prefix1), red_brick_id_prefix2,
		                       strlen(red_brick_id_prefix2)) == 0) {
			uid_start = dev->id_ascii + strlen(brick_id_prefix1) + strlen(red_brick_id_prefix2);
		}
	}

	if (uid_start != nullptr) {
		uid_end = strchr(uid_start, '#');
	}

	if (uid_start != nullptr && uid_end != nullptr) {
		string_copy(dev->serial_number, sizeof(dev->serial_number), uid_start, uid_end - uid_start);
	} else {
		string_copy(dev->serial_number, sizeof(dev->serial_number), "<unknown>", -1);
	}

	usbi_log_debug(ctx, "Created device %p (context: %p, id: %s)",
	               dev, ctx, dev->id_ascii);

	*dev_ptr = dev;

	return LIBUSB_SUCCESS;
}

static void usbi_free_device(libusb_device *dev) {
	usbi_log_debug(dev->ctx, "Destroying device %p (context: %p, id: %s)",
	               dev, dev->ctx, dev->id_ascii);

	free(dev->product_ascii);
	free(dev->id_ascii);
	free(dev);
}

static int usbi_get_device_list(libusb_context *ctx, uint16_t vendor_id,
                                uint16_t product_id, Node *sentinel) {
	int rc = LIBUSB_SUCCESS;
	int *rc_ptr = &rc;
	int length = 0;
	int *length_ptr = &length;

	create_task(DeviceInformation::FindAllAsync(UsbDevice::GetDeviceSelector(vendor_id, product_id)))
	.then([ctx, rc_ptr, sentinel, length_ptr](task<DeviceInformationCollection ^> previous) {
		DeviceInformationCollection ^devices;
		unsigned int i;
		DeviceInformation ^info;
		libusb_device *dev;
		int rc;

		try {
			devices = previous.get();
		} catch (...) { // FIXME: too generic
			usbi_log_error(ctx, "Could not get device list: <exception>"); // FIXME

			*rc_ptr = LIBUSB_ERROR_OTHER;

			return;
		}

		for (i = 0; i < devices->Size; ++i) {
			info = devices->GetAt(i);
			rc = usbi_create_device(ctx, info, &dev);

			if (rc < 0) {
				*rc_ptr = rc;

				return;
			}

			node_insert_before(sentinel, &dev->node);
			++*length_ptr;
		}
	}).wait();

	if (rc < 0) {
		return rc;
	}

	return length;
}

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

	if (usbi_pipe(ctx->event_pipe) < 0) {
		usbi_log_error(ctx, "Could not create transfer pipe for context %p: %s (%d)",
		               ctx, get_errno_name(errno), errno);

		free(ctx);

		return LIBUSB_ERROR_OTHER;
	}

	ctx->event_pollfd.fd = ctx->event_pipe[0];
	ctx->event_pollfd.events = USBI_POLLIN;

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

	usbi_close(ctx->event_pipe[0]);
	usbi_close(ctx->event_pipe[1]);

	free(ctx);
}

void libusb_set_debug(libusb_context *ctx, int level) {
	(void)ctx;
	(void)level;
}

int libusb_pollfds_handle_timeouts(libusb_context *ctx) {
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

void libusb_set_pollfd_notifiers(libusb_context *ctx, libusb_pollfd_added_cb added_cb,
                                 libusb_pollfd_removed_cb removed_cb, void *user_data) {
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

					if (usbi_read(ctx->event_pipe[0], nullptr, 1) != 1) {
						usbi_log_error(ctx, "usbi_read failed: %d", errno); // FIXME
					} else {
						itransfer->triggered = false;
					}
				}

				if (!sparse || transfer->status != LIBUSB_TRANSFER_COMPLETED) {
					usbi_log_debug(ctx, "Read transfer %p [%u] completed (length: %d, status: %d)",
					               transfer, itransfer->sequence_number,
					               transfer->actual_length, transfer->status);

					node_remove(&itransfer->node);

					itransfer->load_operation->Close();

					itransfer->submitted = false;
					itransfer->completed = false;
					itransfer->reader = nullptr;
					itransfer->load_operation = nullptr;

					usbi_log_debug(ctx, "Triggering callback for read transfer %p [%u]",
					               transfer, itransfer->sequence_number);

					transfer->callback(transfer); // might free or submit transfer

					libusb_unref_device(itransfer->transfer.dev_handle->dev);

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

					if (usbi_read(ctx->event_pipe[0], nullptr, 1) != 1) {
						usbi_log_error(ctx, "usbi_read failed: %d", errno); // FIXME
					} else {
						itransfer->triggered = false;
					}
				}

				node_remove(&itransfer->node);

				itransfer->store_operation->Close();

				itransfer->submitted = false;
				itransfer->completed = false;
				itransfer->writer = nullptr;
				itransfer->store_operation = nullptr;

				usbi_log_debug(ctx, "Triggering callback for write transfer %p [%u]",
				               transfer, itransfer->sequence_number);

				transfer->callback(transfer); // might free or submit transfer

				libusb_unref_device(itransfer->transfer.dev_handle->dev);

				++count;
			}

			itransfer_node = itransfer_node_next;
		}

		dev_handle_node = dev_handle_node->next;
	}

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

	// find Bricks
	rc = usbi_get_device_list(ctx, 0x16D0, 0x063D, &sentinel);

	if (rc < 0) {
		goto error;
	}

	length += rc;

	// find RED Bricks
	rc = usbi_get_device_list(ctx, 0x16D0, 0x09E5, &sentinel);

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
                                 struct libusb_device_descriptor *desc) {
	if (dev == nullptr || desc == nullptr) {
		return LIBUSB_ERROR_INVALID_PARAM;
	}

	memcpy(desc, &dev->descriptor->desc, sizeof(struct libusb_device_descriptor));

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

	*config_ptr = dev->descriptor->config;

	return LIBUSB_SUCCESS;
}

void libusb_free_config_descriptor(struct libusb_config_descriptor *config) {
	// nothing to free, because libusb_get_config_descriptor doesn't allocate memory
}

int libusb_open(libusb_device *dev, libusb_device_handle **dev_handle_ptr) {
	libusb_device_handle *dev_handle;

	dev_handle = (libusb_device_handle *)calloc(1, sizeof(libusb_device_handle));

	if (dev_handle == nullptr) {
		usbi_log_error(dev->ctx, "Could not allocate device handle");

		return LIBUSB_ERROR_NO_MEM;
	}

	create_task(UsbDevice::FromIdAsync(dev->id))
	.then([dev, dev_handle](task<UsbDevice ^> previous) {
		try {
			dev_handle->device = previous.get();
		} catch (...) { // FIXME: too generic
			usbi_log_error(dev->ctx, "Could not open device %p (context: %p, id: %s): <exception>", // FIXME
			               dev, dev->ctx, dev->id_ascii);

			dev_handle->device = nullptr;
		}
	}).wait();

	if (dev_handle->device == nullptr) {
		free(dev_handle);

		return LIBUSB_ERROR_NO_DEVICE;
	}

	dev_handle->dev = libusb_ref_device(dev);

	node_reset(&dev_handle->read_itransfer_sentinel);
	node_reset(&dev_handle->write_itransfer_sentinel);

	node_insert_before(&dev->ctx->dev_handle_sentinel, &dev_handle->node);

	*dev_handle_ptr = dev_handle;

	usbi_log_debug(dev->ctx, "Opened device %p (context: %p, id: %s)",
	               dev, dev->ctx, dev->id_ascii);

	return LIBUSB_SUCCESS;
}

void libusb_close(libusb_device_handle *dev_handle) {
	libusb_device *dev = dev_handle->dev;

	usbi_log_debug(dev->ctx, "Closing device %p (context: %p, id: %s)",
	               dev, dev->ctx, dev->id_ascii);

	delete dev_handle->device;

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
	const char *string = nullptr;

	if (desc_index == USBI_STRING_MANUFACTURER) {
		string = dev_handle->dev->manufacturer;
	} else if (desc_index == USBI_STRING_PRODUCT) {
		string = dev_handle->dev->product_ascii;
	} else if (desc_index == USBI_STRING_SERIAL_NUMBER) {
		string = dev_handle->dev->serial_number;
	}

	if (string == nullptr) {
		return LIBUSB_ERROR_INVALID_PARAM;
	}

	length = strnlen(string, length - 1);

	strncpy((char *)data, string, length);

	data[length] = '\0';

	return length;
}

int libusb_claim_interface(libusb_device_handle *dev_handle, int interface_number) {
	return LIBUSB_SUCCESS;
}

int libusb_release_interface(libusb_device_handle *dev_handle, int interface_number) {
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
	itransfer->completed = false;
	itransfer->reader = nullptr;
	itransfer->load_operation = nullptr;
	itransfer->writer = nullptr;
	itransfer->store_operation = nullptr;

	return &itransfer->transfer;
}

int libusb_submit_transfer(struct libusb_transfer *transfer) {
	usbi_transfer *itransfer = (usbi_transfer *)transfer;
	libusb_context *ctx = transfer->dev_handle->dev->ctx;
	unsigned int i;
	UsbInterface ^interface = transfer->dev_handle->device->DefaultInterface;
	UsbBulkInPipe ^pipe_in;
	UsbBulkOutPipe ^pipe_out;
	Array<unsigned char> ^data;

	if (transfer->timeout != 0 || transfer->callback == nullptr) {
		return LIBUSB_ERROR_INVALID_PARAM;
	}

	if (itransfer->submitted) {
		return LIBUSB_ERROR_BUSY;
	}

	if ((transfer->endpoint & LIBUSB_ENDPOINT_IN) != 0) {
		for (i = 0; i < interface->BulkInPipes->Size; ++i) {
			pipe_in = interface->BulkInPipes->GetAt(i);

			if ((LIBUSB_ENDPOINT_IN | pipe_in->EndpointDescriptor->EndpointNumber) == transfer->endpoint) {
				libusb_ref_device(transfer->dev_handle->dev);

				itransfer->submitted = true;
				itransfer->sequence_number = _next_read_itransfer_sequence_number++;
				itransfer->reader = ref new DataReader(pipe_in->InputStream);

				try {
					itransfer->load_operation = itransfer->reader->LoadAsync(transfer->length);
				} catch (...) { // FIXME: too generic
					usbi_log_error(ctx, "Could not submit read transfer %p [%u] (length: %d): <exception>", // FIXME
					               transfer, itransfer->sequence_number, transfer->length);

					return LIBUSB_ERROR_NO_DEVICE; // FIXME: assumes that this happend because of device hotunplug
				}

				itransfer->load_operation->Completed = ref new AsyncOperationCompletedHandler<size_t>(
				[ctx, itransfer](IAsyncOperation<size_t> ^operation, AsyncStatus status) {
					struct libusb_transfer *transfer = &itransfer->transfer;
					Array<unsigned char> ^data;

					usbi_set_transfer_status(transfer, operation, status);

					if (transfer->status == LIBUSB_TRANSFER_COMPLETED) {
						data = ref new Array<unsigned char>(transfer->actual_length);

						itransfer->reader->ReadBytes(data);

						memcpy(transfer->buffer, data->Data, transfer->actual_length);
					}

					itransfer->triggered = true;
					itransfer->completed = true;

					usbi_log_debug(ctx, "Read transfer %p [%u] completed (length: %d, status: %d)",
					               transfer, itransfer->sequence_number,
					               transfer->actual_length, transfer->status);

					if (usbi_write(ctx->event_pipe[1], nullptr, 1) != 1) {
						usbi_log_error(ctx, "usbi_write failed: %d", errno); // FIXME
					}
				});

				node_insert_before(&transfer->dev_handle->read_itransfer_sentinel,
				                   &itransfer->node);

				return LIBUSB_SUCCESS;
			}
		}
	} else {
		for (i = 0; i < interface->BulkOutPipes->Size; ++i) {
			pipe_out = interface->BulkOutPipes->GetAt(i);

			if (pipe_out->EndpointDescriptor->EndpointNumber == transfer->endpoint) {
				libusb_ref_device(transfer->dev_handle->dev);

				itransfer->submitted = true;
				itransfer->sequence_number = _next_write_itransfer_sequence_number++;
				itransfer->writer = ref new DataWriter(pipe_out->OutputStream);

				data = ref new Array<unsigned char>(transfer->length);

				memcpy(data->Data, transfer->buffer, transfer->length);

				itransfer->writer->WriteBytes(data);

				try {
					itransfer->store_operation = itransfer->writer->StoreAsync();
				} catch (...) { // FIXME: too generic
					usbi_log_error(ctx, "Could not submit write transfer %p [%u] (length: %d): <exception>", // FIXME
					               transfer, itransfer->sequence_number, transfer->length);

					return LIBUSB_ERROR_NO_DEVICE; // FIXME: assumes that this happend because of device hotunplug
				}

				itransfer->store_operation->Completed = ref new AsyncOperationCompletedHandler<size_t>(
				[ctx, itransfer](IAsyncOperation<size_t> ^operation, AsyncStatus status) {
					struct libusb_transfer *transfer = &itransfer->transfer;

					usbi_set_transfer_status(transfer, operation, status);

					itransfer->triggered = true;
					itransfer->completed = true;

					usbi_log_debug(ctx, "Write transfer %p [%u] completed (length: %d, status: %d)",
					               transfer, itransfer->sequence_number,
					               transfer->actual_length, transfer->status);

					if (usbi_write(ctx->event_pipe[1], nullptr, 1) != 1) {
						usbi_log_error(ctx, "usbi_write failed: %d", errno); // FIXME
					}
				});

				node_insert_before(&transfer->dev_handle->write_itransfer_sentinel,
				                   &itransfer->node);

				return LIBUSB_SUCCESS;
			}
		}
	}

	return LIBUSB_ERROR_NOT_FOUND;
}

int libusb_cancel_transfer(struct libusb_transfer *transfer) {
	usbi_transfer *itransfer = (usbi_transfer *)transfer;

	if (itransfer->load_operation != nullptr) {
		itransfer->load_operation->Cancel();
	}

	if (itransfer->store_operation != nullptr) {
		itransfer->store_operation->Cancel();
	}

	return LIBUSB_SUCCESS;
}

void libusb_free_transfer(struct libusb_transfer *transfer) {
	usbi_transfer *itransfer = (usbi_transfer *)transfer;

	free(itransfer);
}

void libusb_set_log_function(libusb_log_function function) {
	_log_function = function;
}
