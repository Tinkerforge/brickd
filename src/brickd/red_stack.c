/*
 * brickd
 * Copyright (C) 2014 Olaf Lüke <olaf@tinkerforge.com>
 * Copyright (C) 2014 Matthias Bolte <matthias@tinkerforge.com>
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

#include <daemonlib/base58.h>
#include <daemonlib/config.h>
#include <daemonlib/event.h>
#include <daemonlib/io.h>
#include <daemonlib/log.h>
#include <daemonlib/packet.h>
#include <daemonlib/pipe.h>
#include <daemonlib/red_gpio.h>
#include <daemonlib/threads.h>

#include "red_stack.h"

#include "hardware.h"
#include "network.h"
#include "red_usb_gadget.h"
#include "stack.h"

static LogSource _log_source = LOG_SOURCE_INITIALIZER;

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
#define RED_STACK_SPI_MAX_SLAVES        8
#define RED_STACK_SPI_ROUTING_WAIT      (1000*1000*50) // Give slave 50ms between each routing table setup try
#define RED_STACK_SPI_ROUTING_TRIES     10             // Try 10 times for each slave to setup routing table

#define RED_STACK_SPI_INFO_SEQUENCE_MASTER_MASK (0x07)
#define RED_STACK_SPI_INFO_SEQUENCE_SLAVE_MASK  (0x38)

#define RED_STACK_SPI_CONFIG_MODE           SPI_CPOL
#define RED_STACK_SPI_CONFIG_LSB_FIRST      0
#define RED_STACK_SPI_CONFIG_BITS_PER_WORD  8
#define RED_STACK_SPI_CONFIG_MAX_SPEED_HZ   8000000

#define RED_STACK_TRANSCEIVE_DATA_SEND          (1 << 8)   // data has been send
#define RED_STACK_TRANSCEIVE_DATA_RECEIVED      (1 << 7)   // data has been received

#define RED_STACK_TRANSCEIVE_RESULT_SEND_ERROR  (1 << 0)   // data has not been send because of a problem (malformed packet or similar)
#define RED_STACK_TRANSCEIVE_RESULT_SEND_NONE   (2 << 0)   // data has not been send because there was no data
#define RED_STACK_TRANSCEIVE_RESULT_SEND_OK     (3 << 0)   // data has been send
#define RED_STACK_TRANSCEIVE_RESULT_READ_ERROR  (1 << 3)   // data has not been received because of an problem (wrong checksum or similar)
#define RED_STACK_TRANSCEIVE_RESULT_READ_NONE   (2 << 3)   // data has not been received because slave had none
#define RED_STACK_TRANSCEIVE_RESULT_READ_OK     (3 << 3)   // data has been received

#define RED_STACK_TRANSCEIVE_RESULT_MASK_SEND   0x7
#define RED_STACK_TRANSCEIVE_RESULT_MASK_READ   0x38

#define RED_STACK_RESET_PIN_GPIO_NUM            16           // defined in fex file
#define RED_STACK_RESET_PIN_GPIO_NAME           "gpio16_pb5" // defined in fex file

static char packet_signature[PACKET_MAX_SIGNATURE_LENGTH];

static bool _red_stack_spi_thread_running = false;
static int _red_stack_spi_fd = -1;

static Thread _red_stack_spi_thread;
static Semaphore _red_stack_dispatch_packet_from_spi_semaphore;

// We use a proper condition variable with mutex and helper variable (as is suggested by kernel documentation)
// to synchronize after a reset. If someone else needs this we may want to add
// the mechanism to daemonlibs thread implementation.
static pthread_cond_t   _red_stack_wait_for_reset_cond   = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t  _red_stack_wait_for_reset_mutex  = PTHREAD_MUTEX_INITIALIZER;
static int              _red_stack_wait_for_reset_helper = 0;

static int _red_stack_notification_event;
static int _red_stack_reset_fd;
static int _red_stack_reset_detected = 0;

// delay between transfers in microseconds. configurable with brickd.conf option poll_delay.spi
static int _red_stack_spi_poll_delay = 50;

typedef enum {
	RED_STACK_SLAVE_STATUS_ABSENT = 0,
	RED_STACK_SLAVE_STATUS_AVAILABLE,
} REDStackSlaveStatus;

typedef enum {
	RED_STACK_PACKET_STATUS_ADDED = 0,
	RED_STACK_PACKET_STATUS_SEQUENCE_NUMBER_SET
} REDStackPacketStatus;

typedef struct {
	uint8_t stack_address;
	uint8_t sequence_number_master;
	uint8_t sequence_number_slave;
	REDStackSlaveStatus status;
	GPIOPin slave_select_pin;
	Queue packet_to_spi_queue;
	Mutex packet_queue_mutex;
} REDStackSlave;

typedef struct {
	Stack base;
	REDStackSlave slaves[RED_STACK_SPI_MAX_SLAVES];
	uint8_t slave_num;

	Packet packet_from_spi;
} REDStack;

typedef struct {
	REDStackSlave *slave;
	Packet packet;
	REDStackPacketStatus status;
} REDStackPacket;

static REDStack _red_stack;

static const GPIOPin _red_stack_reset_stack_pin = {GPIO_PORT_B, GPIO_PIN_5};
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

#define SLEEP_NS(s, ns) do{ \
	struct timespec t; \
	t.tv_sec = (s); \
	t.tv_nsec = (ns); \
	clock_nanosleep(CLOCK_MONOTONIC, 0, &t, NULL); \
}while(0)

#define PRINT_TIME(str) do { \
	struct timespec t; \
	clock_gettime(CLOCK_MONOTONIC, &t); \
	printf(str ": %lds %ldms %ldus %ldns\n", t.tv_sec, t.tv_nsec/(1000*1000), t.tv_nsec/1000, t.tv_nsec); \
}while(0)


// * Packet structure:
//  * Byte 0: Preamble = 0xAA
//  * Byte 1: Length = n+2
//  * Byte 2 to n: Payload
//  * Byte n+1: Info (slave sequence, master sequence)
//   * Bit 0-2: Master sequence number (MSN)
//   * Bit 3-5: Slave sequence number (SSN)
//   * Bit 6-7: Currently unused
//  * Byte n+2: Checksum over bytes 0 to n+1


// ----- RED STACK SPI ------
// These functions run in SPI thread


static void red_stack_increase_master_sequence_number(REDStackSlave *slave) {
	slave->sequence_number_master += 1;
	if(slave->sequence_number_master > RED_STACK_SPI_INFO_SEQUENCE_MASTER_MASK) {
		slave->sequence_number_master = 0;
	}
}

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
static int red_stack_spi_transceive_message(REDStackPacket *packet_send, Packet *packet_recv, REDStackSlave *slave) {
	int retval = 0;
	uint8_t length, length_send;
	uint8_t checksum;
	int rc;
	uint8_t sequence_number_master = 0xFF;
	uint8_t sequence_number_slave = 0xFF;

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
        retval = RED_STACK_TRANSCEIVE_RESULT_SEND_NONE;
    } else if(slave->status == RED_STACK_SLAVE_STATUS_AVAILABLE) {
    	length = packet_send->packet.header.length;
    	if(length > sizeof(Packet)) {
    		retval |= RED_STACK_TRANSCEIVE_RESULT_SEND_ERROR;
			log_error("Send length is greater then allowed (actual: %d > maximum: %d)",
			          length, (int)sizeof(Packet));
    		goto ret;
    	}

    	retval = RED_STACK_TRANSCEIVE_DATA_SEND;

        tx[RED_STACK_SPI_LENGTH] = length + RED_STACK_SPI_PACKET_EMPTY_SIZE;
    	memcpy(tx+2, &packet_send->packet, length);
    } else {
    	retval = RED_STACK_TRANSCEIVE_RESULT_SEND_ERROR;
    	log_error("Slave with stack address %d is not present in stack", slave->stack_address);
    	goto ret;
    }

    length = tx[RED_STACK_SPI_LENGTH];

    // Set master and slave sequence number
	tx[RED_STACK_SPI_INFO(length)] = slave->sequence_number_master | slave->sequence_number_slave;

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
		retval = (retval & (~RED_STACK_TRANSCEIVE_RESULT_MASK_READ)) | RED_STACK_TRANSCEIVE_RESULT_READ_ERROR;
		goto ret;
	}

	// Check length
	length = rx[RED_STACK_SPI_LENGTH];

	if((length != RED_STACK_SPI_PACKET_EMPTY_SIZE) &&
	   ((length < (RED_STACK_SPI_PACKET_EMPTY_SIZE + sizeof(PacketHeader))) ||
	    (length > RED_STACK_SPI_PACKET_SIZE))) {
		log_error("Received packet with malformed length: %d", length);
		retval = (retval & (~RED_STACK_TRANSCEIVE_RESULT_MASK_READ)) | RED_STACK_TRANSCEIVE_RESULT_READ_ERROR;
		goto ret;
	}

	// Calculate and check checksum
	checksum = red_stack_spi_calculate_pearson_hash(rx, length-1);
	if(checksum != rx[RED_STACK_SPI_CHECKSUM(length)]) {
		log_error("Received packet with wrong checksum (actual: %x != expected: %x)",
		          checksum, rx[RED_STACK_SPI_CHECKSUM(length)]);
		retval = (retval & (~RED_STACK_TRANSCEIVE_RESULT_MASK_READ)) | RED_STACK_TRANSCEIVE_RESULT_READ_ERROR;
		goto ret;
	}

	// If we send data and the master sequence number matches to the one
	// set in the packet we know that the slave received the packet!
	if((packet_send != NULL) /*&& (packet_send->status == RED_STACK_PACKET_STATUS_SEQUENCE_NUMBER_SET)*/) {
		sequence_number_master = rx[RED_STACK_SPI_INFO(length)] & RED_STACK_SPI_INFO_SEQUENCE_MASTER_MASK;
		if(sequence_number_master == slave->sequence_number_master) {
			retval = (retval & (~RED_STACK_TRANSCEIVE_RESULT_MASK_SEND)) | RED_STACK_TRANSCEIVE_RESULT_SEND_OK;

			// Increase sequence number for next packet
			red_stack_increase_master_sequence_number(slave);
		}
	} else {
		// If we didn't send anything we can always increase the sequence number,
		// it doesn't matter if the slave actually received it.
		red_stack_increase_master_sequence_number(slave);
	}


	// If the slave sequence number matches we already processed this packet
	sequence_number_slave = rx[RED_STACK_SPI_INFO(length)] & RED_STACK_SPI_INFO_SEQUENCE_SLAVE_MASK;
	if(sequence_number_slave == slave->sequence_number_slave) {
		retval = (retval & (~RED_STACK_TRANSCEIVE_RESULT_MASK_READ)) | RED_STACK_TRANSCEIVE_RESULT_READ_NONE;
	} else {
		// Otherwise we save the new sequence number
		slave->sequence_number_slave = sequence_number_slave;
		if(length == RED_STACK_SPI_PACKET_EMPTY_SIZE) {
			// Do not log by default, will produce 2000 log entries per second
			// log_debug("Received empty packet over SPI (w/ header)");
			retval = (retval & (~RED_STACK_TRANSCEIVE_RESULT_MASK_READ)) | RED_STACK_TRANSCEIVE_RESULT_READ_NONE;
		} else {
			// Everything seems OK, we can copy to buffer
			memcpy(packet_recv, rx+2, length - RED_STACK_SPI_PACKET_EMPTY_SIZE);
			log_debug("Received packet over SPI (%s)",
					  packet_get_response_signature(packet_signature, packet_recv));
			retval = (retval & (~RED_STACK_TRANSCEIVE_RESULT_MASK_READ)) | RED_STACK_TRANSCEIVE_RESULT_READ_OK;
			retval |= RED_STACK_TRANSCEIVE_DATA_RECEIVED;
		}
	}

