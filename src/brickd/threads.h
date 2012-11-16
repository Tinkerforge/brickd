/*
 * brickd
 * Copyright (C) 2012 Matthias Bolte <matthias@tinkerforge.com>
 *
 * threads.h: Thread and locking specific functions
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

#ifndef BRICKD_THREADS_H
#define BRICKD_THREADS_H

#ifdef _WIN32
	#include "threads_winapi.h"
#else
	#include "threads_posix.h"
#endif

void mutex_create(Mutex *mutex);
void mutex_destroy(Mutex *mutex);
void mutex_lock(Mutex *mutex);
void mutex_unlock(Mutex *mutex);

int semaphore_create(Semaphore *semaphore);
void semaphore_destroy(Semaphore *semaphore);
void semaphore_acquire(Semaphore *semaphore);
void semaphore_release(Semaphore *semaphore);

void thread_create(Thread *thread, ThreadFunction function, void *opaque);
void thread_destroy(Thread *thread);
void thread_join(Thread *thread);

#endif // BRICKD_THREADS_H
