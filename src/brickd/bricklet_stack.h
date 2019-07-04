/*
 * brickd
 * Copyright (C) 2018 Olaf Lüke <olaf@tinkerforge.com>
 * Copyright (C) 2019 Matthias Bolte <matthias@tinkerforge.com>
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
#include <daemonlib/gpio_sysfs.h>

#include "stack.h"

#define BRICKLET_STACK_SPI_RECEIVE_BUFFER_LENGTH 1024 // keep as power of 2
#define BRICKLET_STACK_SPI_RECEIVE_BUFFER_MASK   (BRICKLET_STACK_SPI_RECEIVE_BUFFER_LENGTH-1)

#define BRICKLET_STACK_FIRST_MESSAGE_TRIES 1000

#define TFP_MESSAGE_MIN_LENGTH 8
#define TFP_MESSAGE_MAX_LENGTH 80

#define SPITFP_PROTOCOL_OVERHEAD 3 // 3 byte overhead for Brick <-> Bricklet SPI protocol

#define SPITFP_MIN_TFP_MESSAGE_LENGTH (TFP_MESSAGE_MIN_LENGTH + SPITFP_PROTOCOL_OVERHEAD)
#define SPITFP_MAX_TFP_MESSAGE_LENGTH (TFP_MESSAGE_MAX_LENGTH + SPITFP_PROTOCOL_OVERHEAD)

#define SPITFP_TIMEOUT 5 // in ms

typedef enum {
	CHIP_SELECT_HARDWARE,
	CHIP_SELECT_GPIO,
	CHIP_SELECT_WIRINGPI // TODO
} BrickletStackChipSelectDriver;

typedef struct {
    char spi_device[64]; // e.g. "/dev/spidev0.0";
	int mode;
	BrickletStackChipSelectDriver chip_select_driver;

	// Unused in case of hardware or WiringPi CS
	GPIOSYSFS chip_select_gpio_sysfs;

	// TODO: Add WiringPi structure

	// One mutex per spidev, so that we can use several SPI hardware units in parallel.
	// Has to be properly managed during initialization.
	Mutex *mutex;

	uint32_t *connected_uid;
	uint8_t num;
	uint32_t startup_wait_time; // in milliseconds
} BrickletStackConfig;

typedef struct {
	Stack base;

	Queue request_queue;
	Mutex request_queue_mutex;

	Queue response_queue;
	Mutex response_queue_mutex;

	int notification_event;
	int spi_fd;
	bool spi_thread_running;
	Thread spi_thread;

	BrickletStackConfig config;

	// SPITFP protocol related variables.
	uint8_t buffer_recv[BRICKLET_STACK_SPI_RECEIVE_BUFFER_LENGTH];
	uint8_t buffer_send[TFP_MESSAGE_MAX_LENGTH + SPITFP_PROTOCOL_OVERHEAD*2]; // *2 for send message overhead and additional ACK

	uint8_t buffer_send_length;

	uint8_t buffer_recv_tmp[TFP_MESSAGE_MAX_LENGTH + SPITFP_PROTOCOL_OVERHEAD*2];
	uint8_t buffer_recv_tmp_length;

	uint8_t current_sequence_number;
	uint8_t last_sequence_number_seen;
	uint64_t last_send_started;

	Ringbuffer ringbuffer_recv;

	bool ack_to_send;
	bool wait_for_ack;
	bool data_seen;

	uint32_t error_count_ack_checksum;
	uint32_t error_count_message_checksum;
	uint32_t error_count_frame;
	uint32_t error_count_overflow;

	uint32_t first_message_tries;
} BrickletStack;

BrickletStack* bricklet_stack_init(BrickletStackConfig *config);
void bricklet_stack_exit(BrickletStack *bricklet_stack);

#endif // BRICKD_BRICKLET_STACK_H
