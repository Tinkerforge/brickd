/*
 * brickd
 * Copyright (C) 2013-2014, 2017-2021 Matthias Bolte <matthias@tinkerforge.com>
 *
 * usb_winapi.c: WinAPI specific USB functions
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
 * once libusb gains hotplug support for Windows and the libusb fork bundled
 * with brickd gets updated to include it brickd will also have to used the
 * hotplug handling in libusb on Windows. otherwise there is a race condition
 * between libusb and brickd noticing the same hotplug event. if brickd notices
 * the event first then libusb might not have updated its device list resulting
 * in brickd not seeing a change between libusb_get_device_list calls and
 * missing the device arrival/removal.
 */

#include <windows.h>
#include <dbt.h>
#include <libusb.h>

#include <daemonlib/event.h>
#include <daemonlib/log.h>
#include <daemonlib/pipe.h>
#include <daemonlib/threads.h>

#include "usb.h"
#include "usb_transfer.h"

#include "service.h"
#include "usb_windows.h"

static LogSource _log_source = LOG_SOURCE_INITIALIZER;

static Pipe _transfer_pipe;
static HWND _message_pump_hwnd;
static Thread _message_pump_thread;
static bool _message_pump_running;
static HDEVNOTIFY _notification_handle;
static bool _usb_event_running;
static Thread _usb_event_thread;

extern void usb_transfer_finish(struct libusb_transfer *handle);

static int usb_forward_transfer_internal(bool silent_errno_would_block) {
	struct libusb_transfer *handle;

	// FIXME: partial read?
	if (pipe_read(&_transfer_pipe, &handle, sizeof(handle)) < 0) {
		if (errno_would_block() && silent_errno_would_block) {
			return 1;
		}

		log_error("Could not read from USB transfer pipe: %s (%d)",
		          get_errno_name(errno), errno);

		return -1;
	}

	usb_transfer_finish(handle);

	return 0;
}

static void usb_forward_transfer(void *opaque) {
	(void)opaque;

	usb_forward_transfer_internal(false);
}

void LIBUSB_CALL usb_transfer_callback(struct libusb_transfer *handle) {
	// FIXME: partial write?
	if (pipe_write(&_transfer_pipe, &handle, sizeof(handle)) < 0) {
		log_error("Could not append finished USB transfer (handle: %p) to USB transfer pipe: %s (%d)",
		          handle, get_errno_name(errno), errno);

		return;
	}

	log_debug("Append finished USB transfer (handle: %p) to USB transfer pipe", handle);
}

void usb_handle_device_event(DWORD event_type, DEV_BROADCAST_HDR *event_data) {
	DEV_BROADCAST_DEVICEINTERFACE_A *event_data_a;
	USBHotplugType type;
	char buffer[1024] = "<unknown>";
	int rc;
	char *name;

	if (event_data->dbch_devicetype != DBT_DEVTYP_DEVICEINTERFACE) {
		return;
	}

	event_data_a = (DEV_BROADCAST_DEVICEINTERFACE_A *)event_data;

	switch (event_type) {
	case DBT_DEVICEARRIVAL:
		type = USB_HOTPLUG_TYPE_ARRIVAL;
		break;

	case DBT_DEVICEREMOVECOMPLETE:
		type = USB_HOTPLUG_TYPE_REMOVAL;
		break;

	default:
		return;
	}

	if (service_get_status_handle() != NULL) {
		// services always receive the DEV_BROADCAST_DEVICEINTERFACE_W flavor
		if (WideCharToMultiByte(CP_UTF8, 0, (LPCWSTR)event_data_a->dbcc_name,
		                        -1, buffer, sizeof(buffer), NULL, NULL) == 0) {
			rc = ERRNO_WINAPI_OFFSET + GetLastError();

			log_error("Could not convert device name to UTF-8: %s (%d)",
			          get_errno_name(rc), rc);

			return;
		}

		name = buffer;
	} else {
		name = event_data_a->dbcc_name;
	}

	if (!usb_check_hotplug_event(type, &event_data_a->dbcc_classguid, name)) {
		return;
	}

	usb_handle_hotplug();
}

