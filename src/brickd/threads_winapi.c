/*
 * brickd
 * Copyright (C) 2012 Matthias Bolte <matthias@tinkerforge.com>
 *
 * threads_winapi.c: WinAPI based thread and locking implementation
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
#include <stdint.h>

#include "threads_winapi.h"

#include "log.h"
#include "utils.h"

#define LOG_CATEGORY LOG_CATEGORY_OTHER

void mutex_create(Mutex *mutex) {
	InitializeCriticalSection(&mutex->handle);
}

void mutex_destroy(Mutex *mutex) {
	DeleteCriticalSection(&mutex->handle);
}

void mutex_lock(Mutex *mutex) {
	EnterCriticalSection(&mutex->handle);
}

void mutex_unlock(Mutex *mutex) {
	LeaveCriticalSection(&mutex->handle);
}

// sets errno on error
int semaphore_create(Semaphore *semaphore) {
	semaphore->handle = CreateSemaphore(NULL, 0, INT32_MAX, NULL);

	if (semaphore->handle == NULL) {
		errno = ERRNO_WINAPI_OFFSET + GetLastError();

		return -1;
	}

	return 0;
}

void semaphore_destroy(Semaphore *semaphore) {
	CloseHandle(semaphore->handle);

	// FIXME: error handling
}

void semaphore_acquire(Semaphore *semaphore) {
	WaitForSingleObject(semaphore->handle, INFINITE);

	// FIXME: error handling
}

void semaphore_release(Semaphore *semaphore) {
	ReleaseSemaphore(semaphore->handle, 1, NULL);

	// FIXME: error handling
}

static DWORD WINAPI thread_wrapper(void *opaque) {
	Thread *thread = opaque;

	thread->function(thread->opaque);

	return 0;
}

void thread_create(Thread *thread, ThreadFunction function, void *opaque) {
	thread->function = function;
	thread->opaque = opaque;

	thread->handle = CreateThread(NULL, 0, thread_wrapper, thread, 0, &thread->id);

	// FIXME: error handling
}

void thread_destroy(Thread *thread) {
	CloseHandle(thread->handle);
}

void thread_join(Thread *thread) {
	if (thread->id == GetCurrentThreadId()) {
		log_error("Thread with ID %u is joining itself", (uint32_t)thread->id);
	}

	WaitForSingleObject(thread->handle, INFINITE);

	// FIXME: error handling
}
