/*
 * brickd
 * Copyright (C) 2018 Olaf Lüke <olaf@tinkerforge.com>
 *
 * bricklet_stack.h: SPI Tinkerforge Protocol (SPITFP) implementation for direct
 *                   communication between brickd and Bricklets with co-processor
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

#ifndef BRICKD_BRICKLET_STACK_H
#define BRICKD_BRICKLET_STACK_H

#include <stdbool.h>
#include <stdint.h>

#include <daemonlib/threads.h>
#include <daemonlib/queue.h>
#include <daemonlib/ringbuffer.h>

#include "stack.h"

#define BRICKLET_STACK_SPI_RECEIVE_BUFFER_SIZE 1024

typedef struct {
    char spi_device[64]; // e.g. "/dev/spidev0.0";
} BrickletStackConfig;

typedef struct {
    Stack base;

	Queue request_queue;
	Mutex request_queue_mutex;

	Queue response_queue;
	Mutex response_queue_mutex;

    Ringbuffer spi_receive_ringbuffer;
    uint8_t spi_receive_buffer[BRICKLET_STACK_SPI_RECEIVE_BUFFER_SIZE];

	int notification_event;
	int spi_fd;
	bool spi_thread_running;
	Thread spi_thread;

	BrickletStackConfig config;
} BrickletStack;

BrickletStack* bricklet_stack_init(BrickletStackConfig *config);
void bricklet_stack_exit(BrickletStack *bricklet_stack);

#endif // BRICKD_BRICKLET_STACK_H