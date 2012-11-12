/*
 * brickd
 * Copyright (C) 2012 Matthias Bolte <matthias@tinkerforge.com>
 *
 * pipe_posix.c: POSIX based pipe implementation
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

#include <unistd.h>

#include "pipe.h"

#include "utils.h"

// sets errno on error
int pipe_create(EventHandle handles[2]) {
	return pipe(handles);
}

void pipe_destroy(EventHandle handles[2]) {
	close(handles[0]);
	close(handles[1]);
}

// sets errno on error
int pipe_read(EventHandle handle, void *buffer, int length) {
	int rc;

	// FIXME: handle partial read
	do {
		rc = read(handle, buffer, length);
	} while (rc < 0 && errno_interrupted());

	return rc;
}

// sets errno on error
int pipe_write(EventHandle handle, void *buffer, int length) {
	int rc;

	// FIXME: handle partial write
	do {
		rc = write(handle, buffer, length);
	} while (rc < 0 && errno_interrupted());

	return rc;
}
