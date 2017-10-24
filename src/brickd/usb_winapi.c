/*
 * brickd
 * Copyright (C) 2013-2014, 2017 Matthias Bolte <matthias@tinkerforge.com>
 *
 * usb_winapi.c: WinAPI based USB specific functions
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
 * brickd comes with its own libusb fork on Windows. therefore, it is not
 * affected by the hotplug race between brickd and libusb 1.0.16. see the long
 * comment in usb_posix.c for details.
 *
 * once libusb gains hotplug support for Windows and the libusb fork bundled
 * with brickd gets updated to include it brickd will also have to used the
 * hotplug handling in libusb on Windows. there is a similar race in event
 * handling to expect as on Linux and Mac OS X.
 */

#include <windows.h>
#include <dbt.h>

#include <daemonlib/event.h>
#include <daemonlib/log.h>

#include "usb.h"

#include "service.h"

static LogSource _log_source = LOG_SOURCE_INITIALIZER;

// general USB device GUID, applies to all Bricks. for the RED Brick this only
// applies to the composite device itself, but not to its functions
static const GUID GUID_DEVINTERFACE_USB_DEVICE =
{ 0xA5DCBF10L, 0x6530, 0x11D2, { 0x90, 0x1F, 0x00, 0xC0, 0x4F, 0xB9, 0x51, 0xED } };

// Brick device GUID (does not apply to the RED Brick). only set by the
// brick.inf driver, not reported by the Brick itself if used driverless since
// Windows 8. therefore it cannot be used as the only way to detect Bricks
static const GUID GUID_DEVINTERFACE_BRICK_DEVICE =
{ 0x870013DDL, 0xFB1D, 0x4BD7, { 0xA9, 0x6C, 0x1F, 0x0B, 0x7D, 0x31, 0xAF, 0x41 } };

// RED Brick device GUID (only applies to the Brick function). set by the
// red_brick.inf driver and reported by the RED Brick itself if used driverless
// since Windows 8. therefore it can be used as the sole way to detect RED Bricks
static const GUID GUID_DEVINTERFACE_RED_BRICK_DEVICE =
{ 0x9536B3B1L, 0x6077, 0x4A3B, { 0x9B, 0xAC, 0x7C, 0x2C, 0xFA, 0x8A, 0x2B, 0xF3 } };

static Pipe _notification_pipe;
static HWND _message_pump_hwnd = NULL;
static Thread _message_pump_thread;
static bool _message_pump_running = false;
static HDEVNOTIFY _notification_handle = NULL;

static void usb_forward_notifications(void *opaque) {
	uint8_t byte;

	(void)opaque;

	if (pipe_read(&_notification_pipe, &byte, sizeof(byte)) < 0) {
		log_error("Could not read from notification pipe: %s (%d)",
		          get_errno_name(errno), errno);

		return;
	}

	usb_rescan();
}


