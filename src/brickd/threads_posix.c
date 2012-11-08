/*
 * brickd
 * Copyright (C) 2012 Matthias Bolte <matthias@tinkerforge.com>
 *
 * threads_posix.c: PThread based thread and locking implementation
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

#include "threads_posix.h"

#include "log.h"

#define LOG_CATEGORY LOG_CATEGORY_OTHER

#define MACOSX_SEMAPHORE_NAME

void mutex_create(Mutex *mutex) {
	pthread_mutex_init(&mutex->handle, NULL);

	// FIXME: error handling
}

void mutex_destroy(Mutex *mutex) {
	pthread_mutex_destroy(&mutex->handle);

	// FIXME: error handling
}

void mutex_lock(Mutex *mutex) {
	pthread_mutex_lock(&mutex->handle);

	// FIXME: error handling
}

void mutex_unlock(Mutex *mutex) {
	pthread_mutex_unlock(&mutex->handle);

	// FIXME: error handling
}

void semaphore_create(Semaphore *semaphore) {
#ifdef __APPLE__
	// Mac OS X does not support unnamed semaphores, so we fake them. Unlink
	// first to ensure that there is no existing semaphore with that name.
	// Then open the semaphore to create a new one. Finally unlink it again to
	// avoid leaking the name. The semaphore will work fine without a name.
	char name[100];

	snprintf(name, sizeof(name), "tinkerforge-brickd-internal-%p", semaphore);

	sem_unlink(name);
	semaphore->pointer = sem_open(name, O_CREAT | O_EXCL, S_IRWXU, 0);
	sem_unlink(name);
#else
	semaphore->pointer = &semaphore->object;

	sem_init(semaphore->pointer, 0, 0);
#endif

	// FIXME: error handling
}

void semaphore_destroy(Semaphore *semaphore) {
#ifdef __APPLE__
	sem_close(semaphore->pointer);
#endif

	// FIXME: error handling
}

void semaphore_acquire(Semaphore *semaphore) {
	sem_wait(semaphore->pointer);

	// FIXME: error handling
}

void semaphore_release(Semaphore *semaphore) {
	sem_post(semaphore->pointer);

	// FIXME: error handling
}

static void *thread_wrapper(void *opaque) {
	Thread *thread = opaque;

	thread->function(thread->opaque);

	return NULL;
}

void thread_start(Thread *thread, ThreadFunction function, void *opaque)
{
	thread->function = function;
	thread->opaque = opaque;

	pthread_create(&thread->handle, NULL, thread_wrapper, thread);

	// FIXME: error handling
}

void thread_join(Thread *thread) {
	if (pthread_equal(thread->handle, pthread_self())) {
		log_error("Thread at address %p is joining itself", thread);
	}

	pthread_join(thread->handle, NULL);

	// FIXME: error handling
}
