/*
 * brickd
 * Copyright (C) 2012-2013 Matthias Bolte <matthias@tinkerforge.com>
 *
 * iokit.c: IOKit specific functions
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
 * IOKit is used to detect USB hot(un)plug in case the available libusb version
 * doesn't support this. the delivery of IOKit notifications is done by a run
 * loop that is driven by CFRunLoopRun. this function blocks and requires to
 * use an extra thread to do this blocking call. IOKit uses callbacks to send
 * notifications. those callbacks are called from the extra thread calling the
 * CFRunLoopRun function. a pipe is used to integrate this with the main event
 * loop. on incoming USB matched and terminated notifications usb_update is
 * called. it scans the bus for added or removed devices.
 *
 * by default the Makefile checks the libusb version and disables IOKit usage
 * completely if libusb 1.0.16 (first version to support hotplug on Mac OSX)
 * or newer is available. in that case BRICKD_WITH_IOKIT is not defined and
 * iokit.c is not included into the build. this decision can be overridden by
 * running make with WITH_IOKIT=yes to force inclusion of IOKit support or
 * WITH_IOKIT=no to force its exclusion. the build_pkg.py script runs make with
 * WITH_IOKIT=yes.
 *
 * anyway, even it IOKit support is enforced by WITH_IOKIT=yes, it'll only
 * be used if libusb doesn't support hotplug on its own (detected at runtime).
 */

#include <errno.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/usb/IOUSBLib.h>
#include <AvailabilityMacros.h>
#if MAC_OS_X_VERSION_MIN_REQUIRED >= 1060
	#include <objc/objc-auto.h>
#endif

#include "iokit.h"

#include "event.h"
#include "log.h"
#include "pipe.h"
#include "threads.h"
#include "usb.h"
#include "utils.h"

#define LOG_CATEGORY LOG_CATEGORY_HOTPLUG

static EventHandle _notification_pipe[2] = { INVALID_EVENT_HANDLE,
                                             INVALID_EVENT_HANDLE };
static Thread _poller_thread;
static Semaphore _started;
static Semaphore _stopped;
static int _running = 0;
static CFRunLoopRef _run_loop = NULL;

static const char *iokit_get_error_name(int error_code) {
	#define IOKIT_ERROR_NAME(code) case code: return #code

	switch (error_code) {
	IOKIT_ERROR_NAME(kIOReturnSuccess);
	IOKIT_ERROR_NAME(kIOReturnNotOpen);
	IOKIT_ERROR_NAME(kIOReturnNoDevice);
	IOKIT_ERROR_NAME(kIOUSBNoAsyncPortErr);
	IOKIT_ERROR_NAME(kIOReturnExclusiveAccess);
	IOKIT_ERROR_NAME(kIOUSBPipeStalled);
	IOKIT_ERROR_NAME(kIOReturnError);
	IOKIT_ERROR_NAME(kIOUSBTransactionTimeout);
	IOKIT_ERROR_NAME(kIOReturnBadArgument);
	IOKIT_ERROR_NAME(kIOReturnAborted);
	IOKIT_ERROR_NAME(kIOReturnNotResponding);
	IOKIT_ERROR_NAME(kIOReturnOverrun);
	IOKIT_ERROR_NAME(kIOReturnCannotWire);

	default: return "<unknown>";
	}
}

static void iokit_forward_notifications(void *opaque) {
	uint8_t byte;

	(void)opaque;

	if (pipe_read(_notification_pipe[0], &byte, sizeof(byte)) < 0) {
		log_error("Could not read from notification pipe: %s (%d)",
		          get_errno_name(errno), errno);

		return;
	}

	usb_update();
}

static void iokit_handle_notifications(void *opaque, io_iterator_t iterator) {
	const char *type = opaque;
	io_service_t object;
	int found = 0;
	uint8_t byte = 0;

	object = IOIteratorNext(iterator);

	while (object != IO_OBJECT_NULL) {
		found = 1;

		IOObjectRelease(object);

		object = IOIteratorNext(iterator);
	}

	if (found) {
		log_debug("Received IOKit notification (type: %s)", type);

		if (pipe_write(_notification_pipe[1], &byte, sizeof(byte)) < 0) {
			log_error("Could not write to notification pipe: %s (%d)",
			          get_errno_name(errno), errno);
		}
	}
}

