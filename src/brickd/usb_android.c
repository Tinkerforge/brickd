/*
 * brickd
 * Copyright (C) 2018-2021 Matthias Bolte <matthias@tinkerforge.com>
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
#include <libusb.h>

#include <daemonlib/event.h>
#include <daemonlib/log.h>
#include <daemonlib/utils.h>

#include "usb.h"

static LogSource _log_source = LOG_SOURCE_INITIALIZER;

static void usb_handle_events_internal(void *opaque) {
	libusb_context *context = opaque;
	struct timeval tv;
	int rc;

	tv.tv_sec = 0;
	tv.tv_usec = 0;

	rc = libusb_handle_events_timeout(context, &tv);

	if (rc < 0) {
		log_error("Could not handle USB events: %s (%d)", usb_get_error_name(rc), rc);
	}
}

static void LIBUSB_CALL usb_add_pollfd(int fd, short events, void *opaque) {
	libusb_context *context = opaque;

	log_event_debug("Got told to add libusb pollfd (handle: %d, events: %d)", fd, events);

	// FIXME: handle error?
	event_add_source(fd, EVENT_SOURCE_TYPE_USB, "usb-poll", events,
	                 usb_handle_events_internal, context);
}

static void LIBUSB_CALL usb_remove_pollfd(int fd, void *opaque) {
	(void)opaque;

	log_event_debug("Got told to remove libusb pollfd (handle: %d)", fd);

	event_remove_source(fd, EVENT_SOURCE_TYPE_USB);
}

int usb_init_platform(libusb_context *context) {
	int phase = 0;
	const struct libusb_pollfd **pollfds = NULL;
	const struct libusb_pollfd **pollfd;
	const struct libusb_pollfd **last_added_pollfd = NULL;

	// get pollfds from libusb context
	pollfds = libusb_get_pollfds(context);

	if (pollfds == NULL) {
		log_error("Could not get pollfds from libusb context");

		goto cleanup;
	}

	for (pollfd = pollfds; *pollfd != NULL; ++pollfd) {
		if (event_add_source((*pollfd)->fd, EVENT_SOURCE_TYPE_USB, "usb-poll",
		                     (*pollfd)->events, usb_handle_events_internal, context) < 0) {
			goto cleanup;
		}

		last_added_pollfd = pollfd;
		phase = 1;
	}

	// register pollfd notifiers
	libusb_set_pollfd_notifiers(context, usb_add_pollfd, usb_remove_pollfd, context);

	phase = 2;

cleanup:
	switch (phase) { // no breaks, all cases fall through intentionally
	case 1:
		for (pollfd = pollfds; pollfd != last_added_pollfd; ++pollfd) {
			event_remove_source((*pollfd)->fd, EVENT_SOURCE_TYPE_USB);
		}

		if (last_added_pollfd != NULL) {
			event_remove_source((*last_added_pollfd)->fd, EVENT_SOURCE_TYPE_USB);
		}

		// fall through

	default:
		break;
	}

	libusb_free_pollfds(pollfds);

	return phase == 2 ? 0 : -1;
}

void usb_exit_platform(libusb_context *context) {
	const struct libusb_pollfd **pollfds = NULL;
	const struct libusb_pollfd **pollfd;

	libusb_set_pollfd_notifiers(context, NULL, NULL, NULL);

	pollfds = libusb_get_pollfds(context);

	if (pollfds == NULL) {
		log_error("Could not get pollfds from libusb context");
	} else {
		for (pollfd = pollfds; *pollfd != NULL; ++pollfd) {
			event_remove_source((*pollfd)->fd, EVENT_SOURCE_TYPE_USB);
		}

		libusb_free_pollfds(pollfds);
	}
}

void usb_handle_events_platform(libusb_context *context) {
	usb_handle_events_internal(context);
}

JNIEXPORT void JNICALL
Java_com_tinkerforge_brickd_MainService_hotplug(JNIEnv *env, jobject this) {
	(void)env;
	(void)this;

	usb_handle_hotplug();
}
