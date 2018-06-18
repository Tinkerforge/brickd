/*
 * brickd
 * Copyright (C) 2018 Matthias Bolte <matthias@tinkerforge.com>
 *
 * usb_android.c: Android USB hotplug implementation
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

#include <jni.h>
#include <errno.h>

extern "C" {

#include <daemonlib/event.h>
#include <daemonlib/log.h>
#include <daemonlib/pipe.h>
#include <daemonlib/utils.h>

#include "usb.h"

}

static LogSource _log_source = LOG_SOURCE_INITIALIZER;

static Pipe _notification_pipe;

static void usb_forward_notifications(void *opaque) {
	uint8_t byte;

	(void)opaque;

	if (pipe_read(&_notification_pipe, &byte, sizeof(byte)) < 0) {
		log_error("Could not read from notification pipe: %s (%d)",
		          get_errno_name(errno), errno);

		return;
	}

	log_debug("Starting USB device scan, triggered by notification");

	usb_rescan();
}

extern "C" int usb_init_platform(void) {
	return 0;
}

extern "C" void usb_exit_platform(void) {
}

extern "C" int usb_init_hotplug(libusb_context *context) {
	(void)context;

	// create notification pipe
	if (pipe_create(&_notification_pipe, 0) < 0) {
		log_error("Could not create hotplug pipe: %s (%d)",
		          get_errno_name(errno), errno);

		return -1;
	}

	if (event_add_source(_notification_pipe.base.read_handle, EVENT_SOURCE_TYPE_GENERIC,
	                     "hotplug", EVENT_READ, usb_forward_notifications, NULL) < 0) {
		pipe_destroy(&_notification_pipe);
		
		return -1;
	}

	return 0;
}

extern "C" void usb_exit_hotplug(libusb_context *context) {
	(void)context;

	event_remove_source(_notification_pipe.base.read_handle, EVENT_SOURCE_TYPE_GENERIC);
	pipe_destroy(&_notification_pipe);
}

extern "C" bool usb_has_hotplug(void) {
	return true;
}

extern "C" JNIEXPORT void JNICALL
Java_com_tinkerforge_brickd_MainService_hotplug(JNIEnv *env, jobject /* this */) {
	uint8_t byte = 0;

	if (pipe_write(&_notification_pipe, &byte, sizeof(byte)) < 0) {
		log_error("Could not write to notification pipe: %s (%d)",
		          get_errno_name(errno), errno);
	}
}
