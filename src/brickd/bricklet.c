/*
 * brickd
 * Copyright (C) 2018 Olaf Lüke <olaf@tinkerforge.com>
 *
 * bricklet.c: SPI Tinkerforge Protocol (SPITFP) implementation for direct
 *             communication between brickd and Bricklets with co-processor
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

#include "bricklet.h"

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

#define BRICKLET_SPI_CONFIG_MODE           SPI_MODE_0
#define BRICKLET_SPI_CONFIG_LSB_FIRST      0
#define BRICKLET_SPI_CONFIG_BITS_PER_WORD  8
#define BRICKLET_SPI_CONFIG_MAX_SPEED_HZ   2000000

static LogSource _log_source = LOG_SOURCE_INITIALIZER;


// New packet from brickd event loop is queued to be written to Bricklet via SPI
static int bricklet_dispatch_to_spi(Stack *stack, Packet *request, Recipient *recipient) {
	Bricklet *bricklet = (Bricklet*)stack;

    return 0;
}

// New packet from Bricklet is send into brickd event loop
static void bricklet_dispatch_from_spi(void *opaque) {
	Bricklet *bricklet = (Bricklet*)opaque;
}

static void bricklet_spi_thread(void *opaque) {
	Bricklet *bricklet = (Bricklet*)opaque;
	bricklet->spi_thread_running = true;
	while (bricklet->spi_thread_running) {
		uint8_t tx[64] = {1, 2, 3, 4, 5, 6, 7, 8};
		uint8_t rx[64] = {0};

		struct spi_ioc_transfer spi_transfer = {
			.tx_buf = (unsigned long)&tx,
			.rx_buf = (unsigned long)&rx,
			.len = 64,
		};

		int rc = ioctl(bricklet->spi_fd, SPI_IOC_MESSAGE(1), &spi_transfer);

		struct timespec t;
		t.tv_sec = 1;
		t.tv_nsec = 0;
		clock_nanosleep(CLOCK_MONOTONIC, 0, &t, NULL);
	}
}

static int bricklet_init_spi(Bricklet *bricklet) {
	const uint8_t mode = BRICKLET_SPI_CONFIG_MODE;
	const uint8_t lsb_first = BRICKLET_SPI_CONFIG_LSB_FIRST;
	const uint8_t bits_per_word = BRICKLET_SPI_CONFIG_BITS_PER_WORD;
	const uint32_t max_speed_hz = BRICKLET_SPI_CONFIG_MAX_SPEED_HZ;

	// Open spidev
	bricklet->spi_fd = open(bricklet->config.spi_device, O_RDWR);
	if (bricklet->spi_fd < 0) {
		log_error("Could not open %s", bricklet->config.spi_device);
		return -1;
	}

	if (ioctl(bricklet->spi_fd, SPI_IOC_WR_MODE, &mode) < 0) {
		log_error("Could not configure SPI mode");
		return -1;
	}

	if (ioctl(bricklet->spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &max_speed_hz) < 0) {
		log_error("Could not configure SPI max speed");
		return -1;
	}

	if (ioctl(bricklet->spi_fd, SPI_IOC_WR_BITS_PER_WORD, &bits_per_word) < 0) {
		log_error("Could not configure SPI bits per word");
		return -1;
	}

	if (ioctl(bricklet->spi_fd, SPI_IOC_WR_LSB_FIRST, &lsb_first) < 0) {
		log_error("Could not configure SPI lsb first");
		return -1;
	}

	thread_create(&bricklet->spi_thread, bricklet_spi_thread, bricklet);

	return 0;
}

Bricklet* bricklet_init(BrickletConfig *config) {
    int phase = 0;

    log_debug("Initializing Bricklet subsystem");

	// create bricklet struct
	Bricklet *bricklet = (Bricklet*)malloc(sizeof(Bricklet));	
	if(bricklet == NULL) {
		goto cleanup;
	}
	bricklet->spi_fd = -1;
	bricklet->spi_thread_running = false;

	memcpy(&bricklet->config, config, sizeof(BrickletConfig));

	// create base stack
	if (stack_create(&bricklet->base, "bricklet", bricklet_dispatch_to_spi) < 0) {
		log_error("Could not create base stack for Bricklet: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 1;

	// add to stacks array
	if (hardware_add_stack(&bricklet->base) < 0) {
		goto cleanup;
	}

	phase = 2;

	if ((bricklet->notification_event = eventfd(0, EFD_NONBLOCK | EFD_SEMAPHORE)) < 0) {
		log_error("Could not create bricklet notification event: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 3;

	// Add notification pipe as event source.
	// Event is used to dispatch packets.
	if (event_add_source(bricklet->notification_event, EVENT_SOURCE_TYPE_GENERIC,
	                     "bricklet-notification", EVENT_READ,
	                     bricklet_dispatch_from_spi, bricklet) < 0) {
		log_error("Could not add bricklet notification pipe as event source");

		goto cleanup;
	}

	phase = 4;

	// Initialize SPI packet queues
	if (queue_create(&bricklet->request_queue, sizeof(Packet)) < 0) {
		log_error("Could not create SPI request queue: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}
	mutex_create(&bricklet->request_queue_mutex);

	phase = 5;

	if (queue_create(&bricklet->response_queue, sizeof(Packet)) < 0) {
		log_error("Could not create SPI response queue: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}
	mutex_create(&bricklet->response_queue_mutex);

	phase = 6;

	if (bricklet_init_spi(bricklet) < 0) {
		goto cleanup;
	}

	phase = 7;

    cleanup:
	switch (phase) { // no breaks, all cases fall through intentionally
	case 6:
		mutex_destroy(&bricklet->response_queue_mutex);
		queue_destroy(&bricklet->response_queue, NULL);
		// fall through

	case 5:
		mutex_destroy(&bricklet->request_queue_mutex);
		queue_destroy(&bricklet->request_queue, NULL);

		// fall through

	case 4:
		event_remove_source(bricklet->notification_event, EVENT_SOURCE_TYPE_GENERIC);
		// fall through

	case 3:
		close(bricklet->notification_event);
		// fall through

	case 2:
		hardware_remove_stack(&bricklet->base);
		// fall through

	case 1:
		stack_destroy(&bricklet->base);
		// fall through

	default:
		break;
	}

	return phase == 7 ? bricklet : NULL;
}

void bricklet_exit(Bricklet *bricklet) {
	// Remove event as possible poll source
	event_remove_source(bricklet->notification_event, EVENT_SOURCE_TYPE_GENERIC);

	// Make sure that Thread shuts down properly
	if (bricklet->spi_thread_running) {
		bricklet->spi_thread_running = false;

		thread_join(&bricklet->spi_thread);
		thread_destroy(&bricklet->spi_thread);
	}

	hardware_remove_stack(&bricklet->base);
	stack_destroy(&bricklet->base);

	queue_destroy(&bricklet->request_queue, NULL);
	mutex_destroy(&bricklet->request_queue_mutex);

	queue_destroy(&bricklet->response_queue, NULL);
	mutex_destroy(&bricklet->response_queue_mutex);

	// Close file descriptors
	close(bricklet->notification_event);
	close(bricklet->spi_fd);

	free(bricklet);
}