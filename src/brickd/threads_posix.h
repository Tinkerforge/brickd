/*
 * brickd
 * Copyright (C) 2012 Matthias Bolte <matthias@tinkerforge.com>
 *
 * threads_posix.h: PThread based thread and locking implementation
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

#ifndef BRICKD_THREADS_POSIX_H
#define BRICKD_THREADS_POSIX_H

#include <pthread.h>
#include <semaphore.h>

typedef void (*ThreadFunction)(void *opaque);

typedef struct {
	pthread_mutex_t handle;
} Mutex;

typedef struct {
	sem_t object;
	sem_t *pointer;
} Semaphore;

typedef struct {
	pthread_t handle;
	ThreadFunction function;
	void *opaque;
} Thread;

#endif // BRICKD_THREADS_POSIX_H
