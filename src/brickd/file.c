/*
 * brickd
 * Copyright (C) 2014 Matthias Bolte <matthias@tinkerforge.com>
 *
 * file.c: File based I/O device
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

#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "file.h"

// sets errno on error
int file_create(File *file, const char *name, int flags) {
	int rc;
	int fcntl_flags;
	int saved_errno;

	rc  = io_create(&file->base, "file",
	                (IODestroyFunction)file_destroy,
	                (IOReadFunction)file_read,
	                (IOWriteFunction)file_write);

	if (rc < 0) {
		return rc;
	}

	file->base.handle = open(name, flags);

	if (file->base.handle < 0) {
		return -1;
	}

	// enable non-blocking operation
	fcntl_flags = fcntl(file->base.handle, F_GETFL, 0);

	if (fcntl_flags < 0 ||
	    fcntl(file->base.handle, F_SETFL, fcntl_flags | O_NONBLOCK) < 0) {
		saved_errno = errno;

		close(file->base.handle);

		errno = saved_errno;

		return -1;
	}

	return 0;
}

void file_destroy(File *file) {
	close(file->base.handle);
}

// sets errno on error
int file_read(File *file, void *buffer, int length) {
	return read(file->base.handle, buffer, length);
}

// sets errno on error
int file_write(File *file, void *buffer, int length) {
	return write(file->base.handle, buffer, length);
}
