/*
 * brickd
 * Copyright (C) 2016-2017 Matthias Bolte <matthias@tinkerforge.com>
 *
 * usb_uwp.c: Universal Windows Platform USB hotplug implementation
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

//#include <cfgmgr32.h>

#include <daemonlib/event.h>
#include <daemonlib/log.h>
#include <daemonlib/pipe.h>
#include <daemonlib/utils.h>

#include "usb.h"

#include "usb_windows.h"

// BEGIN: cfgmgr32.h

typedef DWORD CONFIGRET;

#define CR_SUCCESS                  0x00000000
#define CR_DEFAULT                  0x00000001
#define CR_OUT_OF_MEMORY            0x00000002
#define CR_INVALID_POINTER          0x00000003
#define CR_INVALID_FLAG             0x00000004
#define CR_INVALID_DEVNODE          0x00000005
#define CR_INVALID_RES_DES          0x00000006
#define CR_INVALID_LOG_CONF         0x00000007
#define CR_INVALID_ARBITRATOR       0x00000008
#define CR_INVALID_NODELIST         0x00000009
#define CR_DEVNODE_HAS_REQS         0x0000000A
#define CR_INVALID_RESOURCEID       0x0000000B
#define CR_DLVXD_NOT_FOUND          0x0000000C
#define CR_NO_SUCH_DEVNODE          0x0000000D
#define CR_NO_MORE_LOG_CONF         0x0000000E
#define CR_NO_MORE_RES_DES          0x0000000F
#define CR_ALREADY_SUCH_DEVNODE     0x00000010
#define CR_INVALID_RANGE_LIST       0x00000011
#define CR_INVALID_RANGE            0x00000012
#define CR_FAILURE                  0x00000013
#define CR_NO_SUCH_LOGICAL_DEV      0x00000014
#define CR_CREATE_BLOCKED           0x00000015
#define CR_NOT_SYSTEM_VM            0x00000016
#define CR_REMOVE_VETOED            0x00000017
#define CR_APM_VETOED               0x00000018
#define CR_INVALID_LOAD_TYPE        0x00000019
#define CR_BUFFER_SMALL             0x0000001A
#define CR_NO_ARBITRATOR            0x0000001B
#define CR_NO_REGISTRY_HANDLE       0x0000001C
#define CR_REGISTRY_ERROR           0x0000001D
#define CR_INVALID_DEVICE_ID        0x0000001E
#define CR_INVALID_DATA             0x0000001F
#define CR_INVALID_API              0x00000020
#define CR_DEVLOADER_NOT_READY      0x00000021
#define CR_NEED_RESTART             0x00000022
#define CR_NO_MORE_HW_PROFILES      0x00000023
#define CR_DEVICE_NOT_THERE         0x00000024
#define CR_NO_SUCH_VALUE            0x00000025
#define CR_WRONG_TYPE               0x00000026
#define CR_INVALID_PRIORITY         0x00000027
#define CR_NOT_DISABLEABLE          0x00000028
#define CR_FREE_RESOURCES           0x00000029
#define CR_QUERY_VETOED             0x0000002A
#define CR_CANT_SHARE_IRQ           0x0000002B
#define CR_NO_DEPENDENT             0x0000002C
#define CR_SAME_RESOURCES           0x0000002D
#define CR_NO_SUCH_REGISTRY_KEY     0x0000002E
#define CR_INVALID_MACHINENAME      0x0000002F
#define CR_REMOTE_COMM_FAILURE      0x00000030
#define CR_MACHINE_UNAVAILABLE      0x00000031
#define CR_NO_CM_SERVICES           0x00000032
#define CR_ACCESS_DENIED            0x00000033
#define CR_CALL_NOT_IMPLEMENTED     0x00000034
#define CR_INVALID_PROPERTY         0x00000035
#define CR_DEVICE_INTERFACE_ACTIVE  0x00000036
#define CR_NO_SUCH_DEVICE_INTERFACE 0x00000037
#define CR_INVALID_REFERENCE_STRING 0x00000038
#define CR_INVALID_CONFLICT_LIST    0x00000039
#define CR_INVALID_INDEX            0x0000003A
#define CR_INVALID_STRUCTURE_SIZE   0x0000003B

#define CM_NOTIFY_FILTER_FLAG_ALL_INTERFACE_CLASSES     0x00000001
#define CM_NOTIFY_FILTER_FLAG_ALL_DEVICE_INSTANCES      0x00000002

DECLARE_HANDLE(HCMNOTIFICATION);
typedef HCMNOTIFICATION *PHCMNOTIFICATION;

typedef enum _CM_NOTIFY_ACTION {
	CM_NOTIFY_ACTION_DEVICEINTERFACEARRIVAL = 0,
	CM_NOTIFY_ACTION_DEVICEINTERFACEREMOVAL,
	CM_NOTIFY_ACTION_DEVICEQUERYREMOVE,
	CM_NOTIFY_ACTION_DEVICEQUERYREMOVEFAILED,
	CM_NOTIFY_ACTION_DEVICEREMOVEPENDING,
	CM_NOTIFY_ACTION_DEVICEREMOVECOMPLETE,
	CM_NOTIFY_ACTION_DEVICECUSTOMEVENT,
	CM_NOTIFY_ACTION_DEVICEINSTANCEENUMERATED,
	CM_NOTIFY_ACTION_DEVICEINSTANCESTARTED,
	CM_NOTIFY_ACTION_DEVICEINSTANCEREMOVED,
} CM_NOTIFY_ACTION, *PCM_NOTIFY_ACTION;

typedef enum _CM_NOTIFY_FILTER_TYPE {
	CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE = 0,
	CM_NOTIFY_FILTER_TYPE_DEVICEHANDLE,
	CM_NOTIFY_FILTER_TYPE_DEVICEINSTANCE,
} CM_NOTIFY_FILTER_TYPE, *PCM_NOTIFY_FILTER_TYPE;

typedef struct _CM_NOTIFY_EVENT_DATA {
	CM_NOTIFY_FILTER_TYPE    FilterType;
	DWORD                    Reserved;
	union {
		struct {
			GUID    ClassGuid;
			WCHAR   SymbolicLink[ANYSIZE_ARRAY];
		} DeviceInterface;
		struct {
			GUID    EventGuid;
			LONG    NameOffset;
			DWORD   DataSize;
			BYTE    Data[ANYSIZE_ARRAY];
		} DeviceHandle;
		struct {
			WCHAR   InstanceId[ANYSIZE_ARRAY];
		} DeviceInstance;
	} u;
} CM_NOTIFY_EVENT_DATA, *PCM_NOTIFY_EVENT_DATA;

#define MAX_DEVICE_ID_LEN 200

typedef struct _CM_NOTIFY_FILTER {
	DWORD                    cbSize;
	DWORD                    Flags;
	CM_NOTIFY_FILTER_TYPE    FilterType;
	DWORD                    Reserved;
	union {
		struct {
			GUID    ClassGuid;
		} DeviceInterface;
		struct {
			HANDLE  hTarget;
		} DeviceHandle;
		struct {
			WCHAR   InstanceId[MAX_DEVICE_ID_LEN];
		} DeviceInstance;
	} u;
} CM_NOTIFY_FILTER, *PCM_NOTIFY_FILTER;

typedef __callback DWORD (CALLBACK *PCM_NOTIFY_CALLBACK)(
	_In_ HCMNOTIFICATION       hNotify,
	_In_opt_ PVOID             Context,
	_In_ CM_NOTIFY_ACTION      Action,
	_In_reads_bytes_(EventDataSize) PCM_NOTIFY_EVENT_DATA EventData,
	_In_ DWORD                 EventDataSize
);

CMAPI CONFIGRET WINAPI CM_Register_Notification(
	_In_    PCM_NOTIFY_FILTER       pFilter,
	_In_opt_ PVOID                  pContext,
	_In_    PCM_NOTIFY_CALLBACK     pCallback,
	_Out_   PHCMNOTIFICATION        pNotifyContext
);

CMAPI CONFIGRET WINAPI CM_Unregister_Notification(
	_In_    HCMNOTIFICATION NotifyContext
);

// END: cfgmgr32.h

static LogSource _log_source = LOG_SOURCE_INITIALIZER;

static Pipe _notification_pipe;
static HCMNOTIFICATION _notification_handle;

const char *get_configret_name(int configret) {
#define CONFIGRET_NAME(code) case code: return #code

	switch (configret) {
	CONFIGRET_NAME(CR_SUCCESS);
	CONFIGRET_NAME(CR_DEFAULT);
	CONFIGRET_NAME(CR_OUT_OF_MEMORY);
	CONFIGRET_NAME(CR_INVALID_POINTER);
	CONFIGRET_NAME(CR_INVALID_FLAG);
	CONFIGRET_NAME(CR_INVALID_DEVNODE);
	CONFIGRET_NAME(CR_INVALID_RES_DES);
	CONFIGRET_NAME(CR_INVALID_LOG_CONF);
	CONFIGRET_NAME(CR_INVALID_ARBITRATOR);
	CONFIGRET_NAME(CR_INVALID_NODELIST);
	CONFIGRET_NAME(CR_DEVNODE_HAS_REQS);
	CONFIGRET_NAME(CR_INVALID_RESOURCEID);
	CONFIGRET_NAME(CR_DLVXD_NOT_FOUND);
	CONFIGRET_NAME(CR_NO_SUCH_DEVNODE);
	CONFIGRET_NAME(CR_NO_MORE_LOG_CONF);
	CONFIGRET_NAME(CR_NO_MORE_RES_DES);
	CONFIGRET_NAME(CR_ALREADY_SUCH_DEVNODE);
	CONFIGRET_NAME(CR_INVALID_RANGE_LIST);
	CONFIGRET_NAME(CR_INVALID_RANGE);
	CONFIGRET_NAME(CR_FAILURE);
	CONFIGRET_NAME(CR_NO_SUCH_LOGICAL_DEV);
	CONFIGRET_NAME(CR_CREATE_BLOCKED);
	CONFIGRET_NAME(CR_NOT_SYSTEM_VM);
	CONFIGRET_NAME(CR_REMOVE_VETOED);
	CONFIGRET_NAME(CR_APM_VETOED);
	CONFIGRET_NAME(CR_INVALID_LOAD_TYPE);
	CONFIGRET_NAME(CR_BUFFER_SMALL);
	CONFIGRET_NAME(CR_NO_ARBITRATOR);
	CONFIGRET_NAME(CR_NO_REGISTRY_HANDLE);
	CONFIGRET_NAME(CR_REGISTRY_ERROR);
	CONFIGRET_NAME(CR_INVALID_DEVICE_ID);
	CONFIGRET_NAME(CR_INVALID_DATA);
	CONFIGRET_NAME(CR_INVALID_API);
	CONFIGRET_NAME(CR_DEVLOADER_NOT_READY);
	CONFIGRET_NAME(CR_NEED_RESTART);
	CONFIGRET_NAME(CR_NO_MORE_HW_PROFILES);
	CONFIGRET_NAME(CR_DEVICE_NOT_THERE);
	CONFIGRET_NAME(CR_NO_SUCH_VALUE);
	CONFIGRET_NAME(CR_WRONG_TYPE);
	CONFIGRET_NAME(CR_INVALID_PRIORITY);
	CONFIGRET_NAME(CR_NOT_DISABLEABLE);
	CONFIGRET_NAME(CR_FREE_RESOURCES);
	CONFIGRET_NAME(CR_QUERY_VETOED);
	CONFIGRET_NAME(CR_CANT_SHARE_IRQ);
	CONFIGRET_NAME(CR_NO_DEPENDENT);
	CONFIGRET_NAME(CR_SAME_RESOURCES);
	CONFIGRET_NAME(CR_NO_SUCH_REGISTRY_KEY);
	CONFIGRET_NAME(CR_INVALID_MACHINENAME);
	CONFIGRET_NAME(CR_REMOTE_COMM_FAILURE);
	CONFIGRET_NAME(CR_MACHINE_UNAVAILABLE);
	CONFIGRET_NAME(CR_NO_CM_SERVICES);
	CONFIGRET_NAME(CR_ACCESS_DENIED);
	CONFIGRET_NAME(CR_CALL_NOT_IMPLEMENTED);
	CONFIGRET_NAME(CR_INVALID_PROPERTY);
	CONFIGRET_NAME(CR_DEVICE_INTERFACE_ACTIVE);
	CONFIGRET_NAME(CR_NO_SUCH_DEVICE_INTERFACE);
	CONFIGRET_NAME(CR_INVALID_REFERENCE_STRING);
	CONFIGRET_NAME(CR_INVALID_CONFLICT_LIST);
	CONFIGRET_NAME(CR_INVALID_INDEX);
	CONFIGRET_NAME(CR_INVALID_STRUCTURE_SIZE);

	default: return "<unknown>";
	}

#undef CONFIGRET_NAME
}

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

static DWORD CALLBACK usb_handle_notify_event(HCMNOTIFICATION hnotify,
                                              void *context,
                                              CM_NOTIFY_ACTION action,
                                              CM_NOTIFY_EVENT_DATA *event_data,
                                              DWORD event_data_size) {
	USBHotplugType type;
	char name[1024] = "<unknown>";
	int rc;
	uint8_t byte = 0;

	(void)hnotify;
	(void)context;
	(void)event_data_size;

	switch (action) {
	case CM_NOTIFY_ACTION_DEVICEINTERFACEARRIVAL:
		type = USB_HOTPLUG_TYPE_ARRIVAL;
		break;

	case CM_NOTIFY_ACTION_DEVICEINTERFACEREMOVAL:
		type = USB_HOTPLUG_TYPE_REMOVAL;
		break;

	default:
		return ERROR_SUCCESS;
	}

	if (event_data->FilterType != CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE) {
		return ERROR_SUCCESS;
	}

	if (WideCharToMultiByte(CP_UTF8, 0, event_data->u.DeviceInterface.SymbolicLink,
	                        -1, name, sizeof(name), NULL, NULL) == 0) {
		rc = ERRNO_WINAPI_OFFSET + GetLastError();

		log_error("Could not convert device interface symbolic link to UTF-8: %s (%d)",
		          get_errno_name(rc), rc);

		return ERROR_SUCCESS;
	}

	if (!usb_check_hotplug_event(type, &event_data->u.DeviceInterface.ClassGuid, name)) {
		return ERROR_SUCCESS;
	}

	if (pipe_write(&_notification_pipe, &byte, sizeof(byte)) < 0) {
		log_error("Could not write to notification pipe: %s (%d)",
		          get_errno_name(errno), errno);
	}

	return ERROR_SUCCESS;
}

int usb_init_platform(void) {
	return 0;
}

void usb_exit_platform(void) {
}

int usb_init_hotplug(libusb_context *context) {
	int phase = 0;
	CM_NOTIFY_FILTER notify_filter;
	CONFIGRET cr;

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

	// register for notifications
	memset(&notify_filter, 0, sizeof(notify_filter));

	notify_filter.cbSize = sizeof(notify_filter);
	notify_filter.Flags = CM_NOTIFY_FILTER_FLAG_ALL_INTERFACE_CLASSES;
	notify_filter.FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE;

	cr = CM_Register_Notification(&notify_filter, NULL,
	                              usb_handle_notify_event, &_notification_handle);

	if (cr != CR_SUCCESS) {
		log_error("Could not register configuration manager notification: %s (%d)",
		          get_configret_name(cr), cr);

		goto cleanup;
	}

	phase = 3;

cleanup:
	switch (phase) { // no breaks, all cases fall through intentionally
	case 2:
		event_remove_source(_notification_pipe.base.read_handle, EVENT_SOURCE_TYPE_GENERIC);

	case 1:
		pipe_destroy(&_notification_pipe);

	default:
		break;
	}

	return phase == 3 ? 0 : -1;
}

void usb_exit_hotplug(libusb_context *context) {
	CONFIGRET cr;

	(void)context;

	cr = CM_Unregister_Notification(_notification_handle);

	if (cr != CR_SUCCESS) {
		log_error("Could not unregister configuration manager notification: %s (%d)",
		          get_configret_name(cr), cr);
	}

	event_remove_source(_notification_pipe.base.read_handle, EVENT_SOURCE_TYPE_GENERIC);
	pipe_destroy(&_notification_pipe);
}

bool usb_has_hotplug(void) {
	return true;
}
