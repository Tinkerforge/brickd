/*
 * brickd
 * Copyright (C) 2012-2013 Matthias Bolte <matthias@tinkerforge.com>
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

/*
 * pipes are used to inject events into the poll based event loop. this
 * implementation is a direct wrapper of the POSIX pipe function.
 */

#include <unistd.h>

#include "pipe.h"

#include "utils.h"

// sets errno on error
int pipe_create(Pipe *pipe_) {
	int rc;
	EventHandle handles[2];

	rc = pipe(handles);

	pipe_->read_end = handles[0];
	pipe_->write_end = handles[1];

	return rc;
}

void pipe_destroy(Pipe *pipe) {
	close(pipe->read_end);
	close(pipe->write_end);
}

// sets errno on error
int pipe_read(Pipe *pipe, void *buffer, int length) {
	int rc;

	// FIXME: handle partial read
	do {
		rc = read(pipe->read_end, buffer, length);
	} while (rc < 0 && errno_interrupted());

	return rc;
}

// sets errno on error
int pipe_write(Pipe *pipe, void *buffer, int length) {
	int rc;

	// FIXME: handle partial write
	do {
		rc = write(pipe->write_end, buffer, length);
	} while (rc < 0 && errno_interrupted());

	return rc;
}
