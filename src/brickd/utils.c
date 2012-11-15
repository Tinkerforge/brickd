/*
 * brickd
 * Copyright (C) 2012 Matthias Bolte <matthias@tinkerforge.com>
 *
 * utils.c: Utility functions
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
#include <libusb.h>
#include <stdlib.h>
#include <string.h>

#include "utils.h"

#include "log.h"

#define LOG_CATEGORY LOG_CATEGORY_OTHER

int errno_would_block(void) {
#ifdef _WIN32
	return errno == ERRNO_WINSOCK2_OFFSET + WSAEWOULDBLOCK ? 1 : 0;
#else
	return errno == EAGAIN || errno == EWOULDBLOCK ? 1 : 0;
#endif
}

int errno_interrupted(void) {
#ifdef _WIN32
	return errno == ERRNO_WINSOCK2_OFFSET + WSAEINTR ? 1 : 0;
#else
	return errno == EINTR ? 1 : 0;
#endif
}

const char *get_errno_name(int error_code) {
	#define ERRNO_NAME(code) case code: return #code
	#define WINAPI_ERROR_NAME(code) case ERRNO_WINAPI_OFFSET + code: return #code
	#define WINSOCK2_ERROR_NAME(code) case ERRNO_WINSOCK2_OFFSET + code: return #code

	switch (error_code) {
	ERRNO_NAME(EPERM);
	ERRNO_NAME(ENOENT);
	ERRNO_NAME(ESRCH);
	ERRNO_NAME(EINTR);
	ERRNO_NAME(EIO);
	ERRNO_NAME(ENXIO);
	ERRNO_NAME(E2BIG);
	ERRNO_NAME(ENOEXEC);
	ERRNO_NAME(EBADF);
	ERRNO_NAME(ECHILD);
	ERRNO_NAME(EAGAIN);
	ERRNO_NAME(ENOMEM);
	ERRNO_NAME(EACCES);
	ERRNO_NAME(EFAULT);
#ifdef ENOTBLK
	ERRNO_NAME(ENOTBLK);
#endif
	ERRNO_NAME(EBUSY);
	ERRNO_NAME(EEXIST);
	ERRNO_NAME(EXDEV);
	ERRNO_NAME(ENODEV);
	ERRNO_NAME(ENOTDIR);
	ERRNO_NAME(EISDIR);
	ERRNO_NAME(ENFILE);
	ERRNO_NAME(EMFILE);
	ERRNO_NAME(ENOTTY);
#ifdef ETXTBSY
	ERRNO_NAME(ETXTBSY);
#endif
	ERRNO_NAME(EFBIG);
	ERRNO_NAME(ENOSPC);
	ERRNO_NAME(ESPIPE);
	ERRNO_NAME(EROFS);
	ERRNO_NAME(EMLINK);
	ERRNO_NAME(EPIPE);
	ERRNO_NAME(EDOM);
	ERRNO_NAME(ERANGE);
	ERRNO_NAME(EDEADLK);
	ERRNO_NAME(ENAMETOOLONG);
	ERRNO_NAME(ENOLCK);
	ERRNO_NAME(ENOSYS);
	ERRNO_NAME(ENOTEMPTY);

#ifndef _WIN32
	ERRNO_NAME(ELOOP);
	#if EWOULDBLOCK != EAGAIN
	ERRNO_NAME(EWOULDBLOCK);
	#endif
	ERRNO_NAME(ENOMSG);
	ERRNO_NAME(EIDRM);
	ERRNO_NAME(ENOSTR);
	ERRNO_NAME(ENODATA);
	ERRNO_NAME(ETIME);
	ERRNO_NAME(ENOSR);
	ERRNO_NAME(EREMOTE);
	ERRNO_NAME(ENOLINK);
	ERRNO_NAME(EPROTO);
	ERRNO_NAME(EMULTIHOP);
	ERRNO_NAME(EBADMSG);
	ERRNO_NAME(EOVERFLOW);
	ERRNO_NAME(EUSERS);
	ERRNO_NAME(ENOTSOCK);
	ERRNO_NAME(EDESTADDRREQ);
	ERRNO_NAME(EMSGSIZE);
	ERRNO_NAME(EPROTOTYPE);
	ERRNO_NAME(ENOPROTOOPT);
	ERRNO_NAME(EPROTONOSUPPORT);
	ERRNO_NAME(ESOCKTNOSUPPORT);
	ERRNO_NAME(EOPNOTSUPP);
	ERRNO_NAME(EPFNOSUPPORT);
	ERRNO_NAME(EAFNOSUPPORT);
	ERRNO_NAME(EADDRINUSE);
	ERRNO_NAME(EADDRNOTAVAIL);
	ERRNO_NAME(ENETDOWN);
	ERRNO_NAME(ENETUNREACH);
	ERRNO_NAME(ENETRESET);
	ERRNO_NAME(ECONNABORTED);
	ERRNO_NAME(ECONNRESET);
	ERRNO_NAME(ENOBUFS);
	ERRNO_NAME(EISCONN);
	ERRNO_NAME(ENOTCONN);
	ERRNO_NAME(ESHUTDOWN);
	ERRNO_NAME(ETOOMANYREFS);
	ERRNO_NAME(ETIMEDOUT);
	ERRNO_NAME(ECONNREFUSED);
	ERRNO_NAME(EHOSTDOWN);
	ERRNO_NAME(EHOSTUNREACH);
	ERRNO_NAME(EALREADY);
	ERRNO_NAME(EINPROGRESS);
	ERRNO_NAME(ESTALE);
	ERRNO_NAME(EDQUOT);
	ERRNO_NAME(ECANCELED);
	ERRNO_NAME(EOWNERDEAD);
	ERRNO_NAME(ENOTRECOVERABLE);
#endif

#if !defined _WIN32 && !defined __APPLE__
	ERRNO_NAME(ECHRNG);
	ERRNO_NAME(EL2NSYNC);
	ERRNO_NAME(EL3HLT);
	ERRNO_NAME(EL3RST);
	ERRNO_NAME(ELNRNG);
	ERRNO_NAME(EUNATCH);
	ERRNO_NAME(ENOCSI);
	ERRNO_NAME(EL2HLT);
	ERRNO_NAME(EBADE);
	ERRNO_NAME(EBADR);
	ERRNO_NAME(EXFULL);
	ERRNO_NAME(ENOANO);
	ERRNO_NAME(EBADRQC);
	ERRNO_NAME(EBADSLT);
	#if EDEADLOCK != EDEADLK
	ERRNO_NAME(EDEADLOCK);
	#endif
	ERRNO_NAME(EBFONT);
	ERRNO_NAME(ENONET);
	ERRNO_NAME(ENOPKG);
	ERRNO_NAME(EADV);
	ERRNO_NAME(ESRMNT);
	ERRNO_NAME(ECOMM);
	ERRNO_NAME(EDOTDOT);
	ERRNO_NAME(ENOTUNIQ);
	ERRNO_NAME(EBADFD);
	ERRNO_NAME(EREMCHG);
	ERRNO_NAME(ELIBACC);
	ERRNO_NAME(ELIBBAD);
	ERRNO_NAME(ELIBSCN);
	ERRNO_NAME(ELIBMAX);
	ERRNO_NAME(ELIBEXEC);
	ERRNO_NAME(EILSEQ);
	ERRNO_NAME(ERESTART);
	ERRNO_NAME(ESTRPIPE);
	ERRNO_NAME(EUCLEAN);
	ERRNO_NAME(ENOTNAM);
	ERRNO_NAME(ENAVAIL);
	ERRNO_NAME(EISNAM);
	ERRNO_NAME(EREMOTEIO);
	ERRNO_NAME(ENOMEDIUM);
	ERRNO_NAME(EMEDIUMTYPE);
	ERRNO_NAME(ENOKEY);
	ERRNO_NAME(EKEYEXPIRED);
	ERRNO_NAME(EKEYREVOKED);
	ERRNO_NAME(EKEYREJECTED);
	ERRNO_NAME(ERFKILL);
#endif

#ifdef _WIN32
	WINAPI_ERROR_NAME(ERROR_FAILED_SERVICE_CONTROLLER_CONNECT);
	WINAPI_ERROR_NAME(ERROR_INVALID_DATA);
	WINAPI_ERROR_NAME(ERROR_ACCESS_DENIED);
	WINAPI_ERROR_NAME(ERROR_INVALID_HANDLE);
	WINAPI_ERROR_NAME(ERROR_INVALID_NAME);
	WINAPI_ERROR_NAME(ERROR_CIRCULAR_DEPENDENCY);
	WINAPI_ERROR_NAME(ERROR_INVALID_PARAMETER);
	WINAPI_ERROR_NAME(ERROR_INVALID_SERVICE_ACCOUNT);
	WINAPI_ERROR_NAME(ERROR_DUPLICATE_SERVICE_NAME);
	WINAPI_ERROR_NAME(ERROR_SERVICE_ALREADY_RUNNING);
	WINAPI_ERROR_NAME(ERROR_SERVICE_DOES_NOT_EXIST);
	WINAPI_ERROR_NAME(ERROR_SERVICE_EXISTS);
	WINAPI_ERROR_NAME(ERROR_SERVICE_MARKED_FOR_DELETE);

	WINSOCK2_ERROR_NAME(WSAEINTR);
	WINSOCK2_ERROR_NAME(WSAEBADF);
	WINSOCK2_ERROR_NAME(WSAEACCES);
	WINSOCK2_ERROR_NAME(WSAEFAULT);
	WINSOCK2_ERROR_NAME(WSAEINVAL);
	WINSOCK2_ERROR_NAME(WSAEMFILE);
	WINSOCK2_ERROR_NAME(WSAEWOULDBLOCK);
	WINSOCK2_ERROR_NAME(WSAEINPROGRESS);
	WINSOCK2_ERROR_NAME(WSAEALREADY);
	WINSOCK2_ERROR_NAME(WSAENOTSOCK);
	WINSOCK2_ERROR_NAME(WSAEDESTADDRREQ);
	WINSOCK2_ERROR_NAME(WSAEMSGSIZE);
	WINSOCK2_ERROR_NAME(WSAEPROTOTYPE);
	WINSOCK2_ERROR_NAME(WSAENOPROTOOPT);
	WINSOCK2_ERROR_NAME(WSAEPROTONOSUPPORT);
	WINSOCK2_ERROR_NAME(WSAESOCKTNOSUPPORT);
	WINSOCK2_ERROR_NAME(WSAEOPNOTSUPP);
	WINSOCK2_ERROR_NAME(WSAEPFNOSUPPORT);
	WINSOCK2_ERROR_NAME(WSAEAFNOSUPPORT);
	WINSOCK2_ERROR_NAME(WSAEADDRINUSE);
	WINSOCK2_ERROR_NAME(WSAEADDRNOTAVAIL);
	WINSOCK2_ERROR_NAME(WSAENETDOWN);
	WINSOCK2_ERROR_NAME(WSAENETUNREACH);
	WINSOCK2_ERROR_NAME(WSAENETRESET);
	WINSOCK2_ERROR_NAME(WSAECONNABORTED);
	WINSOCK2_ERROR_NAME(WSAECONNRESET);
	WINSOCK2_ERROR_NAME(WSAENOBUFS);
	WINSOCK2_ERROR_NAME(WSAEISCONN);
	WINSOCK2_ERROR_NAME(WSAENOTCONN);
	WINSOCK2_ERROR_NAME(WSAESHUTDOWN);
	WINSOCK2_ERROR_NAME(WSAETOOMANYREFS);
	WINSOCK2_ERROR_NAME(WSAETIMEDOUT);
	WINSOCK2_ERROR_NAME(WSAECONNREFUSED);
	WINSOCK2_ERROR_NAME(WSAELOOP);
	WINSOCK2_ERROR_NAME(WSAENAMETOOLONG);
	WINSOCK2_ERROR_NAME(WSAEHOSTDOWN);
	WINSOCK2_ERROR_NAME(WSAEHOSTUNREACH);
	WINSOCK2_ERROR_NAME(WSAENOTEMPTY);
	WINSOCK2_ERROR_NAME(WSAEPROCLIM);
	WINSOCK2_ERROR_NAME(WSAEUSERS);
	WINSOCK2_ERROR_NAME(WSAEDQUOT);
	WINSOCK2_ERROR_NAME(WSAESTALE);
	WINSOCK2_ERROR_NAME(WSAEREMOTE);
#endif

	// FIXME

	default: return "<unknown>";
	}
}

const char *get_libusb_error_name(int error_code) {
	#define LIBUSB_ERROR_NAME(code) case code: return #code

	switch (error_code) {
	LIBUSB_ERROR_NAME(LIBUSB_SUCCESS);
	LIBUSB_ERROR_NAME(LIBUSB_ERROR_IO);
	LIBUSB_ERROR_NAME(LIBUSB_ERROR_INVALID_PARAM);
	LIBUSB_ERROR_NAME(LIBUSB_ERROR_ACCESS);
	LIBUSB_ERROR_NAME(LIBUSB_ERROR_NO_DEVICE);
	LIBUSB_ERROR_NAME(LIBUSB_ERROR_NOT_FOUND);
	LIBUSB_ERROR_NAME(LIBUSB_ERROR_BUSY);
	LIBUSB_ERROR_NAME(LIBUSB_ERROR_TIMEOUT);
	LIBUSB_ERROR_NAME(LIBUSB_ERROR_OVERFLOW);
	LIBUSB_ERROR_NAME(LIBUSB_ERROR_PIPE);
	LIBUSB_ERROR_NAME(LIBUSB_ERROR_INTERRUPTED);
	LIBUSB_ERROR_NAME(LIBUSB_ERROR_NO_MEM);
	LIBUSB_ERROR_NAME(LIBUSB_ERROR_NOT_SUPPORTED);
	LIBUSB_ERROR_NAME(LIBUSB_ERROR_OTHER);

	default: return "<unknown>";
	}
}

const char *get_libusb_transfer_status_name(int transfer_status) {
	#define LIBUSB_TRANSFER_STATUS_NAME(code) case code: return #code

	switch (transfer_status) {
	LIBUSB_TRANSFER_STATUS_NAME(LIBUSB_TRANSFER_COMPLETED);
	LIBUSB_TRANSFER_STATUS_NAME(LIBUSB_TRANSFER_ERROR);
	LIBUSB_TRANSFER_STATUS_NAME(LIBUSB_TRANSFER_TIMED_OUT);
	LIBUSB_TRANSFER_STATUS_NAME(LIBUSB_TRANSFER_CANCELLED);
	LIBUSB_TRANSFER_STATUS_NAME(LIBUSB_TRANSFER_STALL);
	LIBUSB_TRANSFER_STATUS_NAME(LIBUSB_TRANSFER_NO_DEVICE);
	LIBUSB_TRANSFER_STATUS_NAME(LIBUSB_TRANSFER_OVERFLOW);

	default: return "<unknown>";
	}
}

// sets errno on error
int array_create(Array *array, int reserved, int size) {
	if (reserved < 1) {
		reserved = 1;
	}

	array->allocated = 0;
	array->count = 0;
	array->size = size;
	array->bytes = calloc(reserved, size);

	if (array->bytes == NULL) {
		errno = ENOMEM;

		return -1;
	}

	array->allocated = reserved;

	return 0;
}

void array_destroy(Array *array, FreeFunction function) {
	int i;

	if (function != NULL) {
		for (i = 0; i < array->count; ++i) {
			function(array_get(array, i));
		}
	}

	free(array->bytes);
}

// sets errno on error
int array_reserve(Array *array, int count) {
	uint8_t *bytes;

	if (array->allocated >= count) {
		return 0;
	}

	// FIXME: use better growth pattern
	bytes = realloc(array->bytes, count * array->size);

	if (bytes == NULL) {
		errno = ENOMEM;
		return -1;
	}

	array->allocated = count;
	array->bytes = bytes;

	return 0;
}

int array_resize(Array *array, int count, FreeFunction function) {
	int rc;
	int i;

	if (array->count < count) {
		rc = array_reserve(array, count);

		if (rc < 0) {
			return rc;
		}
	} else if (array->count > count) {
		if (function != NULL) {
			for (i = count; i < array->count; ++i) {
				function(array_get(array, i));
			}
		}
	}

	array->count = count;

	return 0;
}

// sets errno on error
void *array_append(Array *array) {
	if (array_reserve(array, array->count + 1) < 0) {
		return NULL;
	}

	return array_get(array, array->count++);
}

void array_remove(Array *array, int i, FreeFunction function) {
	int tail;

	if (function != NULL) {
		function(array_get(array, i));
	}

	tail = (array->count - i - 1) * array->size;

	if (tail > 0) {
		memmove(array_get(array, i), array_get(array, i + 1), tail);
	}

	--array->count;
}

void *array_get(Array *array, int i) {
	if (i >= array->count) {
		return NULL;
	}

	return array->bytes + array->size * i;
}

int array_find(Array *array, void *item) {
	uint8_t *bytes = item;

	if (bytes < array->bytes ||
	    bytes > array->bytes + (array->count - 1) * array->size) {
		return -1;
	}

	if ((bytes - array->bytes) % array->size != 0) {
		log_error("Misaligned array access");

		return -1;
	}

	return (bytes - array->bytes) / array->size;
}

#define MAX_BASE58_STR_SIZE 8

static const char BASE58_STR[] = "123456789abcdefghijkmnopqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ";

void base58_encode(char *str, uint32_t value) {
	uint32_t mod;
	char reverse_str[MAX_BASE58_STR_SIZE] = {0};
	int i = 0;
	int j = 0;

	while (value >= 58) {
		mod = value % 58;
		reverse_str[i] = BASE58_STR[mod];
		value = value / 58;
		++i;
	}

	reverse_str[i] = BASE58_STR[value];
	i = 0;

	while (reverse_str[MAX_BASE58_STR_SIZE - 1 - i] == '\0') {
		++i;
	}

	for(j = 0; j < MAX_BASE58_STR_SIZE; ++j) {
		if (MAX_BASE58_STR_SIZE - i >= 0) {
			str[j] = reverse_str[MAX_BASE58_STR_SIZE - 1 - i];
		} else {
			str[j] = '\0';
		}

		++i;
	}
}