static LRESULT CALLBACK usb_window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
	int rc;

	switch (msg) {
	case WM_USER:
		log_debug("Destroying message pump window");

		if (!DestroyWindow(hwnd)) {
			rc = ERRNO_WINAPI_OFFSET + GetLastError();

			log_warn("Could not destroy message pump window: %s (%d)",
			         get_errno_name(rc), rc);
		}

		return 0;

	case WM_DESTROY:
		log_debug("Posting quit message message loop");

		PostQuitMessage(0);

		return 0;

	case WM_DEVICECHANGE:
		usb_handle_device_event(wparam, (DEV_BROADCAST_HDR *)lparam);

		return TRUE;
	}

	return DefWindowProc(hwnd, msg, wparam, lparam);
}

static void usb_pump_messages(void *opaque) {
	const char *class_name = "tinkerforge-brick-daemon-message-pump";
	Semaphore *handshake = opaque;
	bool handshake_released = false;
	WNDCLASSEXA wc;
	int rc;
	MSG msg;

	log_debug("Started message pump thread");

	memset(&wc, 0, sizeof(wc));

	wc.cbSize = sizeof(wc);
	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc = (WNDPROC)usb_window_proc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = NULL;
	wc.hIcon = NULL;
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)COLOR_WINDOW;
	wc.lpszMenuName = NULL;
	wc.lpszClassName = class_name;
	wc.hIconSm = NULL;

	if (RegisterClassExA(&wc) == 0) {
		rc = ERRNO_WINAPI_OFFSET + GetLastError();

		log_error("Could not register message pump window class: %s (%d)",
		          get_errno_name(rc), rc);

		goto cleanup;
	}

	_message_pump_hwnd = CreateWindowExA(0, class_name, "brickd message pump",
	                                     0, 0, 0, 0, 0, HWND_MESSAGE,
	                                     NULL, NULL, NULL);

	if (_message_pump_hwnd == NULL) {
		rc = ERRNO_WINAPI_OFFSET + GetLastError();

		log_error("Could not create message pump window: %s (%d)",
		          get_errno_name(rc), rc);

		goto cleanup;
	}

	_message_pump_running = true;

	semaphore_release(handshake);

	handshake_released = true;

	while (_message_pump_running &&
	       (rc = GetMessageA(&msg, _message_pump_hwnd, 0, 0)) != 0) {
		if (rc < 0) {
			rc = ERRNO_WINAPI_OFFSET + GetLastError();

			if (rc == ERRNO_WINAPI_OFFSET + ERROR_INVALID_WINDOW_HANDLE) {
				log_debug("Message pump window seems to be destroyed");

				break;
			}

			log_warn("Could not get window message: %s (%d)",
			         get_errno_name(rc), rc);
		} else {
			TranslateMessage(&msg);
			DispatchMessageA(&msg);
		}
	}

	log_debug("Stopped message pump thread");

cleanup:
	if (!handshake_released) {
		// need to release the handshake in all cases, otherwise
		// message_pump_start will block forever in semaphore_acquire
		semaphore_release(handshake);
	}

	_message_pump_running = false;
}

static int usb_start_message_pump(void) {
	Semaphore handshake;

	log_debug("Starting message pump thread");

	_message_pump_running = false;

	semaphore_create(&handshake);

	thread_create(&_message_pump_thread, usb_pump_messages, &handshake);

	semaphore_acquire(&handshake);
	semaphore_destroy(&handshake);

	if (!_message_pump_running) {
		thread_destroy(&_message_pump_thread);

		log_error("Could not start message pump thread");

		return -1;
	}

	return 0;
}

static void usb_stop_message_pump(void) {
	int rc;

	log_debug("Stopping message pump");

	_message_pump_running = false;

	if (!PostMessageA(_message_pump_hwnd, WM_USER, 0, 0)) {
		rc = ERRNO_WINAPI_OFFSET + GetLastError();

		log_warn("Could not trigger destruction of message pump window: %s (%d)",
		         get_errno_name(rc), rc);
	} else {
		thread_join(&_message_pump_thread);
	}

	thread_destroy(&_message_pump_thread);
}