ret:
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

    	REDStackPacket red_stack_packet = {
    		slave,
        	{{
        		0,   // UID 0
        		sizeof(StackEnumerateRequest),
        		FUNCTION_STACK_ENUMERATE,
        		0x08, // Return expected
        		0
        	}, {0}, {0}},
        	RED_STACK_PACKET_STATUS_ADDED,
    	};

    	// We have to assume that the slave is available
    	slave->status = RED_STACK_SLAVE_STATUS_AVAILABLE;

    	// Send stack enumerate request
    	for(tries = 0; tries < RED_STACK_SPI_ROUTING_TRIES; tries++) {
    		ret = red_stack_spi_transceive_message(&red_stack_packet, &packet, slave);

    		if((ret & RED_STACK_TRANSCEIVE_RESULT_MASK_SEND) == RED_STACK_TRANSCEIVE_RESULT_SEND_OK) {
    			break;
    		}
    		SLEEP_NS(0, RED_STACK_SPI_ROUTING_WAIT); // Give slave some more time
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
    		// We first check if we already received an answer before we try again
    		if((ret & RED_STACK_TRANSCEIVE_RESULT_MASK_READ) == RED_STACK_TRANSCEIVE_RESULT_READ_OK) {
    			break;
    		}

    		// Here we sleep before transceive so that there is some time
    		// between the sending of stack enumerate and the receiving
    		// of the answer
    		SLEEP_NS(0, RED_STACK_SPI_ROUTING_WAIT); // Give slave some more time

    		ret = red_stack_spi_transceive_message(NULL, &packet, slave);
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

	log_info("SPI stack slave discovery done. Found %d slave(s) with %d UID(s) in total",
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

static void red_stack_spi_handle_reset(void) {
	int slave;

	stack_announce_disconnect(&_red_stack.base);
	_red_stack.base.recipients.count = 0;

	log_info("Starting reinitialization of SPI slaves");

	// Someone pressed reset we have to wait until he stops pressing
	while(gpio_input(_red_stack_reset_stack_pin) == 0) {
		// Wait 100us and check again. We wait as long as the user presses the button
		SLEEP_NS(0, 1000*100);
	}
	SLEEP_NS(1, 1000*1000*500); // Wait 1.5s so slaves can start properly

	// Reinitialize slaves
	_red_stack.slave_num = 0;
	for(slave = 0; slave < RED_STACK_SPI_MAX_SLAVES; slave++) {
		_red_stack.slaves[slave].stack_address = slave;
		_red_stack.slaves[slave].status = RED_STACK_SLAVE_STATUS_ABSENT;
		_red_stack.slaves[slave].sequence_number_master = 1;
		_red_stack.slaves[slave].sequence_number_slave = 0;

		// Unfortunately we have to discard all of the queued packets.
		// we can't be sure that the packets are for the correct slave after a reset.
		while(queue_peek(&_red_stack.slaves[slave].packet_to_spi_queue) != NULL) {
			queue_pop(&_red_stack.slaves[slave].packet_to_spi_queue, NULL);
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
	uint8_t stack_address_cycle;
	int ret;

	(void)opaque;

	do {
		stack_address_cycle = 0;
		_red_stack_reset_detected = 0;
		_red_stack.slave_num = 0;
		red_stack_spi_create_routing_table();

		_red_stack_spi_thread_running = false;
		if(_red_stack.slave_num > 0) {
			_red_stack_spi_thread_running = true;
		}

		// Ignore resets that we received in the meantime to prevent race conditions.
		_red_stack_reset_detected = 0;
		while(_red_stack_spi_thread_running) {
			REDStackSlave *slave = &_red_stack.slaves[stack_address_cycle];
			REDStackPacket *request = NULL;
			memset(&_red_stack.packet_from_spi, 0, sizeof(Packet));

			// Get packet from queue. The queue contains that are to be
			// send over SPI. It is filled through from the main brickd
			// event thread, so we have to make sure that there is not race
			// condition.
			mutex_lock(&(slave->packet_queue_mutex));
			packet_to_spi = queue_peek(&slave->packet_to_spi_queue);
			mutex_unlock(&(slave->packet_queue_mutex));

			stack_address_cycle++;
			if(stack_address_cycle >= _red_stack.slave_num) {
				stack_address_cycle = 0;
			}

			// Set request if we have a packet to send
			if(packet_to_spi != NULL) {
				log_debug("Packet will now be send over SPI (%s)",
						  packet_get_request_signature(packet_signature, &packet_to_spi->packet));

				request = packet_to_spi;
			}

			ret = red_stack_spi_transceive_message(request,
												   &_red_stack.packet_from_spi,
												   slave);

			if((ret & RED_STACK_TRANSCEIVE_RESULT_MASK_SEND) == RED_STACK_TRANSCEIVE_RESULT_SEND_OK) {
				if((!((ret & RED_STACK_TRANSCEIVE_RESULT_MASK_READ) == RED_STACK_TRANSCEIVE_RESULT_READ_ERROR))) {
					// If we send a packet it must have come from the queue, so we can
					// pop it from the queue now.
					// If the sending didn't work (for whatever reason), we don't pop it
					// and therefore we will automatically try to send it again in the next cycle.
					mutex_lock(&(slave->packet_queue_mutex));
					queue_pop(&slave->packet_to_spi_queue, NULL);
					mutex_unlock(&(slave->packet_queue_mutex));
				}
			}

			// If we received a packet, we will dispatch it immediately.
			// We have some time until we try the next SPI communication anyway.
			if((ret & RED_STACK_TRANSCEIVE_RESULT_MASK_READ) == RED_STACK_TRANSCEIVE_RESULT_READ_OK) {
				// TODO: Check again if packet is valid?
				// We did already check the hash.

				// Before the dispatching we insert the stack position into an enumerate message
				red_stack_spi_insert_position(slave);

				red_stack_spi_request_dispatch_response_event();
				// Wait until message is dispatched, so we don't overwrite it
				// accidentally.
				semaphore_acquire(&_red_stack_dispatch_packet_from_spi_semaphore);
			}

			SLEEP_NS(0, 1000*_red_stack_spi_poll_delay);
		}

		if(_red_stack.slave_num == 0) {
			pthread_mutex_lock(&_red_stack_wait_for_reset_mutex);
			// Use helper to be save against spurious wakeups
			_red_stack_wait_for_reset_helper = 0;
			while(_red_stack_wait_for_reset_helper == 0) {
				pthread_cond_wait(&_red_stack_wait_for_reset_cond, &_red_stack_wait_for_reset_mutex);
			}
			pthread_mutex_unlock(&_red_stack_wait_for_reset_mutex);
		}

		if(_red_stack_reset_detected > 0) {
			red_stack_spi_handle_reset();
		}
	} while(_red_stack_reset_detected > 0);
}


// ----- RED STACK -----
// These functions run in brickd main thread

// Resets stack
static void red_stack_reset(void) {
	// Change mux of reset pin to output
	gpio_mux_configure(_red_stack_reset_stack_pin, GPIO_MUX_OUTPUT);

	gpio_output_clear(_red_stack_reset_stack_pin);
	SLEEP_NS(0, 1000*1000*100); // Clear reset pin for 100ms to force reset
	gpio_output_set(_red_stack_reset_stack_pin);
	SLEEP_NS(1, 1000*1000*500); // Wait 1.5s so slaves can start properly

	// Change mux back to interrupt, so we can see if a human presses reset
	gpio_mux_configure(_red_stack_reset_stack_pin, GPIO_MUX_6);
}

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

	// Initialize slaves
	for(slave = 0; slave < RED_STACK_SPI_MAX_SLAVES; slave++) {
		_red_stack.slaves[slave].stack_address = slave;
		_red_stack.slaves[slave].status = RED_STACK_SLAVE_STATUS_ABSENT;
		_red_stack.slaves[slave].slave_select_pin = _red_stack_slave_select_pins[slave];
		_red_stack.slaves[slave].sequence_number_master = 1;
		_red_stack.slaves[slave].sequence_number_slave = 0;

		// Bring slave in initial state (deselected)
		gpio_mux_configure(_red_stack.slaves[slave].slave_select_pin, GPIO_MUX_OUTPUT);
		red_stack_spi_deselect(&_red_stack.slaves[slave]);
	}

	// Reset slaves and wait for slaves to be ready
	red_stack_reset();

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
static int red_stack_dispatch_to_spi(Stack *stack, Packet *request, Recipient *recipient) {
	REDStackPacket *queued_request;
	(void)stack;

	if(request->header.uid == 0) {
		// UID = 0 -> Broadcast to all UIDs
		uint8_t is;
		for(is = 0; is < _red_stack.slave_num; is++) {
			mutex_lock(&_red_stack.slaves[is].packet_queue_mutex);
			queued_request = queue_push(&_red_stack.slaves[is].packet_to_spi_queue);
			queued_request->status = RED_STACK_PACKET_STATUS_ADDED;
			queued_request->slave = &_red_stack.slaves[is];
			memcpy(&queued_request->packet, request, request->header.length);
			mutex_unlock(&_red_stack.slaves[is].packet_queue_mutex);

			log_debug("Request is queued to be broadcast to slave %d (%s)",
			          is,
			          packet_get_request_signature(packet_signature, request));
		}
	} else if (recipient != NULL) {
		// Get slave for recipient opaque (== stack_address)
		REDStackSlave *slave = &_red_stack.slaves[recipient->opaque];

		mutex_lock(&(slave->packet_queue_mutex));
		queued_request = queue_push(&(slave->packet_to_spi_queue));
		queued_request->status = RED_STACK_PACKET_STATUS_ADDED;
		queued_request->slave = slave;
		memcpy(&queued_request->packet, request, request->header.length);
		mutex_unlock(&(slave->packet_queue_mutex));

		log_debug("Packet is queued to be send to slave %d over SPI (%s)",
		          slave->stack_address,
		          packet_get_request_signature(packet_signature, request));
	}

	return 0;
}

static void red_stack_reset_handler(void *opaque) {
	char buf[2];

	(void)opaque;

	// Seek and read from gpio fd (see https://www.kernel.org/doc/Documentation/gpio/sysfs.txt)
	lseek(_red_stack_reset_fd, 0, SEEK_SET);
	if (read(_red_stack_reset_fd, buf, 2) < 0) {} // ignore return value

	_red_stack_reset_detected++;
	log_debug("Reset button press detected (%d since last reset)", _red_stack_reset_detected);

	_red_stack_spi_thread_running = false;

	// If there is no slave we have to wake up the spi thread
	if(_red_stack.slave_num == 0) {
		pthread_mutex_lock(&_red_stack_wait_for_reset_mutex);
		_red_stack_wait_for_reset_helper = 1;
		pthread_cond_signal(&_red_stack_wait_for_reset_cond);
		pthread_mutex_unlock(&_red_stack_wait_for_reset_mutex);
	}
}

int red_stack_init(void) {
	int i = 0;
	int phase = 0;

	log_debug("Initializing RED Brick SPI Stack subsystem");

	_red_stack_spi_poll_delay = config_get_option_value("poll_delay.spi")->integer;

	if(gpio_sysfs_export(RED_STACK_RESET_PIN_GPIO_NUM) < 0) {
		// Just issue a warning, RED Brick will work without reset interrupt
		log_warn("Could not export GPIO %d in sysfs, disabling reset interrupt",
		         RED_STACK_RESET_PIN_GPIO_NUM);
	} else {
		if((_red_stack_reset_fd = gpio_sysfs_get_value_fd(RED_STACK_RESET_PIN_GPIO_NAME)) < 0) {
			// Just issue a warning, RED Brick will work without reset interrupt
			log_warn("Could not retrieve fd for GPIO %s in sysfs, disabling reset interrupt",
			         RED_STACK_RESET_PIN_GPIO_NAME);
		} else {
			// If everything worked we can set the interrupt to falling.
			// We ignore the return value here, it may work despite error.
			gpio_sysfs_set_edge(RED_STACK_RESET_PIN_GPIO_NAME, "falling");
		}
	}

	// create base stack
	if(stack_create(&_red_stack.base, "red_stack", red_stack_dispatch_to_spi) < 0) {
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

	// Initialize SPI packet queues
	for(i = 0; i < RED_STACK_SPI_MAX_SLAVES; i++) {
		if(queue_create(&_red_stack.slaves[i].packet_to_spi_queue, sizeof(REDStackPacket)) < 0) {
			log_error("Could not create SPI queue %d: %s (%d)",
					  i, get_errno_name(errno), errno);
			goto cleanup;
		}
	}

	if(semaphore_create(&_red_stack_dispatch_packet_from_spi_semaphore) < 0) {
		log_error("Could not create SPI request semaphore: %s (%d)",
		          get_errno_name(errno), errno);
		goto cleanup;
	}

	for(i = 0; i < RED_STACK_SPI_MAX_SLAVES; i++) {
		mutex_create(&_red_stack.slaves[i].packet_queue_mutex);
	}

	phase = 5;

	if(red_stack_init_spi() < 0) {
		goto cleanup;
	}

    // Add reset interrupt as event source
	if(_red_stack_reset_fd > 0) {
		char buf[2];
		lseek(_red_stack_reset_fd, 0, SEEK_SET);
		if (read(_red_stack_reset_fd, buf, 2) < 0) {} // ignore return value

		if(event_add_source(_red_stack_reset_fd, EVENT_SOURCE_TYPE_GENERIC,
		                    EVENT_PRIO | EVENT_ERROR, red_stack_reset_handler, NULL) < 0) {
			log_error("Could not add reset fd event");
			goto cleanup;
		}
	}

	phase = 6;

cleanup:
	switch (phase) { // no breaks, all cases fall through intentionally
	case 5:
		for(i = 0; i < RED_STACK_SPI_MAX_SLAVES; i++) {
			mutex_destroy(&_red_stack.slaves[i].packet_queue_mutex);
		}
		semaphore_destroy(&_red_stack_dispatch_packet_from_spi_semaphore);

	case 4:
		for(i--; i >= 0; i--) {
			queue_destroy(&_red_stack.slaves[i].packet_to_spi_queue, NULL);
		}

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

	return phase == 6 ? 0 : -1;
}

void red_stack_exit(void) {
	int i;
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
	for(i = 0; i < RED_STACK_SPI_MAX_SLAVES; i++) {
		queue_destroy(&_red_stack.slaves[i].packet_to_spi_queue, NULL);
	}
	hardware_remove_stack(&_red_stack.base);
	stack_destroy(&_red_stack.base);

	for(i = 0; i < RED_STACK_SPI_MAX_SLAVES; i++) {
		mutex_destroy(&_red_stack.slaves[i].packet_queue_mutex);
	}
	semaphore_destroy(&_red_stack_dispatch_packet_from_spi_semaphore);

	// Close file descriptors
	close(_red_stack_notification_event);
	close(_red_stack_spi_fd);
}
