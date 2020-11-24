/*
 * brickd
 * Copyright (C) 2020 Matthias Bolte <matthias@tinkerforge.com>
 *
 * vcgencmd.c: Raspberry Pi VideoCore GPU infomarion query interface
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

// https://github.com/raspberrypi/userland/tree/master/host_applications/linux/apps/gencmd 9f3f9054a692e53b60fca54221a402414e030335
// https://www.raspberrypi.org/documentation/configuration/config-txt/overclocking.md
// https://www.raspberrypi.org/documentation/raspbian/applications/vcgencmd.md

/*
 * Copyright (c) 2012-2014, Broadcom Europe Ltd
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the copyright holder nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <errno.h>
#include <string.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <daemonlib/log.h>
#include <daemonlib/utils.h>

#include "vcgencmd.h"

static LogSource _log_source = LOG_SOURCE_INITIALIZER;

typedef struct vchiq_config_struct {
	int max_msg_size;
	int bulk_threshold;
	int max_outstanding_bulks;
	int max_services;
	short version;
	short version_min;
} vchiq_config_t;

typedef struct {
	unsigned int config_size;
	vchiq_config_t *pconfig;
} vchiq_get_config_t;

typedef struct {
	const void *data;
	int size;
} vchiq_element_t;

typedef struct {
	unsigned int handle;
	unsigned int count;
	const vchiq_element_t *elements;
} vchiq_queue_message_t;

typedef enum {
	VCHIQ_ERROR = -1,
	VCHIQ_SUCCESS = 0,
	VCHIQ_RETRY = 1
} vchiq_status_t;

typedef enum {
	VCHIQ_SERVICE_OPENED,
	VCHIQ_SERVICE_CLOSED,
	VCHIQ_MESSAGE_AVAILABLE,
	VCHIQ_BULK_TRANSMIT_DONE,
	VCHIQ_BULK_RECEIVE_DONE,
	VCHIQ_BULK_TRANSMIT_ABORTED,
	VCHIQ_BULK_RECEIVE_ABORTED
} vchiq_reason_t;

typedef unsigned int vchiq_service_handle_t;

typedef struct vchiq_header_struct {
	int msgid;
	unsigned int size;
	char data[0];
} vchiq_header_t;

typedef vchiq_status_t (*vchiq_callback_t)(vchiq_reason_t, vchiq_header_t *,
                                           vchiq_service_handle_t, void *);

typedef struct vchiq_service_params_struct {
	int fourcc;
	vchiq_callback_t callback;
	void *userdata;
	short version;
	short version_min;
} vchiq_service_params_t;

typedef struct {
	vchiq_service_params_t params;
	int is_open;
	int is_vchi;
	unsigned int handle;
} vchiq_create_service_t;

typedef struct {
	unsigned int handle;
	int blocking;
	unsigned int bufsize;
	void *buf;
} vchiq_dequeue_message_t;

#define RETRY(r,x)                     do { r = x; } while ((r < 0) && (errno == EINTR))
#define MAKE_FOURCC(x)                 ((int32_t)((x[0] << 24)|(x[1] << 16)|(x[2] << 8)|x[3]))
#define GENCMDSERVICE_MSGFIFO_SIZE     (4096 - 4)
#define VC_GENCMD_VER                  1
#define VCHIQ_SERVICE_HANDLE_INVALID   0
#define VCHIQ_IOC_MAGIC                0xC4
#define VCHIQ_INVALID_HANDLE           (~0)
#define VCHIQ_IOC_CONNECT              _IO(VCHIQ_IOC_MAGIC,   0)
#define VCHIQ_IOC_CREATE_SERVICE       _IOWR(VCHIQ_IOC_MAGIC, 2, vchiq_create_service_t)
#define VCHIQ_IOC_REMOVE_SERVICE       _IO(VCHIQ_IOC_MAGIC,   3)
#define VCHIQ_IOC_QUEUE_MESSAGE        _IOW(VCHIQ_IOC_MAGIC,  4, vchiq_queue_message_t)
#define VCHIQ_IOC_DEQUEUE_MESSAGE      _IOWR(VCHIQ_IOC_MAGIC, 8, vchiq_dequeue_message_t)
#define VCHIQ_IOC_GET_CONFIG           _IOWR(VCHIQ_IOC_MAGIC, 10, vchiq_get_config_t)
#define VCHIQ_IOC_USE_SERVICE          _IO(VCHIQ_IOC_MAGIC,   12)
#define VCHIQ_IOC_RELEASE_SERVICE      _IO(VCHIQ_IOC_MAGIC,   13)
#define VCHIQ_VERSION_MIN              3
#define VCHIQ_VERSION                  8
#define VCHIQ_VERSION_LIB_VERSION      7
#define VCHIQ_VERSION_CLOSE_DELIVERED  7

int vcgencmd_get_config(const char *name, char *value, int value_length) {
	int fd;
	int rc;
	vchiq_get_config_t get_config;
	vchiq_config_t config;
	vchiq_service_params_t service_params;
	vchiq_create_service_t create_service;
	vchiq_service_handle_t service_handle;
	char command[128];
	vchiq_queue_message_t queue_message;
	vchiq_element_t element;
	vchiq_dequeue_message_t dequeue_message;
	char response[GENCMDSERVICE_MSGFIFO_SIZE];
	int name_length = strlen(name);
	int prefix_length = 4 + name_length + 1;

	// open
	fd = open("/dev/vchiq", O_RDWR);

	if (fd < 0) {
		log_error("Could not open /dev/vchiq for writing: %s (%d)", get_errno_name(errno), errno);

		return -1;
	}

	// check version
	memset(&get_config, 0, sizeof(get_config));
	memset(&config, 0, sizeof(config));

	get_config.config_size = sizeof(config);
	get_config.pconfig = &config;

	RETRY(rc, ioctl(fd, VCHIQ_IOC_GET_CONFIG, &get_config));

	if (rc != 0) {
		log_error("Could not read VCHIQ driver version: %s (%d)", get_errno_name(errno), errno);
		robust_close(fd);

		return -1;
	}

	if (config.version < VCHIQ_VERSION_MIN || config.version_min > VCHIQ_VERSION) {
		log_error("Unsupported VCHIQ driver (version %d, version_min %d)", config.version, config.version_min);
		robust_close(fd);

		return -1;
	}

	// connect
	RETRY(rc, ioctl(fd, VCHIQ_IOC_CONNECT, 0));

	if (rc != 0) {
		log_error("Could not connect to VCHIQ driver: %s (%d)", get_errno_name(errno), errno);
		robust_close(fd);

		return -1;
	}

	// create GCMD service
	memset(&service_params, 0, sizeof(service_params));

	service_params.fourcc = MAKE_FOURCC("GCMD");
	service_params.userdata = NULL;
	service_params.version = VC_GENCMD_VER;
	service_params.version_min = VC_GENCMD_VER;

	memset(&create_service, 0, sizeof(create_service));

	create_service.params = service_params;
	create_service.is_open = 1;
	create_service.is_vchi = 1;
	create_service.handle = VCHIQ_SERVICE_HANDLE_INVALID;

	RETRY(rc, ioctl(fd, VCHIQ_IOC_CREATE_SERVICE, &create_service));

	if (rc != 0) {
		log_error("Could not create VCHIQ GCMD service: %s (%d)", get_errno_name(errno), errno);
		robust_close(fd);

		return -1;
	}

	service_handle = create_service.handle;

	// queue command
	snprintf(command, sizeof(command), "get_config %s", name);

	memset(&queue_message, 0, sizeof(element));

	element.data = command;
	element.size = strlen(command) + 1;

	memset(&queue_message, 0, sizeof(queue_message));

	queue_message.handle = service_handle;
	queue_message.elements = &element;
	queue_message.count = 1;

	RETRY(rc, ioctl(fd, VCHIQ_IOC_QUEUE_MESSAGE, &queue_message));

	if (rc != 0) {
		log_error("Could not queue message to VCHIQ GCMD service: %s (%d)", get_errno_name(errno), errno);
		robust_close(fd);

		return -1;
	}

	while (1) { // FIXME: add timeout
		memset(&dequeue_message, 0, sizeof(dequeue_message));

		dequeue_message.handle = service_handle;
		dequeue_message.blocking = 0;
		dequeue_message.bufsize = sizeof(response);
		dequeue_message.buf = response;

		RETRY(rc, ioctl(fd, VCHIQ_IOC_DEQUEUE_MESSAGE, &dequeue_message));

		if (rc < 0) {
			if (errno_would_block()) {
				millisleep(1);
				continue;
			}

			log_error("Could not dequeue message from VCHIQ GCMD service: %s (%d)", get_errno_name(errno), errno);
			robust_close(fd);

			return -1;
		}

		if (rc == 0) {
			millisleep(1);
			continue;
		}

		// FIXME: ignoring the first 4 bytes of VCHIQ error code (little-endian)

		value_length = rc - prefix_length;

		if (value_length < 0) {
			log_error("Got too short message (length: %d) from VCHIQ GCMD service", rc);
			robust_close(fd);

			return -1;
		}

		if (strncmp(response + 4, name, name_length) != 0 || response[4 + name_length] != '=') {
			log_error("Got invalid message from VCHIQ GCMD service"); // FIXME: include invalid response
			robust_close(fd);

			return -1;
		}

		memcpy(value, response + prefix_length, value_length);

		break;
	}

	RETRY(rc, ioctl(fd, VCHIQ_IOC_RELEASE_SERVICE, service_handle));

	if (rc != 0) {
		log_error("Could not release VCHIQ GCMD service: %s (%d)", get_errno_name(errno), errno);
		robust_close(fd);

		return -1;
	}

	robust_close(fd);

	return value_length;
}
