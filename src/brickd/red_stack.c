/*
 * brickd
 * Copyright (C) 2014 Olaf Lüke <olaf@tinkerforge.com>
 *
 * red_stack.c: SPI stack support for RED Brick
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

#include <daemonlib/threads.h>
#include <daemonlib/packet.h>
#include <daemonlib/pipe.h>
#include <daemonlib/log.h>
#include <daemonlib/red_gpio.h>
#include <daemonlib/event.h>


#include "network.h"
#include "stack.h"
#include "hardware.h"

#include "red_usb_gadget.h"
#include "red_stack.h"

#define LOG_CATEGORY LOG_CATEGORY_SPI

// We use the Pearson Hash for fast hashing
// See: http://en.wikipedia.org/wiki/Pearson_hashing
// the permutation table is taken from the original paper:
// "Fast Hashing of Variable-Length Text Strings" by Peter K. Pearson,
// pp. 677-680, CACM 33(6), June 1990.

#define RED_STACK_SPI_PEARSON_PERMUTATION_SIZE 256
static const uint8_t _red_stack_spi_pearson_permutation[RED_STACK_SPI_PEARSON_PERMUTATION_SIZE] = {
    1, 87, 49, 12, 176, 178, 102, 166, 121, 193, 6, 84, 249, 230, 44, 163,
    14, 197, 213, 181, 161, 85, 218, 80, 64, 239, 24, 226, 236, 142, 38, 200,
    110, 177, 104, 103, 141, 253, 255, 50, 77, 101, 81, 18, 45, 96, 31, 222,
    25, 107, 190, 70, 86, 237, 240, 34, 72, 242, 20, 214, 244, 227, 149, 235,
    97, 234, 57, 22, 60, 250, 82, 175, 208, 5, 127, 199, 111, 62, 135, 248,
    174, 169, 211, 58, 66, 154, 106, 195, 245, 171, 17, 187, 182, 179, 0, 243,
    132, 56, 148, 75, 128, 133, 158, 100, 130, 126, 91, 13, 153, 246, 216, 219,
    119, 68, 223, 78, 83, 88, 201, 99, 122, 11, 92, 32, 136, 114, 52, 10,
    138, 30, 48, 183, 156, 35, 61, 26, 143, 74, 251, 94, 129, 162, 63, 152,
    170, 7, 115, 167, 241, 206, 3, 150, 55, 59, 151, 220, 90, 53, 23, 131,
    125, 173, 15, 238, 79, 95, 89, 16, 105, 137, 225, 224, 217, 160, 37, 123,
    118, 73, 2, 157, 46, 116, 9, 145, 134, 228, 207, 212, 202, 215, 69, 229,
    27, 188, 67, 124, 168, 252, 42, 4, 29, 108, 21, 247, 19, 205, 39, 203,
    233, 40, 186, 147, 198, 192, 155, 33, 164, 191, 98, 204, 165, 180, 117, 76,
    140, 36, 210, 172, 41, 54, 159, 8, 185, 232, 113, 196, 231, 47, 146, 120,
    51, 65, 28, 144, 254, 221, 93, 189, 194, 139, 112, 43, 71, 109, 184, 209
};
#define PEARSON(cur, next) do { cur = _red_stack_spi_pearson_permutation[cur ^ next]; } while(0)

#define RED_STACK_SPI_PACKET_SIZE       84
#define RED_STACK_SPI_PACKET_EMPTY_SIZE 4
#define RED_STACK_SPI_PREAMBLE_VALUE    0xAA
#define RED_STACK_SPI_PREAMBLE          0
#define RED_STACK_SPI_LENGTH            1
#define RED_STACK_SPI_INFO(length)      ((length) -2)
#define RED_STACK_SPI_CHECKSUM(length)  ((length) -1)
#define RED_STACK_SPI_INFO_BUSY         (1 << 0)
#define RED_STACK_SPI_MAX_SLAVES        8
#define RED_STACK_SPI_ROUTING_WAIT      (1000*1000*50) // Give slave 50ms between each routing table setup try
#define RED_STACK_SPI_ROUTING_TRIES     10             // Try 10 times for each slave to setup routing table

#define RED_STACK_SPI_CONFIG_MODE           SPI_CPOL
#define RED_STACK_SPI_CONFIG_LSB_FIRST      0
#define RED_STACK_SPI_CONFIG_BITS_PER_WORD  8
#define RED_STACK_SPI_CONFIG_MAX_SPEED_HZ   8000000

#define RED_STACK_TRANSCEIVE_DATA_SEND          (1 << 8)   // data has been send
#define RED_STACK_TRANSCEIVE_DATA_RECEIVED      (1 << 7)   // data has been received

#define RED_STACK_TRANSCEIVE_RESULT_SEND_ERROR  (1 << 0)   // data has not been send because of a problem (malformed packet or similar)
#define RED_STACK_TRANSCEIVE_RESULT_SEND_BUSY   (2 << 0)   // data has not been send but it was expected, try again!
#define RED_STACK_TRANSCEIVE_RESULT_SEND_NONE   (3 << 0)   // data has not been send because there was no data
#define RED_STACK_TRANSCEIVE_RESULT_SEND_OK     (4 << 0)   // data has been send
#define RED_STACK_TRANSCEIVE_RESULT_READ_ERROR  (1 << 3)   // data has not been received because of an problem (wrong checksum or similar)
#define RED_STACK_TRANSCEIVE_RESULT_READ_NONE   (2 << 3)   // data has not been received because slave had none
#define RED_STACK_TRANSCEIVE_RESULT_READ_OK     (3 << 3)   // data has been received

#define RED_STACK_TRANSCEIVE_RESULT_MASK_SEND   0x7
#define RED_STACK_TRANSCEIVE_RESULT_MASK_READ   0x38

static char packet_signature[PACKET_MAX_SIGNATURE_LENGTH];

static bool _red_stack_spi_thread_running = false;
static int _red_stack_spi_fd = -1;

static Thread _red_stack_spi_thread;
static Semaphore _red_stack_dispatch_packet_from_spi_semaphore;
static Mutex _red_stack_packet_queue_mutex;
static int _red_stack_notification_event;

typedef enum {
	RED_STACK_SLAVE_STATUS_ABSENT = 0,
	RED_STACK_SLAVE_STATUS_AVAILABLE,
	RED_STACK_SLAVE_STATUS_AVAILABLE_BUSY
} REDStackSlaveStatus;

typedef struct {
	uint8_t stack_address;
	REDStackSlaveStatus status;
	GPIOPin slave_select_pin;
} REDStackSlave;

typedef struct {
	Stack base;
	REDStackSlave slaves[RED_STACK_SPI_MAX_SLAVES];
	uint8_t slave_num;

	Packet packet_from_spi;
	Queue packet_to_spi_queue;
	struct timespec spi_deadline;
} REDStack;

typedef struct {
	REDStackSlave *slave;
	Packet packet;
} REDStackPacket;

static REDStack _red_stack;

static const GPIOPin _red_stack_master_high_pin = {GPIO_PORT_B, GPIO_PIN_11};
static const GPIOPin _red_stack_slave_select_pins[RED_STACK_SPI_MAX_SLAVES] = {
	{GPIO_PORT_C, GPIO_PIN_8},
	{GPIO_PORT_C, GPIO_PIN_9},
	{GPIO_PORT_C, GPIO_PIN_10},
	{GPIO_PORT_C, GPIO_PIN_11},
	{GPIO_PORT_C, GPIO_PIN_12},
	{GPIO_PORT_C, GPIO_PIN_13},
	{GPIO_PORT_C, GPIO_PIN_14},
	{GPIO_PORT_C, GPIO_PIN_15}
};

static const char *_red_stack_spi_device = "/dev/spidev0.0";

#define SLEEP_NS(value) do{ \
	struct timespec t; \
	t.tv_sec = 0; \
	t.tv_nsec = (value); \
	clock_nanosleep(CLOCK_MONOTONIC, 0, &t, NULL); \
}while(0)

#define PRINT_TIME(str) do { \
	struct timespec t; \
	clock_gettime(CLOCK_MONOTONIC, &t); \
	printf(str ": %lds %ldms %ldus %ldns\n", t.tv_sec, t.tv_nsec/(1000*1000), t.tv_nsec/1000, t.tv_nsec); \
}while(0)


// ----- RED STACK SPI ------
// These functions run in SPI thread

// Get "red_stack_dispatch_from_spi" called from main brickd event thread
static int red_stack_spi_request_dispatch_response_event(void) {
	eventfd_t ev = 1;
	if(eventfd_write(_red_stack_notification_event, ev) < 0) {
		log_error("Could not write to red stack spi notification event: %s (%d)",
		          get_errno_name(errno), errno);
		return -1;
	}

	return 0;
}

// Calculates a Pearson Hash for the given data
static uint8_t red_stack_spi_calculate_pearson_hash(const uint8_t *data, const uint8_t length) {
    uint8_t i;
    uint8_t checksum = 0;

    for(i = 0; i < length; i++) {
		checksum = _red_stack_spi_pearson_permutation[checksum ^ data[i]];
    }

    return checksum;
}

static void red_stack_spi_select(REDStackSlave *slave) {
	gpio_output_clear(slave->slave_select_pin);
}

static void red_stack_spi_deselect(REDStackSlave *slave) {
	gpio_output_set(slave->slave_select_pin);
}


// If data should just be polled, set packet_send to NULL.
//
// If no packet is received from slave the length in packet_recv will be set to 0,
// the exact reason for that is encoded in the return value.
//
// For the return value see RED_STACK_TRANSCEIVE_RESULT_* at the top of this file.
static int red_stack_spi_transceive_message(Packet *packet_send, Packet *packet_recv, REDStackSlave *slave) {
	int retval = 0;
	uint8_t length, length_send;
	uint8_t checksum;
	int rc;

    uint8_t tx[RED_STACK_SPI_PACKET_SIZE] = {0};
    uint8_t rx[RED_STACK_SPI_PACKET_SIZE] = {0};

    // We assume that we don't receive anything. If we receive a packet the
    // length will be overwritten again
    packet_recv->header.length = 0;

    // Preamble is always the same
    tx[RED_STACK_SPI_PREAMBLE] = RED_STACK_SPI_PREAMBLE_VALUE;

    if(packet_send == NULL) {
    	// If packet_send is NULL
    	// we send a message with empty payload (4 byte)
        tx[RED_STACK_SPI_LENGTH] = RED_STACK_SPI_PACKET_EMPTY_SIZE;
        retval |= RED_STACK_TRANSCEIVE_RESULT_SEND_NONE;
    } else if(slave->status == RED_STACK_SLAVE_STATUS_AVAILABLE_BUSY) {
    	// If the slave is known to be busy
    	// we also send a message with empty payload (4 byte)
        tx[RED_STACK_SPI_LENGTH] = RED_STACK_SPI_PACKET_EMPTY_SIZE;
        retval |= RED_STACK_TRANSCEIVE_RESULT_SEND_BUSY;
    } else if(slave->status == RED_STACK_SLAVE_STATUS_AVAILABLE) {
    	length = packet_send->header.length;
    	if(length > sizeof(Packet)) {
    		retval |= RED_STACK_TRANSCEIVE_RESULT_SEND_ERROR;
			log_error("Send length is greater then allowed (actual: %d > maximum: %d)",
			          length, (int)sizeof(Packet));
    		goto ret;
    	}

    	retval |= RED_STACK_TRANSCEIVE_DATA_SEND | RED_STACK_TRANSCEIVE_RESULT_SEND_OK;

        tx[RED_STACK_SPI_LENGTH] = length + RED_STACK_SPI_PACKET_EMPTY_SIZE;
    	memcpy(tx+2, packet_send, length);
    } else {
    	retval |= RED_STACK_TRANSCEIVE_RESULT_SEND_ERROR;
    	log_error("Slave with stack address %d is not present in stack", slave->stack_address);
    	goto ret;
    }

    length = tx[RED_STACK_SPI_LENGTH];

    // The SPI Master is never busy
    tx[RED_STACK_SPI_INFO(length)] = 0;
    // Calculate checksum
    tx[RED_STACK_SPI_CHECKSUM(length)] = red_stack_spi_calculate_pearson_hash(tx, length-1);

    struct spi_ioc_transfer spi_transfer = {
        .tx_buf = (unsigned long)&tx,
        .rx_buf = (unsigned long)&rx,
        .len = RED_STACK_SPI_PACKET_SIZE,
    };

	red_stack_spi_select(slave);
	rc = ioctl(_red_stack_spi_fd, SPI_IOC_MESSAGE(1), &spi_transfer);
	red_stack_spi_deselect(slave);

	if (rc < 0) {
		// Overwrite current return status with error,
		// it seems ioctl itself didn't work.
		retval = RED_STACK_TRANSCEIVE_RESULT_SEND_ERROR | RED_STACK_TRANSCEIVE_RESULT_READ_ERROR;
		log_error("ioctl failed: %s (%d)", get_errno_name(errno), errno);
		goto ret;
	}

	length_send = rc;

	if(length_send != RED_STACK_SPI_PACKET_SIZE) {
		// Overwrite current return status with error,
		// it seems ioctl itself didn't work.
		retval = RED_STACK_TRANSCEIVE_RESULT_SEND_ERROR | RED_STACK_TRANSCEIVE_RESULT_READ_ERROR;
		log_error("ioctl has unexpected result (actual: %d != expected: %d)",
		          length_send, RED_STACK_SPI_PACKET_SIZE);
		goto ret;
	}

	if(rx[RED_STACK_SPI_PREAMBLE] != RED_STACK_SPI_PREAMBLE_VALUE) {
		// Do not log by default, an "unproper preamble" is part of the protocol
		// if the slave is too busy to fill the DMA buffers fast enough
		// log_error("Received packet without proper preamble (actual: %d != expected: %d)",
		//          rx[RED_STACK_SPI_PREAMBLE], RED_STACK_SPI_PREAMBLE_VALUE);
		retval |= RED_STACK_TRANSCEIVE_RESULT_READ_ERROR;
		retval |= RED_STACK_TRANSCEIVE_RESULT_READ_NONE;
		slave->status = RED_STACK_SLAVE_STATUS_AVAILABLE_BUSY;
		goto ret;
	}

	// Check length
	length = rx[RED_STACK_SPI_LENGTH];

	if((length != RED_STACK_SPI_PACKET_EMPTY_SIZE) &&
	   ((length < (RED_STACK_SPI_PACKET_EMPTY_SIZE + sizeof(PacketHeader))) ||
	    (length > RED_STACK_SPI_PACKET_SIZE))) {
		log_error("Received packet with malformed length: %d", length);
		retval |= RED_STACK_TRANSCEIVE_RESULT_READ_ERROR;
		goto ret;
	}

	// Calculate and check checksum
	checksum = red_stack_spi_calculate_pearson_hash(rx, length-1);
	if(checksum != rx[RED_STACK_SPI_CHECKSUM(length)]) {
		log_error("Received packet with wrong checksum (actual: %x != expected: %x)",
		          checksum, rx[RED_STACK_SPI_CHECKSUM(length)]);
		retval |= RED_STACK_TRANSCEIVE_RESULT_READ_ERROR;
		goto ret;
	}

	if(rx[RED_STACK_SPI_INFO(length)] & RED_STACK_SPI_INFO_BUSY) {
		slave->status = RED_STACK_SLAVE_STATUS_AVAILABLE_BUSY;
	} else {
		if(retval & RED_STACK_TRANSCEIVE_DATA_SEND) {
			// If we did send data this time, we have to assume
			// that the receiver buffer of the slave is full the next time
			// Regardless of what the slave is telling us.
			slave->status = RED_STACK_SLAVE_STATUS_AVAILABLE_BUSY;
		} else {
			slave->status = RED_STACK_SLAVE_STATUS_AVAILABLE;
		}
	}

	if(length == RED_STACK_SPI_PACKET_EMPTY_SIZE) {
		// Do not log by default, will produce 2000 log entries per second
   		// log_debug("Received empty packet over SPI (w/ header)");
   		retval |= RED_STACK_TRANSCEIVE_RESULT_READ_NONE;
	} else {
		// Everything seems OK, we can copy to buffer
		memcpy(packet_recv, rx+2, length - RED_STACK_SPI_PACKET_EMPTY_SIZE);
		log_debug("Received packet over SPI (%s)",
		          packet_get_response_signature(packet_signature, packet_recv));
		retval |= RED_STACK_TRANSCEIVE_RESULT_READ_OK | RED_STACK_TRANSCEIVE_DATA_RECEIVED;
	}

ret:
	// IF we have any kind of error we assume that the slave is busy
	if(((retval & RED_STACK_TRANSCEIVE_RESULT_MASK_SEND) == RED_STACK_TRANSCEIVE_RESULT_SEND_ERROR) ||
	   ((retval & RED_STACK_TRANSCEIVE_RESULT_MASK_READ) == RED_STACK_TRANSCEIVE_RESULT_READ_ERROR)) {
		slave->status = RED_STACK_SLAVE_STATUS_AVAILABLE_BUSY;
	}

	return retval;
}

// Creates the "routing table", which is just the
// array of REDStackSlave structures.
static void red_stack_spi_create_routing_table() {
	char base58[BASE58_MAX_LENGTH];
	int tries, ret, i;
	uint8_t stack_address = 0;
	uint8_t uid_counter = 0;

	log_debug("Starting to discover SPI stack slaves");

    while(stack_address < RED_STACK_SPI_MAX_SLAVES) {
    	REDStackSlave *slave = &_red_stack.slaves[stack_address];

    	Packet packet;
    	StackEnumerateResponse *response;
    	StackEnumerateRequest request = {{
    		0,   // UID 0
    		sizeof(StackEnumerateRequest),
    		FUNCTION_STACK_ENUMERATE,
    		0x08, // Return expecetd
    		0
    	}};

    	// Lets assume the slave is available and see if we get an answer
    	slave->status = RED_STACK_SLAVE_STATUS_AVAILABLE;

    	// Send stack enumerate request
    	for(tries = 0; tries < RED_STACK_SPI_ROUTING_TRIES; tries++) {
    		ret = red_stack_spi_transceive_message((Packet *)&request, &packet, slave);
    		if(ret & RED_STACK_TRANSCEIVE_DATA_SEND) {
    			break;
    		}
    		SLEEP_NS(RED_STACK_SPI_ROUTING_WAIT); // Give slave some more time
    	}

    	if(tries == RED_STACK_SPI_ROUTING_TRIES) {
    		// Slave does not seem to be available,
    		slave->status = RED_STACK_SLAVE_STATUS_ABSENT;
    		// This means that there can't be any more slaves above
    		// and we are actually done here already!
    		break;
    	}

    	// Receive stack enumerate response
    	for(tries = 0; tries < RED_STACK_SPI_ROUTING_TRIES; tries++) {
    		// Here we sleep in the beginning so that there is some time
    		// between the sending of stack enumerate and the receiving
    		// of the answer
    		SLEEP_NS(RED_STACK_SPI_ROUTING_WAIT); // Give slave some more time

    		ret = red_stack_spi_transceive_message(NULL, &packet, slave);
    		if(ret & RED_STACK_TRANSCEIVE_DATA_RECEIVED) {
    			break;
    		}
    	}

    	if(tries == RED_STACK_SPI_ROUTING_TRIES) {
    		// Slave does not seem to be available,
    		slave->status = RED_STACK_SLAVE_STATUS_ABSENT;
    		// This means that there can't be any more slaves above
    		// and we are actually done here already!
    		break;
    	}

    	response = (StackEnumerateResponse *)&packet;

    	for(i = 0; i < PACKET_MAX_STACK_ENUMERATE_UIDS; i++) {
    		if(response->uids[i] != 0) {
    			uid_counter++;
    			stack_add_recipient(&_red_stack.base, response->uids[i], stack_address);
    			log_debug("Found UID number %d of slave %d with UID %s",
    			          i, stack_address,
    			          base58_encode(base58, uint32_from_le(response->uids[i])));
    		} else {
    			break;
    		}
    	}

    	stack_address++;
    }
    _red_stack.slave_num = stack_address;

	log_debug("SPI stack slave discovery done. Found %d slave(s) with %d UID(s) in total",
	          stack_address, uid_counter);
}

static void red_stack_spi_insert_position(REDStackSlave *slave) {
	if(_red_stack.packet_from_spi.header.function_id == CALLBACK_ENUMERATE ||
	   _red_stack.packet_from_spi.header.function_id == FUNCTION_GET_IDENTITY) {
		EnumerateCallback *enum_cb = (EnumerateCallback *)&_red_stack.packet_from_spi;
		if(enum_cb->position == '0') {
			enum_cb->position = '0' + slave->stack_address + 1;
			base58_encode(enum_cb->connected_uid, uint32_from_le(red_usb_gadget_get_uid()));
		}
	}
}

// Main SPI loop. This runs independently from the brickd event thread.
// Data between RED Brick and SPI slave is exchanged every 500us.
// If there is no data to be send, we cycle through the slaves and request
// data. If there is data to be send the slave that ought to receive
// the data gets priority. This can greatly reduce latency in a big stack.
static void red_stack_spi_thread(void *opaque) {
	REDStackPacket *packet_to_spi;
	uint8_t stack_address_cycle = 0;
	int ret;

	(void)opaque;

	red_stack_spi_create_routing_table();

	if(_red_stack.slave_num == 0) {
		log_info("Shutting SPI stack thread down, there are no SPI slaves");
		return;
	}

	clock_gettime(CLOCK_MONOTONIC, &_red_stack.spi_deadline);
	_red_stack_spi_thread_running = true;
	while(_red_stack_spi_thread_running) {
		REDStackSlave *slave = &_red_stack.slaves[stack_address_cycle];
		Packet *request = NULL;
		memset(&_red_stack.packet_from_spi, 0, sizeof(Packet));

		// Get packet from queue. The queue contains that are to be
		// send over SPI. It is filled through from the main brickd
		// event thread, so we have to make sure that there is not race
		// condition.
		mutex_lock(&_red_stack_packet_queue_mutex);
		packet_to_spi = queue_peek(&_red_stack.packet_to_spi_queue);
		mutex_unlock(&_red_stack_packet_queue_mutex);

		if(packet_to_spi == NULL) {
			// If there is no packet in the queue we just cycle through the slaves
			stack_address_cycle++;
			if(stack_address_cycle >= _red_stack.slave_num) {
				stack_address_cycle = 0;
			}
		} else {
			// Otherwise the request gets send
			log_debug("Packet will now be send over SPI (%s)",
			          packet_get_request_signature(packet_signature, &packet_to_spi->packet));

			request = &packet_to_spi->packet;
			slave = packet_to_spi->slave;
		}

		ret = red_stack_spi_transceive_message(request,
		                                       &_red_stack.packet_from_spi,
		                                       slave);

		if(ret & RED_STACK_TRANSCEIVE_DATA_SEND) {
			if((!((ret & RED_STACK_TRANSCEIVE_RESULT_MASK_SEND) == RED_STACK_TRANSCEIVE_RESULT_SEND_ERROR)) &&
			   (!((ret & RED_STACK_TRANSCEIVE_RESULT_MASK_READ) == RED_STACK_TRANSCEIVE_RESULT_READ_ERROR))) {
				// If we send a packet it must have come from the queue, so we can
				// pop it from the queue now.
				// If the sending didn't work (for whatever reason), we don't pop it
				// and therefore we will automatically try to send it again in the next cycle.
				mutex_lock(&_red_stack_packet_queue_mutex);
				queue_pop(&_red_stack.packet_to_spi_queue, NULL);
				mutex_unlock(&_red_stack_packet_queue_mutex);
			}
		}

		// If we received a packet, we will dispatch it immediately.
		// We have some time until we try the next SPI communication anyway.
		if(ret & RED_STACK_TRANSCEIVE_DATA_RECEIVED) {
			// TODO: Check again if packet is valid?
			// We did already check the hash.

			// Before the dispatching we insert the stack position into an enumerate message
			red_stack_spi_insert_position(slave);

			red_stack_spi_request_dispatch_response_event();
			// Wait until message is dispatched, so we don't overwrite it
			// accidentally.
			semaphore_acquire(&_red_stack_dispatch_packet_from_spi_semaphore);
		}

		// TODO: Get sleep time between transfers through RED Brick API with a minimum of 50us
		SLEEP_NS(1000*50);
	}
}


// ----- RED STACK -----
// These functions run in brickd main thread

static int red_stack_init_spi(void) {
	uint8_t slave;
	const uint8_t mode = RED_STACK_SPI_CONFIG_MODE;
	const uint8_t lsb_first = RED_STACK_SPI_CONFIG_LSB_FIRST;
	const uint8_t bits_per_word = RED_STACK_SPI_CONFIG_BITS_PER_WORD;
	const uint32_t max_speed_hz = RED_STACK_SPI_CONFIG_MAX_SPEED_HZ;

	// Set Master High pin to low (so Master Bricks above RED Brick can
	// configure themselves as slave)
	gpio_mux_configure(_red_stack_master_high_pin, GPIO_MUX_OUTPUT);
	gpio_output_clear(_red_stack_master_high_pin);

	// TODO: Reset slaves and sleep (can't be implemented in current hardware version)

	// Initialize slaves
	for(slave = 0; slave < RED_STACK_SPI_MAX_SLAVES; slave++) {
		_red_stack.slaves[slave].stack_address = slave;
		_red_stack.slaves[slave].status = RED_STACK_SLAVE_STATUS_ABSENT;
		_red_stack.slaves[slave].slave_select_pin = _red_stack_slave_select_pins[slave];

		// Bring slave in initial state (deselected)
		gpio_mux_configure(_red_stack.slaves[slave].slave_select_pin, GPIO_MUX_OUTPUT);
		red_stack_spi_deselect(&_red_stack.slaves[slave]);
	}

	// Open spidev
	_red_stack_spi_fd = open(_red_stack_spi_device, O_RDWR);
	if(_red_stack_spi_fd < 0) {
		log_error("Could not open %s", _red_stack_spi_device);
		return -1;
	}

	if(ioctl(_red_stack_spi_fd, SPI_IOC_WR_MODE, &mode) < 0) {
		log_error("Could not configure SPI mode");
		return -1;
	}

	if(ioctl(_red_stack_spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &max_speed_hz) < 0) {
		log_error("Could not configure SPI max speed");
		return -1;
	}

	if(ioctl(_red_stack_spi_fd, SPI_IOC_WR_BITS_PER_WORD, &bits_per_word) < 0) {
		log_error("Could not configure SPI bits per word");
		return -1;
	}

	if(ioctl(_red_stack_spi_fd, SPI_IOC_WR_LSB_FIRST, &lsb_first) < 0) {
		log_error("Could not configure SPI lsb first");
		return -1;
	}

	// Create SPI packet transceive thread
	// FIXME: maybe handshake thread start?
	thread_create(&_red_stack_spi_thread, red_stack_spi_thread, NULL);

	return 0;
}

// New packet from SPI stack is send into brickd event loop
static void red_stack_dispatch_from_spi(void *opaque) {
	eventfd_t ev;
	(void)opaque;

	if(eventfd_read(_red_stack_notification_event, &ev) < 0) {
		log_error("Could not read from SPI notification event: %s (%d)",
		          get_errno_name(errno), errno);
		return;
	}

	// Send message into brickd dispatcher
	// and allow SPI thread to run again.
	network_dispatch_response(&_red_stack.packet_from_spi);
	semaphore_release(&_red_stack_dispatch_packet_from_spi_semaphore);

}

// New packet from brickd event loop is queued to be written to stack via SPI
static void red_stack_dispatch_to_spi(Stack *stack, Packet *request, Recipient *recipient) {
	REDStackPacket *queued_request;
	(void)stack;

	if(request->header.uid == 0) {
		// UID = 0 -> Broadcast to all UIDs
		uint8_t is;
		for(is = 0; is < _red_stack.slave_num; is++) {
			mutex_lock(&_red_stack_packet_queue_mutex);
			queued_request = queue_push(&_red_stack.packet_to_spi_queue);
			queued_request->slave = &_red_stack.slaves[is];
			memcpy(&queued_request->packet, request, request->header.length);
			mutex_unlock(&_red_stack_packet_queue_mutex);

			log_debug("Request is queued to be broadcast to slave %d (%s)",
			          is,
			          packet_get_request_signature(packet_signature, request));

		}
	} else if (recipient != NULL) {
		// Get slave for recipient opaque (== stack_address)
		REDStackSlave *slave = &_red_stack.slaves[recipient->opaque];

		mutex_lock(&_red_stack_packet_queue_mutex);
		queued_request = queue_push(&_red_stack.packet_to_spi_queue);
		queued_request->slave = slave;
		memcpy(&queued_request->packet, request, request->header.length);
		mutex_unlock(&_red_stack_packet_queue_mutex);

		log_debug("Packet is queued to be send to slave %d over SPI (%s)",
		          slave->stack_address,
		          packet_get_request_signature(packet_signature, request));
	}
}

int red_stack_init(void) {
	int phase = 0;

	log_debug("Initializing RED Brick SPI Stack subsystem");

	// create base stack
	if(stack_create(&_red_stack.base, "red_stack",
	                (StackDispatchRequestFunction)red_stack_dispatch_to_spi) < 0) {
		log_error("Could not create base stack for RED Brick SPI Stack: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 1;

	// add to stacks array
	if(hardware_add_stack(&_red_stack.base) < 0) {
		goto cleanup;
	}

	phase = 2;

	if((_red_stack_notification_event = eventfd(0, 0)) < 0) {
		log_error("Could not create red stack notification event: %s (%d)",
		          get_errno_name(errno), errno);
		goto cleanup;
	}

	phase = 3;

	// Add notification pipe as event source.
	// Event is used to dispatch packets.
	if(event_add_source(_red_stack_notification_event, EVENT_SOURCE_TYPE_GENERIC,
	                    EVENT_READ, red_stack_dispatch_from_spi, NULL) < 0) {
		log_error("Could not add red stack notification pipe as event source");
		goto cleanup;
	}

	phase = 4;

	// Initialize SPI packet queue
	if(queue_create(&_red_stack.packet_to_spi_queue, sizeof(REDStackPacket)) < 0) {
		log_error("Could not create SPI queue: %s (%d)",
		          get_errno_name(errno), errno);
		goto cleanup;
	}

	phase = 5;

	if(semaphore_create(&_red_stack_dispatch_packet_from_spi_semaphore) < 0) {
		log_error("Could not create SPI request semaphore: %s (%d)",
		          get_errno_name(errno), errno);
		goto cleanup;
	}

	mutex_create(&_red_stack_packet_queue_mutex);

	phase = 6;

	if(red_stack_init_spi() < 0) {
		goto cleanup;
	}

	phase = 7;

cleanup:
	switch (phase) { // no breaks, all cases fall through intentionally
	case 6:
		mutex_destroy(&_red_stack_packet_queue_mutex);
		semaphore_destroy(&_red_stack_dispatch_packet_from_spi_semaphore);

	case 5:
		queue_destroy(&_red_stack.packet_to_spi_queue, NULL);

	case 4:
		event_remove_source(_red_stack_notification_event, EVENT_SOURCE_TYPE_GENERIC);

	case 3:
		close(_red_stack_notification_event);

	case 2:
		hardware_remove_stack(&_red_stack.base);

	case 1:
		stack_destroy(&_red_stack.base);

	default:
		break;
	}

	return phase == 7 ? 0 : -1;
}

void red_stack_exit(void) {
	int slave;

	// Remove event as possible poll source
	event_remove_source(_red_stack_notification_event, EVENT_SOURCE_TYPE_GENERIC);

	// Make sure that Thread shuts down properly
	if(_red_stack_spi_thread_running) {
		_red_stack_spi_thread_running = false;
		// Write in eventfd to make sure that we are not blocking the Thread
		eventfd_t ev = 1;
		eventfd_write(_red_stack_notification_event, ev);

		thread_join(&_red_stack_spi_thread);
		thread_destroy(&_red_stack_spi_thread);
	}

	// Thread is not running anymore, we make sure that all slaves are deselected
	for(slave = 0; slave < RED_STACK_SPI_MAX_SLAVES; slave++) {
		red_stack_spi_deselect(&_red_stack.slaves[slave]);
	}

	// We can also free the queue and stack now, nobody will use them anymore
	queue_destroy(&_red_stack.packet_to_spi_queue, NULL);
	hardware_remove_stack(&_red_stack.base);
	stack_destroy(&_red_stack.base);

	mutex_destroy(&_red_stack_packet_queue_mutex);
	semaphore_destroy(&_red_stack_dispatch_packet_from_spi_semaphore);

	// Close file descriptors
	close(_red_stack_notification_event);
	close(_red_stack_spi_fd);
}
