/*
 * brickd
 * Copyright (C) 2018 Matthias Bolte <matthias@tinkerforge.com>
 *
 * main_android.cpp: Brick Daemon starting point for Android
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

extern "C" {

#include <daemonlib/config.h>
#include <daemonlib/event.h>
#include <daemonlib/pipe.h>
#include <daemonlib/utils.h>

#include "hardware.h"
#include "network.h"
#include "usb.h"
#include "mesh.h"
#include "version.h"
}

static LogSource _log_source = LOG_SOURCE_INITIALIZER;

static void handle_event_cleanup(void) {
	network_cleanup_clients_and_zombies();
	mesh_cleanup_stacks();
}

extern "C" JNIEXPORT void JNICALL
Java_com_tinkerforge_brickd_MainActivity_init(JNIEnv *env, jobject /* this */) {
	int phase = 0;
	int rc;

	(void)env;

	config_init(NULL);

	phase = 1;

#if 0 // FIXME: config cannot have errors, because not config file is loaded
	if (config_has_error()) {
		debugf("Error(s) occurred while reading config file '%s'\n",
		       config_filename);

		goto cleanup;
	}
#endif

	log_init();

	phase = 2;

	log_info("Brick Daemon %s started", VERSION_STRING);

#if 0 // FIXME: config cannot have warnings, because not config file is loaded
	if (config_has_warning()) {
		log_warn("Warning(s) in config file '%s'", config_filename);
	}
#endif

	if (event_init() < 0) {
		goto cleanup;
	}

	phase = 3;

	if (hardware_init() < 0) {
		goto cleanup;
	}

	phase = 4;

	if (usb_init() < 0) {
		goto cleanup;
	}

	phase = 5;

	if (network_init() < 0) {
		goto cleanup;
	}

	phase = 6;

	if (mesh_init() < 0) {
		goto cleanup;
	}

	phase = 7;

	if (event_run(handle_event_cleanup) < 0) {
		goto cleanup;
	}

cleanup:
	switch (phase) { // no breaks, all cases fall through intentionally
	case 7:
		mesh_exit();
		// fall through

	case 6:
		network_exit();
		// fall through

	case 5:
		usb_exit();
		// fall through

	case 4:
		hardware_exit();
		// fall through

	case 3:
		event_exit();
		// fall through

	case 2:
		log_info("Brick Daemon %s stopped", VERSION_STRING);
		log_exit();
		// fall through

	case 1:
		config_exit();
		// fall through

	default:
		break;
	}
}