/*static*/ void usb_handle_device_event(DWORD event_type,
                                        DEV_BROADCAST_DEVICEINTERFACE *event_data) {
	bool possibly_brick = false;
	bool definitely_brick = false;
	bool definitely_red_brick = false;
	const char *brick_name_prefix1 = "\\\\?\\USB\\"; // according to libusb: "\\?\" == "\\.\" == "##?#" == "##.#" and "\" == "#"
	const char *brick_name_prefix2 = "VID_16D0&PID_063D"; // according to libusb: "Vid_" == "VID_"
	const char *red_brick_name_prefix2 = "VID_16D0&PID_09E5"; // according to libusb: "Vid_" == "VID_"
	char buffer[1024] = "<unknown>";
	int rc;
	char *name;
	char guid[64] = "<unknown>";
	uint8_t byte = 0;

	// check event type
	if (event_type != DBT_DEVICEARRIVAL && event_type != DBT_DEVICEREMOVECOMPLETE) {
		return;
	}

	// check class GUID
	if (memcmp(&event_data->dbcc_classguid,
	           &GUID_DEVINTERFACE_USB_DEVICE, sizeof(GUID)) == 0) {
		possibly_brick = true;
	} else if (memcmp(&event_data->dbcc_classguid,
	                  &GUID_DEVINTERFACE_BRICK_DEVICE, sizeof(GUID)) == 0) {
		definitely_brick = true;
	} else if (memcmp(&event_data->dbcc_classguid,
	                  &GUID_DEVINTERFACE_RED_BRICK_DEVICE, sizeof(GUID)) == 0) {
		definitely_red_brick = true;
	} else {
		return;
	}

	// convert name
	if (service_get_status_handle() != NULL) {
		if (WideCharToMultiByte(CP_UTF8, 0, (LPCWSTR)&event_data->dbcc_name[0],
		                        -1, buffer, sizeof(buffer), NULL, NULL) == 0) {
			rc = ERRNO_WINAPI_OFFSET + GetLastError();

			log_error("Could not convert device name: %s (%d)",
			          get_errno_name(rc), rc);

			return;
		}

		name = buffer;
	} else {
		name = event_data->dbcc_name;
	}

	if (possibly_brick) {
		// check if name contains (RED) Brick vendor and product ID
		if (strlen(name) > strlen(brick_name_prefix1)) {
			if (strncasecmp(name + strlen(brick_name_prefix1), brick_name_prefix2,
			                strlen(brick_name_prefix2)) == 0) {
				definitely_brick = true;
			} else if (strncasecmp(name + strlen(brick_name_prefix1), red_brick_name_prefix2,
			                       strlen(red_brick_name_prefix2)) == 0) {
				definitely_red_brick = true;
			}
		}
	}

	if (!definitely_brick && !definitely_red_brick) {
		return;
	}

	snprintf(guid, sizeof(guid),
	         "{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
	         event_data->dbcc_classguid.Data1,
	         event_data->dbcc_classguid.Data2,
	         event_data->dbcc_classguid.Data3,
	         event_data->dbcc_classguid.Data4[0],
	         event_data->dbcc_classguid.Data4[1],
	         event_data->dbcc_classguid.Data4[2],
	         event_data->dbcc_classguid.Data4[3],
	         event_data->dbcc_classguid.Data4[4],
	         event_data->dbcc_classguid.Data4[5],
	         event_data->dbcc_classguid.Data4[6],
	         event_data->dbcc_classguid.Data4[7]);

	log_debug("Received device notification (type: %s, guid: %s, name: %s)",
	          event_type == DBT_DEVICEARRIVAL ? "arrival" : "removal",
	          guid, name);

	if (pipe_write(&_notification_pipe, &byte, sizeof(byte)) < 0) {
		log_error("Could not write to notification pipe: %s (%d)",
		          get_errno_name(errno), errno);
	}
}

static LRESULT CALLBACK usb_message_pump_window_proc(HWND hwnd, UINT msg,
                                                     WPARAM wparam,
                                                     LPARAM lparam) {
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
		usb_handle_device_event(wparam, (DEV_BROADCAST_DEVICEINTERFACE *)lparam);

		return TRUE;
	}

	return DefWindowProc(hwnd, msg, wparam, lparam);
}

static void usb_message_pump_thread_proc(void *opaque) {
	const char *class_name = "tinkerforge-brick-daemon-message-pump";
	Semaphore *handshake = opaque;
	WNDCLASSEX wc;
	int rc;
	MSG msg;

	log_debug("Started message pump thread");

	memset(&wc, 0, sizeof(wc));

	wc.cbSize = sizeof(wc);
	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc = (WNDPROC)usb_message_pump_window_proc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = NULL;
	wc.hIcon = NULL;
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)COLOR_WINDOW;
	wc.lpszMenuName = NULL;
	wc.lpszClassName = class_name;
	wc.hIconSm = NULL;

	if (RegisterClassEx(&wc) == 0) {
		rc = ERRNO_WINAPI_OFFSET + GetLastError();

		log_error("Could not register message pump window class: %s (%d)",
		          get_errno_name(rc), rc);

		goto cleanup;
	}

	_message_pump_hwnd = CreateWindowEx(0, class_name, "brickd message pump",
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

	while (_message_pump_running &&
	       (rc = GetMessage(&msg, _message_pump_hwnd, 0, 0)) != 0) {
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
			DispatchMessage(&msg);
		}
	}

	log_debug("Stopped message pump thread");