static void iokit_poll_notifications(void *opaque) {
	int phase = 0;
	CFMutableDictionaryRef matching_dictionary;
	IONotificationPortRef notification_port;
	CFRunLoopSourceRef notification_run_loop_source;
	IOReturn rc;
	io_iterator_t matched_iterator;
	io_iterator_t terminated_iterator;

	(void)opaque;

	log_debug("Started notification poll thread");

	// need to register this PThread with the Objective-C garbage collector,
	// because CoreFoundation uses Objective-C
#if MAC_OS_X_VERSION_MIN_REQUIRED >= 1060
	objc_registerThreadWithCollector();
#endif

	// create USB matching dictionary
	matching_dictionary = IOServiceMatching(kIOUSBDeviceClassName);

	if (matching_dictionary == NULL) {
		log_error("Could not create USB matching dictionary");

		goto cleanup;
	}

	phase = 1;

	// create notification port
	notification_port = IONotificationPortCreate(kIOMasterPortDefault);

	if (notification_port == NULL) {
		log_error("Could not create notification port");

		goto cleanup;
	}

	phase = 2;

	// get notification run loop source
	notification_run_loop_source = IONotificationPortGetRunLoopSource(notification_port);

	if (notification_run_loop_source == NULL) {
		log_error("Could not get notification run loop source");

		goto cleanup;
	}

	CFRunLoopAddSource(CFRunLoopGetCurrent(), notification_run_loop_source,
	                   kCFRunLoopDefaultMode);

	phase = 3;

	// register notification callbacks
	matching_dictionary = (CFMutableDictionaryRef)CFRetain(matching_dictionary);
	rc = IOServiceAddMatchingNotification(notification_port,
	                                      kIOMatchedNotification,
	                                      matching_dictionary,
	                                      iokit_handle_notifications,
	                                      "matched",
	                                      &matched_iterator);

	if (rc != kIOReturnSuccess) {
		log_error("Could not add 'matched' notification source: %s (%d)",
		          iokit_get_error_name(rc), rc);

		goto cleanup;
	}

	iokit_handle_notifications("matched", matched_iterator);

	matching_dictionary = (CFMutableDictionaryRef)CFRetain(matching_dictionary);
	rc = IOServiceAddMatchingNotification(notification_port,
	                                      kIOTerminatedNotification,
	                                      matching_dictionary,
	                                      iokit_handle_notifications,
	                                      "terminated",
	                                      &terminated_iterator);

	if (rc != kIOReturnSuccess) {
		log_error("Could not add 'terminated' notification source: %s (%d)",
		          iokit_get_error_name(rc), rc);

		goto cleanup;
	}

	iokit_handle_notifications("terminated", terminated_iterator);

	phase = 4;

	// start loop
	_running = 1;
	_run_loop = CFRunLoopGetCurrent();

	semaphore_release(&_started);

	CFRunLoopRun();

	log_debug("Stopped notification poll thread");

	semaphore_release(&_stopped);

cleanup:
	switch (phase) { // no breaks, all cases fall through intentionally
	case 3:
		CFRunLoopRemoveSource(CFRunLoopGetCurrent(), notification_run_loop_source,
		                      kCFRunLoopDefaultMode);

	case 2:
		IONotificationPortDestroy(notification_port);

	case 1:
		CFRelease(matching_dictionary);

	default:
		break;
	}

	if (phase != 4) {
		_running = 0;

		semaphore_release(&_started);
	}
}

int iokit_init(void) {
	int phase = 0;

	log_debug("Initializing IOKit subsystem");

	// create notification pipe
	if (pipe_create(_notification_pipe) < 0) {
		log_error("Could not create notification pipe: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 1;

	if (event_add_source(_notification_pipe[0], EVENT_SOURCE_TYPE_GENERIC,
	                     EVENT_READ, iokit_forward_notifications, NULL) < 0) {
		goto cleanup;
	}

	phase = 2;

	// create notification poll thread
	if (semaphore_create(&_started) < 0) {
		log_error("Could not create started semaphore: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 3;

	if (semaphore_create(&_stopped) < 0) {
		log_error("Could not create stopped semaphore: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	thread_create(&_poller_thread, iokit_poll_notifications, NULL);

	phase = 4;

	semaphore_acquire(&_started);

	if (!_running) {
		log_error("Could not start notification poll thread");

		goto cleanup;
	}

	phase = 5;

cleanup:
	switch (phase) { // no breaks, all cases fall through intentionally
	case 4:
		thread_destroy(&_poller_thread);
		semaphore_destroy(&_stopped);

	case 3:
		semaphore_destroy(&_started);

	case 2:
		event_remove_source(_notification_pipe[0], EVENT_SOURCE_TYPE_GENERIC);

	case 1:
		pipe_destroy(_notification_pipe);

	default:
		break;
	}

	return phase == 5 ? 0 : -1;
}

void iokit_exit(void) {
	log_debug("Shutting down IOKit subsystem");

	if (_running) {
		_running = 0;

		CFRunLoopStop(_run_loop);

		semaphore_acquire(&_stopped);
	}

	thread_destroy(&_poller_thread);

	semaphore_destroy(&_started);
	semaphore_destroy(&_stopped);

	event_remove_source(_notification_pipe[0], EVENT_SOURCE_TYPE_GENERIC);

	pipe_destroy(_notification_pipe);
}
