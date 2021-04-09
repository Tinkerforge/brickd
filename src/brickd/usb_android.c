/*
 * brickd
 * Copyright (C) 2018-2019, 2021 Matthias Bolte <matthias@tinkerforge.com>
 *
 * usb_android.c: Android specific USB functions
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

#include <daemonlib/event.h>
#include <daemonlib/log.h>
#include <daemonlib/pipe.h>
#include <daemonlib/utils.h>

#include "usb.h"

static LogSource _log_source = LOG_SOURCE_INITIALIZER;

static Pipe _hotplug_pipe;

static void usb_forward_hotplug(void *opaque) {
	uint8_t byte;

	(void)opaque;

	if (pipe_read(&_hotplug_pipe, &byte, sizeof(byte)) < 0) {
		log_error("Could not read from hotplug pipe: %s (%d)",
		          get_errno_name(errno), errno);

		return;
	}

	log_debug("Starting USB device scan, triggered by hotplug");

	usb_rescan();
}

int usb_init_platform(void) {
	return 0;
}

void usb_exit_platform(void) {
}

int usb_init_hotplug(libusb_context *context) {
	(void)context;

	// create hotplug pipe
	if (pipe_create(&_hotplug_pipe, PIPE_FLAG_NON_BLOCKING_READ) < 0) {
		log_error("Could not create hotplug pipe: %s (%d)",
		          get_errno_name(errno), errno);

		return -1;
	}

	if (event_add_source(_hotplug_pipe.base.read_handle, EVENT_SOURCE_TYPE_GENERIC,
	                     "hotplug", EVENT_READ, usb_forward_hotplug, NULL) < 0) {
		pipe_destroy(&_hotplug_pipe);

		return -1;
	}

	return 0;
}

void usb_exit_hotplug(libusb_context *context) {
	(void)context;

	event_remove_source(_hotplug_pipe.base.read_handle, EVENT_SOURCE_TYPE_GENERIC);
	pipe_destroy(&_hotplug_pipe);
}

bool usb_has_hotplug(void) {
	return true;
}

JNIEXPORT void JNICALL
Java_com_tinkerforge_brickd_MainService_hotplug(JNIEnv *env, jobject this) {
	uint8_t byte = 0;

	(void)this;

	if (pipe_write(&_hotplug_pipe, &byte, sizeof(byte)) < 0) {
		log_error("Could not write to hotplug pipe: %s (%d)",
		          get_errno_name(errno), errno);
	}
}