static void usb_handle_events_internal(void *opaque) {
	libusb_context *context = opaque;
	int rc;
	struct timeval tv;

	log_debug("Started USB event handler thread");

	while (_usb_event_running) {
		// nothing special about INT32_MAX / 2 here, it's just a big number
		// that should fit into a timeval safely
		tv.tv_sec = INT32_MAX / 2;
		tv.tv_usec = 0;

		rc = libusb_handle_events_timeout(context, &tv);

		if (rc < 0) {
			log_warn("Could not handle USB events: %s (%d)",
			         usb_get_error_name(rc), rc);
		}
	}

	log_debug("Stopped USB event handler thread");
}

int usb_init_platform(libusb_context *context) {
	int phase = 0;
	int rc;
	SERVICE_STATUS_HANDLE service_status_handle;
	DEV_BROADCAST_DEVICEINTERFACE notification_filter;

	_message_pump_running = false;

	// create transfer pipe
	if (pipe_create(&_transfer_pipe, PIPE_FLAG_NON_BLOCKING_READ) < 0) {
		log_error("Could not create USB transfer pipe: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 1;

	if (event_add_source(_transfer_pipe.base.read_handle, EVENT_SOURCE_TYPE_GENERIC,
	                     "usb-transfer", EVENT_READ, usb_forward_transfer, NULL) < 0) {
		goto cleanup;
	}

	phase = 2;

	// start message pump, if necessary
	service_status_handle = service_get_status_handle();

	if (service_status_handle == NULL) {
		if (usb_start_message_pump() < 0) {
			goto cleanup;
		}
	}

	phase = 3;

	// register for notifications
	memset(&notification_filter, 0, sizeof(notification_filter));

	notification_filter.dbcc_size = sizeof(notification_filter);
	notification_filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;

	if (service_status_handle != NULL) {
		_notification_handle = RegisterDeviceNotificationA((HANDLE)service_status_handle,
		                                                   &notification_filter,
		                                                   DEVICE_NOTIFY_SERVICE_HANDLE |
		                                                   DEVICE_NOTIFY_ALL_INTERFACE_CLASSES);
	} else {
		_notification_handle = RegisterDeviceNotificationA(_message_pump_hwnd,
		                                                   &notification_filter,
		                                                   DEVICE_NOTIFY_WINDOW_HANDLE |
		                                                   DEVICE_NOTIFY_ALL_INTERFACE_CLASSES);
	}

	if (_notification_handle == NULL) {
		rc = ERRNO_WINAPI_OFFSET + GetLastError();

		log_error("Could not register for device notification: %s (%d)",
		          get_errno_name(rc), rc);

		goto cleanup;
	}

	// start event handler
	_usb_event_running = true;

	thread_create(&_usb_event_thread, usb_handle_events_internal, context);

	phase = 4;

cleanup:
	switch (phase) { // no breaks, all cases fall through intentionally
	case 3:
		if (_message_pump_running) {
			usb_stop_message_pump();
		}

		// fall through

	case 2:
		event_remove_source(_transfer_pipe.base.read_handle, EVENT_SOURCE_TYPE_GENERIC);
		// fall through

	case 1:
		pipe_destroy(&_transfer_pipe);
		// fall through

	default:
		break;
	}

	return phase == 4 ? 0 : -1;
}

void usb_exit_platform(libusb_context *context) {
	log_debug("Stopping USB event handler thread");

	_usb_event_running = false;

	libusb_interrupt_event_handler(context);
	thread_join(&_usb_event_thread);
	thread_destroy(&_usb_event_thread);

	UnregisterDeviceNotification(_notification_handle);

	if (_message_pump_running) {
		usb_stop_message_pump();
	}

	event_remove_source(_transfer_pipe.base.read_handle, EVENT_SOURCE_TYPE_GENERIC);
	pipe_destroy(&_transfer_pipe);
}

void usb_handle_events_platform(libusb_context *context) {
	(void)context;

	microsleep(0); // give USB event handler thread a chance

	while (usb_forward_transfer_internal(true) == 0) {}
}
