/*
 * brickd
 * Copyright (C) 2018 Olaf Lüke <olaf@tinkerforge.com>
 *
 * bricklet_stack.c: SPI Tinkerforge Protocol (SPITFP) implementation for direct
 *                   communication between brickd and Bricklet with co-processor
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

#include "bricklet_stack.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <linux/spi/spidev.h>
#include <sys/eventfd.h>

#include <daemonlib/base58.h>
#include <daemonlib/config.h>
#include <daemonlib/event.h>
#include <daemonlib/io.h>
#include <daemonlib/log.h>
#include <daemonlib/packet.h>
#include <daemonlib/pipe.h>
#include <daemonlib/red_gpio.h>

#include "hardware.h"
#include "network.h"

#define BRICKLET_STACK_SPI_CONFIG_MODE           SPI_MODE_0
#define BRICKLET_STACK_SPI_CONFIG_LSB_FIRST      0
#define BRICKLET_STACK_SPI_CONFIG_BITS_PER_WORD  8
#define BRICKLET_STACK_SPI_CONFIG_MAX_SPEED_HZ   2000000

static LogSource _log_source = LOG_SOURCE_INITIALIZER;


// New packet from brickd event loop is queued to be written to BrickletStack via SPI
static int bricklet_stack_dispatch_to_spi(Stack *stack, Packet *request, Recipient *recipient) {
	BrickletStack *bricklet_stack = (BrickletStack*)stack;

    return 0;
}

// New packet from BrickletStack is send into brickd event loop
static void bricklet_stack_dispatch_from_spi(void *opaque) {
	BrickletStack *bricklet_stack = (BrickletStack*)opaque;
}

static void bricklet_stack_spi_thread(void *opaque) {
	BrickletStack *bricklet_stack = (BrickletStack*)opaque;
	bricklet_stack->spi_thread_running = true;
	while (bricklet_stack->spi_thread_running) {
		uint8_t tx[64] = {1, 2, 3, 4, 5, 6, 7, 8};
		uint8_t rx[64] = {0};

		struct spi_ioc_transfer spi_transfer = {
			.tx_buf = (unsigned long)&tx,
			.rx_buf = (unsigned long)&rx,
			.len = 64,
		};

		int rc = ioctl(bricklet_stack->spi_fd, SPI_IOC_MESSAGE(1), &spi_transfer);

		struct timespec t;
		t.tv_sec = 1;
		t.tv_nsec = 0;
		clock_nanosleep(CLOCK_MONOTONIC, 0, &t, NULL);
	}
}

static int bricklet_stack_init_spi(BrickletStack *bricklet_stack) {
	const uint8_t  mode          = BRICKLET_STACK_SPI_CONFIG_MODE;
	const uint8_t  lsb_first     = BRICKLET_STACK_SPI_CONFIG_LSB_FIRST;
	const uint8_t  bits_per_word = BRICKLET_STACK_SPI_CONFIG_BITS_PER_WORD;
	const uint32_t max_speed_hz  = BRICKLET_STACK_SPI_CONFIG_MAX_SPEED_HZ;

	// Open spidev
	bricklet_stack->spi_fd = open(bricklet_stack->config.spi_device, O_RDWR);
	if (bricklet_stack->spi_fd < 0) {
		log_error("Could not open %s", bricklet_stack->config.spi_device);
		return -1;
	}

	if (ioctl(bricklet_stack->spi_fd, SPI_IOC_WR_MODE, &mode) < 0) {
		log_error("Could not configure SPI mode");
		return -1;
	}

	if (ioctl(bricklet_stack->spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &max_speed_hz) < 0) {
		log_error("Could not configure SPI max speed");
		return -1;
	}

	if (ioctl(bricklet_stack->spi_fd, SPI_IOC_WR_BITS_PER_WORD, &bits_per_word) < 0) {
		log_error("Could not configure SPI bits per word");
		return -1;
	}

	if (ioctl(bricklet_stack->spi_fd, SPI_IOC_WR_LSB_FIRST, &lsb_first) < 0) {
		log_error("Could not configure SPI lsb first");
		return -1;
	}

	thread_create(&bricklet_stack->spi_thread, bricklet_stack_spi_thread, bricklet_stack);

	return 0;
}

BrickletStack* bricklet_stack_init(BrickletStackConfig *config) {
    int phase = 0;
	char bricklet_stack_name[129] = {'\0'};
	char notification_name[129] = {'\0'};

    log_debug("Initializing BrickletStack subsystem");

	// create bricklet_stack struct
	BrickletStack *bricklet_stack = (BrickletStack*)malloc(sizeof(BrickletStack));	
	if(bricklet_stack == NULL) {
		goto cleanup;
	}
	bricklet_stack->spi_fd = -1;
	bricklet_stack->spi_thread_running = false;

	memcpy(&bricklet_stack->config, config, sizeof(BrickletStackConfig));

	ringbuffer_init(&bricklet_stack->spi_receive_ringbuffer, 
	                BRICKLET_STACK_SPI_RECEIVE_BUFFER_SIZE, 
					bricklet_stack->spi_receive_buffer);

	// create base stack
	if (snprintf(bricklet_stack_name, 128, "bricklet-stack-%s", bricklet_stack->config.spi_device) < 0) {
		goto cleanup;
	}
	if (stack_create(&bricklet_stack->base, bricklet_stack_name, bricklet_stack_dispatch_to_spi) < 0) {
		log_error("Could not create base stack for BrickletStack: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 1;

	// add to stacks array
	if (hardware_add_stack(&bricklet_stack->base) < 0) {
		goto cleanup;
	}

	phase = 2;

	if ((bricklet_stack->notification_event = eventfd(0, EFD_NONBLOCK | EFD_SEMAPHORE)) < 0) {
		log_error("Could not create bricklet notification event: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 3;

	// Add notification pipe as event source.
	// Event is used to dispatch packets.
	if (snprintf(notification_name, 128, "bricklet-stack-notification-%s", bricklet_stack->config.spi_device) < 0) {
		goto cleanup;
	}
	if (event_add_source(bricklet_stack->notification_event, EVENT_SOURCE_TYPE_GENERIC,
	                     notification_name, EVENT_READ,
	                     bricklet_stack_dispatch_from_spi, bricklet_stack) < 0) {
		log_error("Could not add bricklet notification pipe as event source");

		goto cleanup;
	}

	phase = 4;

	// Initialize SPI packet queues
	if (queue_create(&bricklet_stack->request_queue, sizeof(Packet)) < 0) {
		log_error("Could not create SPI request queue: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}
	mutex_create(&bricklet_stack->request_queue_mutex);

	phase = 5;

	if (queue_create(&bricklet_stack->response_queue, sizeof(Packet)) < 0) {
		log_error("Could not create SPI response queue: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}
	mutex_create(&bricklet_stack->response_queue_mutex);

	phase = 6;

	if (bricklet_stack_init_spi(bricklet_stack) < 0) {
		goto cleanup;
	}

	phase = 7;

    cleanup:
	switch (phase) { // no breaks, all cases fall through intentionally
	case 6:
		mutex_destroy(&bricklet_stack->response_queue_mutex);
		queue_destroy(&bricklet_stack->response_queue, NULL);
		// fall through

	case 5:
		mutex_destroy(&bricklet_stack->request_queue_mutex);
		queue_destroy(&bricklet_stack->request_queue, NULL);

		// fall through

	case 4:
		event_remove_source(bricklet_stack->notification_event, EVENT_SOURCE_TYPE_GENERIC);
		// fall through

	case 3:
		close(bricklet_stack->notification_event);
		// fall through

	case 2:
		hardware_remove_stack(&bricklet_stack->base);
		// fall through

	case 1:
		stack_destroy(&bricklet_stack->base);
		// fall through

	default:
		break;
	}

	return phase == 7 ? bricklet_stack : NULL;
}

void bricklet_stack_exit(BrickletStack *bricklet_stack) {
	// Remove event as possible poll source
	event_remove_source(bricklet_stack->notification_event, EVENT_SOURCE_TYPE_GENERIC);

	// Make sure that Thread shuts down properly
	if (bricklet_stack->spi_thread_running) {
		bricklet_stack->spi_thread_running = false;

		thread_join(&bricklet_stack->spi_thread);
		thread_destroy(&bricklet_stack->spi_thread);
	}

	hardware_remove_stack(&bricklet_stack->base);
	stack_destroy(&bricklet_stack->base);

	queue_destroy(&bricklet_stack->request_queue, NULL);
	mutex_destroy(&bricklet_stack->request_queue_mutex);

	queue_destroy(&bricklet_stack->response_queue, NULL);
	mutex_destroy(&bricklet_stack->response_queue_mutex);

	// Close file descriptors
	close(bricklet_stack->notification_event);
	close(bricklet_stack->spi_fd);

	free(bricklet_stack);
}