cleanup:
	if (!_message_pump_running) {
		// need to release the handshake in all cases, otherwise
		// message_pump_start will block forever in semaphore_acquire
		semaphore_release(handshake);
	}

	_message_pump_running = false;
}

static int usb_message_pump_start(void) {
	Semaphore handshake;

	log_debug("Starting message pump thread");

	semaphore_create(&handshake);

	thread_create(&_message_pump_thread, usb_message_pump_thread_proc, &handshake);

	semaphore_acquire(&handshake);
	semaphore_destroy(&handshake);

	if (!_message_pump_running) {
		thread_destroy(&_message_pump_thread);

		log_error("Could not start message pump thread");

		return -1;
	}

	return 0;
}

static void usb_message_pump_stop(void) {
	int rc;

	log_debug("Stopping message pump");

	_message_pump_running = false;

	if (!PostMessage(_message_pump_hwnd, WM_USER, 0, 0)) {
		rc = ERRNO_WINAPI_OFFSET + GetLastError();

		log_warn("Could not trigger destruction of message pump window: %s (%d)",
		         get_errno_name(rc), rc);
	} else {
		thread_join(&_message_pump_thread);
	}

	thread_destroy(&_message_pump_thread);
}

int usb_init_platform(void) {
	return 0;
}

void usb_exit_platform(void) {
}

int usb_init_hotplug(libusb_context *context) {
	int phase = 0;
	int rc;
	SERVICE_STATUS_HANDLE service_status_handle;
	DEV_BROADCAST_DEVICEINTERFACE notification_filter;

	(void)context;

	// create notification pipe
	if (pipe_create(&_notification_pipe, 0) < 0) {
		log_error("Could not create hotplug pipe: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 1;

	if (event_add_source(_notification_pipe.base.read_handle, EVENT_SOURCE_TYPE_GENERIC,
	                     EVENT_READ, usb_forward_notifications, NULL) < 0) {
		goto cleanup;
	}

	phase = 2;

	// start message pump, if necessary
	service_status_handle = service_get_status_handle();

	if (service_status_handle == NULL) {
		if (usb_message_pump_start() < 0) {
			goto cleanup;
		}
	}

	phase = 3;

	// register for notifications
	memset(&notification_filter, 0, sizeof(notification_filter));

	notification_filter.dbcc_size = sizeof(notification_filter);
	notification_filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;

	if (service_status_handle != NULL) {
		_notification_handle = RegisterDeviceNotification((HANDLE)service_status_handle,
		                                                  &notification_filter,
		                                                  DEVICE_NOTIFY_SERVICE_HANDLE |
		                                                  DEVICE_NOTIFY_ALL_INTERFACE_CLASSES);
	} else {
		_notification_handle = RegisterDeviceNotification(_message_pump_hwnd,
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

	phase = 4;

cleanup:
	switch (phase) { // no breaks, all cases fall through intentionally
	case 3:
		if (_message_pump_running) {
			usb_message_pump_stop();
		}

	case 2:
		event_remove_source(_notification_pipe.base.read_handle,
		                    EVENT_SOURCE_TYPE_GENERIC);

	case 1:
		pipe_destroy(&_notification_pipe);

	default:
		break;
	}

	return phase == 4 ? 0 : -1;
}

void usb_exit_hotplug(libusb_context *context) {
	(void)context;

	UnregisterDeviceNotification(_notification_handle);

	if (_message_pump_running) {
		usb_message_pump_stop();
	}

	event_remove_source(_notification_pipe.base.read_handle, EVENT_SOURCE_TYPE_GENERIC);
	pipe_destroy(&_notification_pipe);
}

bool usb_has_hotplug(void) {
	return true;
}
