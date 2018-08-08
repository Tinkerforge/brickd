/*
 * brickd
 * Copyright (C) 2014 Olaf Lüke <olaf@tinkerforge.com>
 * Copyright (C) 2014-2016 Matthias Bolte <matthias@tinkerforge.com>
 * Copyright (C) 2017 Ishraq Ibne Ashraf <ishraq@tinkerforge.com>
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
#include <daemonlib/gpio_sysfs.h>
#include <daemonlib/io.h>
#include <daemonlib/log.h>
#include <daemonlib/packet.h>
#include <daemonlib/pearson_hash.h>
#include <daemonlib/pipe.h>
#include <daemonlib/gpio_red.h>
#include <daemonlib/threads.h>

#include "red_stack.h"

#include "hardware.h"
#include "network.h"
#include "red_usb_gadget.h"
#include "stack.h"

static LogSource _log_source = LOG_SOURCE_INITIALIZER;

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

#if BRICKD_WITH_RED_BRICK == 9

static GPIOSYSFS red_stack_reset_pin = {
	.name = "gpio16_pb5",
	.num  = 16
};

#else

// ((PORT_ALPHABET_INDEX - 1) * 32) + PIN_NR
// Example: For PB5, ((2 - 1) * 32) + 5 = 37
static GPIOSYSFS red_stack_reset_pin = {
	.name = "gpio37",
	.num  = 37
};

#endif

static char packet_signature[PACKET_MAX_SIGNATURE_LENGTH];

static bool _red_stack_spi_thread_running = false;
static int _red_stack_spi_fd = -1;

static Thread _red_stack_spi_thread;

// We use a proper condition variable with mutex and helper variable (as is suggested
// by kernel documentation) to synchronize after a reset. If someone else needs
// this we may want to add the mechanism to daemonlibs thread implementation.
static pthread_cond_t _red_stack_wait_for_reset_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t _red_stack_wait_for_reset_mutex = PTHREAD_MUTEX_INITIALIZER;
static int _red_stack_wait_for_reset_helper = 0;

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
	RED_STACK_REQUEST_STATUS_ADDED = 0,
	RED_STACK_REQUEST_STATUS_SEQUENCE_NUMBER_SET
} REDStackRequestStatus;

typedef struct {
	uint8_t stack_address;
	uint8_t sequence_number_master;
	uint8_t sequence_number_slave;
	REDStackSlaveStatus status;
	GPIOREDPin slave_select_pin;
	Queue request_queue;
	Mutex request_queue_mutex;
	bool next_packet_empty;
} REDStackSlave;

typedef struct {
	Stack base;
	REDStackSlave slaves[RED_STACK_SPI_MAX_SLAVES];
	uint8_t slave_num;

	Queue response_queue;
	Mutex response_queue_mutex;
} REDStack;

typedef struct {
	REDStackSlave *slave;
	Packet packet;
	REDStackRequestStatus status;
} REDStackRequest;

typedef struct {
	Packet packet;
	uint8_t stack_address;
} REDStackResponse;

static REDStack _red_stack;

static const GPIOREDPin _red_stack_reset_stack_pin = {GPIO_RED_PORT_B, GPIO_RED_PIN_5};
static const GPIOREDPin _red_stack_master_high_pin = {GPIO_RED_PORT_B, GPIO_RED_PIN_11};
static const GPIOREDPin _red_stack_slave_select_pins[RED_STACK_SPI_MAX_SLAVES] = {
	{GPIO_RED_PORT_C, GPIO_RED_PIN_8},
	{GPIO_RED_PORT_C, GPIO_RED_PIN_9},
	{GPIO_RED_PORT_C, GPIO_RED_PIN_10},
	{GPIO_RED_PORT_C, GPIO_RED_PIN_11},
	{GPIO_RED_PORT_C, GPIO_RED_PIN_12},
	{GPIO_RED_PORT_C, GPIO_RED_PIN_13},
	{GPIO_RED_PORT_C, GPIO_RED_PIN_14},
	{GPIO_RED_PORT_C, GPIO_RED_PIN_15}
};

static const char *_red_stack_spi_device = "/dev/spidev0.0";

#define SLEEP_NS(s, ns) do { \
	struct timespec t; \
	t.tv_sec = (s); \
	t.tv_nsec = (ns); \
	clock_nanosleep(CLOCK_MONOTONIC, 0, &t, NULL); \
} while(0)

#define PRINT_TIME(str) do { \
	struct timespec t; \
	clock_gettime(CLOCK_MONOTONIC, &t); \
	printf(str ": %lds %ldms %ldus %ldns\n", t.tv_sec, t.tv_nsec/(1000*1000), t.tv_nsec/1000, t.tv_nsec); \
} while(0)


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

	if (slave->sequence_number_master > RED_STACK_SPI_INFO_SEQUENCE_MASTER_MASK) {
		slave->sequence_number_master = 0;
	}
}

// Get "red_stack_dispatch_from_spi" called from main brickd event thread
static int red_stack_spi_request_dispatch_response_event(REDStackResponse *response) {
	REDStackResponse *queued_response;
	eventfd_t ev = 1;

	mutex_lock(&_red_stack.response_queue_mutex);
	queued_response = queue_push(&_red_stack.response_queue);
	memcpy(queued_response, response, sizeof(REDStackResponse));
	mutex_unlock(&_red_stack.response_queue_mutex);

	if (eventfd_write(_red_stack_notification_event, ev) < 0) {
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

	for (i = 0; i < length; i++) {
		PEARSON(checksum, data[i]);
	}

	return checksum;
}

static void red_stack_spi_select(REDStackSlave *slave) {
	gpio_red_output_clear(slave->slave_select_pin);
}

static void red_stack_spi_deselect(REDStackSlave *slave) {
	gpio_red_output_set(slave->slave_select_pin);
}

// If data should just be polled, set packet_send to NULL.
//
// If no packet is received from slave the length in packet_recv will be set to 0,
// the exact reason for that is encoded in the return value.
//
// For the return value see RED_STACK_TRANSCEIVE_RESULT_* at the top of this file.
static int red_stack_spi_transceive_message(REDStackRequest *packet_send, REDStackResponse *packet_recv, REDStackSlave *slave) {
	int retval = 0;
	uint8_t length, length_send;
	uint8_t checksum;
	int rc;
	uint8_t sequence_number_master = 0xFF;
	uint8_t sequence_number_slave = 0xFF;
	uint8_t tx[RED_STACK_SPI_PACKET_SIZE] = {0};
	uint8_t rx[RED_STACK_SPI_PACKET_SIZE] = {0};

	packet_add_trace(&packet_send->packet);

	// We assume that we don't receive anything. If we receive a packet the
	// length will be overwritten again
	packet_recv->packet.header.length = 0;

	// Set stack address for packet
	packet_recv->stack_address = slave->stack_address;

	// Preamble is always the same
	tx[RED_STACK_SPI_PREAMBLE] = RED_STACK_SPI_PREAMBLE_VALUE;

	if (packet_send == NULL) {
		// If packet_send is NULL
		// we send a message with empty payload (4 byte)
		tx[RED_STACK_SPI_LENGTH] = RED_STACK_SPI_PACKET_EMPTY_SIZE;
		retval = RED_STACK_TRANSCEIVE_RESULT_SEND_NONE;
	} else if (slave->status == RED_STACK_SLAVE_STATUS_AVAILABLE) {
		length = packet_send->packet.header.length;

		if (length > sizeof(Packet)) {
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

		if(packet_send == NULL) {
			slave->next_packet_empty = true;
		}

		log_error("ioctl failed: %s (%d)", get_errno_name(errno), errno);

		goto ret;
	}

	length_send = rc;

	if (length_send != RED_STACK_SPI_PACKET_SIZE) {
		// Overwrite current return status with error,
		// it seems ioctl itself didn't work.
		retval = RED_STACK_TRANSCEIVE_RESULT_SEND_ERROR | RED_STACK_TRANSCEIVE_RESULT_READ_ERROR;

		if(packet_send == NULL) {
			slave->next_packet_empty = true;
		}

		log_error("ioctl has unexpected result (actual: %d != expected: %d)",
		          length_send, RED_STACK_SPI_PACKET_SIZE);

		goto ret;
	}

	if (rx[RED_STACK_SPI_PREAMBLE] != RED_STACK_SPI_PREAMBLE_VALUE) {
		// Do not log by default, an "unproper preamble" is part of the protocol
		// if the slave is too busy to fill the DMA buffers fast enough
		// log_error("Received packet without proper preamble (actual: %d != expected: %d)",
		//          rx[RED_STACK_SPI_PREAMBLE], RED_STACK_SPI_PREAMBLE_VALUE);
		retval = (retval & (~RED_STACK_TRANSCEIVE_RESULT_MASK_READ)) | RED_STACK_TRANSCEIVE_RESULT_READ_ERROR;

		if(packet_send == NULL) {
			slave->next_packet_empty = true;
		}

		goto ret;
	}

	// Check length
	length = rx[RED_STACK_SPI_LENGTH];

	if ((length != RED_STACK_SPI_PACKET_EMPTY_SIZE) &&
	    ((length < (RED_STACK_SPI_PACKET_EMPTY_SIZE + sizeof(PacketHeader))) ||
	     (length > RED_STACK_SPI_PACKET_SIZE))) {
		log_error("Received packet with malformed length: %d", length);
		retval = (retval & (~RED_STACK_TRANSCEIVE_RESULT_MASK_READ)) | RED_STACK_TRANSCEIVE_RESULT_READ_ERROR;

		if(packet_send == NULL) {
			slave->next_packet_empty = true;
		}

		goto ret;
	}

	// Calculate and check checksum
	checksum = red_stack_spi_calculate_pearson_hash(rx, length-1);

	if (checksum != rx[RED_STACK_SPI_CHECKSUM(length)]) {
		log_error("Received packet with wrong checksum (actual: %x != expected: %x)",
		          checksum, rx[RED_STACK_SPI_CHECKSUM(length)]);
		retval = (retval & (~RED_STACK_TRANSCEIVE_RESULT_MASK_READ)) | RED_STACK_TRANSCEIVE_RESULT_READ_ERROR;

		if(packet_send == NULL) {
			slave->next_packet_empty = true;
		}

		goto ret;
	}

	// If we send data and the master sequence number matches to the one
	// set in the packet we know that the slave received the packet!
	sequence_number_master = rx[RED_STACK_SPI_INFO(length)] & RED_STACK_SPI_INFO_SEQUENCE_MASTER_MASK;

	if (packet_send != NULL /*&& (packet_send->status == RED_STACK_REQUEST_STATUS_SEQUENCE_NUMBER_SET)*/) {
		if (sequence_number_master == slave->sequence_number_master) {
			retval = (retval & (~RED_STACK_TRANSCEIVE_RESULT_MASK_SEND)) | RED_STACK_TRANSCEIVE_RESULT_SEND_OK;

			// Increase sequence number for next packet
			red_stack_increase_master_sequence_number(slave);
		}
	} else {
		// If we didn't send anything we can increase the sequence number
		// if the increased sequence number does not match the last sequence number
		// that we ACKed. Otherwise we may get a false positive ACK for the next
		// message.

		uint8_t seq_inc = slave->sequence_number_master + 1 > RED_STACK_SPI_INFO_SEQUENCE_MASTER_MASK ? 0 : slave->sequence_number_master + 1;

		if (sequence_number_master == slave->sequence_number_master || seq_inc != sequence_number_master) {
			red_stack_increase_master_sequence_number(slave);
		} else {
			// Since we did't increase the sequence number, then ext packet must
			// be empty, otherwise we may get a ACK for the last empty packet and
			// interpret it as an ACK for a packet with a message
			slave->next_packet_empty = true;
		}
	}

	// If the slave sequence number matches we already processed this packet
	sequence_number_slave = rx[RED_STACK_SPI_INFO(length)] & RED_STACK_SPI_INFO_SEQUENCE_SLAVE_MASK;

	if (sequence_number_slave == slave->sequence_number_slave) {
		retval = (retval & (~RED_STACK_TRANSCEIVE_RESULT_MASK_READ)) | RED_STACK_TRANSCEIVE_RESULT_READ_NONE;
	} else {
		// Otherwise we save the new sequence number
		slave->sequence_number_slave = sequence_number_slave;

		if (length == RED_STACK_SPI_PACKET_EMPTY_SIZE) {
			// Do not log by default, will produce 2000 log entries per second
			// log_packet_debug("Received empty packet over SPI (w/ header)");
			retval = (retval & (~RED_STACK_TRANSCEIVE_RESULT_MASK_READ)) | RED_STACK_TRANSCEIVE_RESULT_READ_NONE;
		} else {
			// Everything seems OK, we can copy to buffer
			memcpy(&packet_recv->packet, rx+2, length - RED_STACK_SPI_PACKET_EMPTY_SIZE);

#ifdef DAEMONLIB_WITH_PACKET_TRACE
			packet_recv->packet.trace_id = packet_get_next_response_trace_id();
#endif

			packet_add_trace(&packet_recv->packet);

			log_packet_debug("Received packet over SPI (%s)",
			                 packet_get_response_signature(packet_signature, &packet_recv->packet));

			retval = (retval & (~RED_STACK_TRANSCEIVE_RESULT_MASK_READ)) | RED_STACK_TRANSCEIVE_RESULT_READ_OK;
			retval |= RED_STACK_TRANSCEIVE_DATA_RECEIVED;
		}
	}

ret:
	return retval;
}

// Creates the "routing table", which is just the
// array of REDStackSlave structures.
static void red_stack_spi_create_routing_table(void) {
	char base58[BASE58_MAX_LENGTH];
	int tries, ret, i;
	uint8_t stack_address = 0;
	uint8_t uid_counter = 0;

	log_debug("Starting to discover SPI stack slaves");

	while (stack_address < RED_STACK_SPI_MAX_SLAVES) {
		REDStackSlave *slave = &_red_stack.slaves[stack_address];
		REDStackResponse response;
		StackEnumerateResponse *enumerate_response;
		REDStackRequest request = {
			slave,
			{{
				0,   // UID 0
				sizeof(StackEnumerateRequest),
				FUNCTION_STACK_ENUMERATE,
				0x08, // Return expected
				0
			}, {0}, {0}},
			RED_STACK_REQUEST_STATUS_ADDED,
		};

		// We have to assume that the slave is available
		slave->status = RED_STACK_SLAVE_STATUS_AVAILABLE;

		// Send stack enumerate request
		for (tries = 0; tries < RED_STACK_SPI_ROUTING_TRIES; tries++) {
			ret = red_stack_spi_transceive_message(&request, &response, slave);

			if ((ret & RED_STACK_TRANSCEIVE_RESULT_MASK_SEND) == RED_STACK_TRANSCEIVE_RESULT_SEND_OK) {
				break;
			}

			SLEEP_NS(0, RED_STACK_SPI_ROUTING_WAIT); // Give slave some more time
		}

		if (tries == RED_STACK_SPI_ROUTING_TRIES) {
			// Slave does not seem to be available,
			slave->status = RED_STACK_SLAVE_STATUS_ABSENT;
			// This means that there can't be any more slaves above
			// and we are actually done here already!
			break;
		}

		// Receive stack enumerate response
		for (tries = 0; tries < RED_STACK_SPI_ROUTING_TRIES; tries++) {
			// We first check if we already received an answer before we try again
			if ((ret & RED_STACK_TRANSCEIVE_RESULT_MASK_READ) == RED_STACK_TRANSCEIVE_RESULT_READ_OK) {
				break;
			}

			// Here we sleep before transceive so that there is some time
			// between the sending of stack enumerate and the receiving
			// of the answer
			SLEEP_NS(0, RED_STACK_SPI_ROUTING_WAIT); // Give slave some more time

			ret = red_stack_spi_transceive_message(NULL, &response, slave);
		}

		if (tries == RED_STACK_SPI_ROUTING_TRIES) {
			// Slave does not seem to be available,
			slave->status = RED_STACK_SLAVE_STATUS_ABSENT;
			// This means that there can't be any more slaves above
			// and we are actually done here already!
			break;
		}

		enumerate_response = (StackEnumerateResponse *)&response.packet;

		for (i = 0; i < PACKET_MAX_STACK_ENUMERATE_UIDS; i++) {
			if (enumerate_response->uids[i] != 0) {
				uid_counter++;

				stack_add_recipient(&_red_stack.base, enumerate_response->uids[i], stack_address);

				log_debug("Found UID number %d of slave %d with UID %s",
				          i, stack_address,
				          base58_encode(base58, uint32_from_le(enumerate_response->uids[i])));
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

static void red_stack_spi_insert_position(REDStackResponse *response) {
	if (response->packet.header.function_id == CALLBACK_ENUMERATE ||
	    response->packet.header.function_id == FUNCTION_GET_IDENTITY) {
		EnumerateCallback *enum_cb = (EnumerateCallback *)&response->packet;

		if (enum_cb->position == '0') {
			enum_cb->position = '0' + response->stack_address + 1;
			base58_encode(enum_cb->connected_uid, uint32_from_le(red_usb_gadget_get_uid()));
		}
	}
}

static void red_stack_spi_handle_reset(void) {
	int slave;

	stack_announce_disconnect(&_red_stack.base);
	_red_stack.base.recipients.count = 0; // FIXME: properly clear the array

	log_info("Starting reinitialization of SPI slaves");

	// Someone pressed reset we have to wait until he stops pressing
	while (gpio_red_input(_red_stack_reset_stack_pin) == 0) {
		// Wait 100us and check again. We wait as long as the user presses the button
		SLEEP_NS(0, 1000*100);
	}

	SLEEP_NS(1, 1000*1000*500); // Wait 1.5s so slaves can start properly

	// Reinitialize slaves
	_red_stack.slave_num = 0;

	for (slave = 0; slave < RED_STACK_SPI_MAX_SLAVES; slave++) {
		_red_stack.slaves[slave].stack_address = slave;
		_red_stack.slaves[slave].status = RED_STACK_SLAVE_STATUS_ABSENT;
		_red_stack.slaves[slave].sequence_number_master = 1;
		_red_stack.slaves[slave].sequence_number_slave = 0;
		_red_stack.slaves[slave].next_packet_empty = false;

		// Unfortunately we have to discard all of the queued packets.
		// we can't be sure that the packets are for the correct slave after a reset.
		while (queue_peek(&_red_stack.slaves[slave].request_queue) != NULL) {
			queue_pop(&_red_stack.slaves[slave].request_queue, NULL);
		}
	}
}

// Main SPI loop. This runs independently from the brickd event thread.
// Data between RED Brick and SPI slave is exchanged every 500us.
// If there is no data to be send, we cycle through the slaves and request
// data. If there is data to be send the slave that ought to receive
// the data gets priority. This can greatly reduce latency in a big stack.
static void red_stack_spi_thread(void *opaque) {
	uint8_t stack_address_cycle;
	int ret;

	(void)opaque;

	do {
		stack_address_cycle = 0;
		_red_stack_reset_detected = 0;
		_red_stack.slave_num = 0;
		red_stack_spi_create_routing_table();

		_red_stack_spi_thread_running = false;

		if (_red_stack.slave_num > 0) {
			_red_stack_spi_thread_running = true;
		}

		// Ignore resets that we received in the meantime to prevent race conditions.
		_red_stack_reset_detected = 0;

		while (_red_stack_spi_thread_running) {
			REDStackSlave *slave = &_red_stack.slaves[stack_address_cycle];
			REDStackRequest *request = NULL;
			REDStackResponse response;

			// Get packet from queue. The queue contains request that are to
			// be send over SPI. It is filled through from the main brickd
			// event thread, so we have to make sure that there is not race
			// condition.
			if(slave->next_packet_empty) {
				slave->next_packet_empty = false;
				request = NULL;
			} else {
				mutex_lock(&slave->request_queue_mutex);
				request = queue_peek(&slave->request_queue);
				mutex_unlock(&slave->request_queue_mutex);
			}

			stack_address_cycle++;

			if (stack_address_cycle >= _red_stack.slave_num) {
				stack_address_cycle = 0;
			}

			// Set request if we have a packet to send
			if (request != NULL) {
				log_packet_debug("Packet will now be send over SPI (%s)",
				                 packet_get_request_signature(packet_signature, &request->packet));
			}

			ret = red_stack_spi_transceive_message(request, &response, slave);

			if ((ret & RED_STACK_TRANSCEIVE_RESULT_MASK_SEND) == RED_STACK_TRANSCEIVE_RESULT_SEND_OK) {
				if (!((ret & RED_STACK_TRANSCEIVE_RESULT_MASK_READ) == RED_STACK_TRANSCEIVE_RESULT_READ_ERROR)) {
					// If we send a packet it must have come from the queue, so we can
					// pop it from the queue now.
					// If the sending didn't work (for whatever reason), we don't pop it
					// and therefore we will automatically try to send it again in the next cycle.
					mutex_lock(&slave->request_queue_mutex);
					queue_pop(&slave->request_queue, NULL);
					mutex_unlock(&slave->request_queue_mutex);
				}
			}

			// If we received a packet, we will dispatch it immediately.
			// We have some time until we try the next SPI communication anyway.
			if ((ret & RED_STACK_TRANSCEIVE_RESULT_MASK_READ) == RED_STACK_TRANSCEIVE_RESULT_READ_OK) {
				// TODO: Check again if packet is valid?
				// We did already check the hash.

				// Before the dispatching we insert the stack position into an enumerate message
				red_stack_spi_insert_position(&response);

				red_stack_spi_request_dispatch_response_event(&response);
				// Wait until message is dispatched, so we don't overwrite it
				// accidentally.
				//semaphore_acquire(&_red_stack_dispatch_packet_from_spi_semaphore);
			}

			SLEEP_NS(0, 1000*_red_stack_spi_poll_delay);
		}

		if (_red_stack.slave_num == 0) {
			pthread_mutex_lock(&_red_stack_wait_for_reset_mutex);
			// Use helper to be save against spurious wakeups
			_red_stack_wait_for_reset_helper = 0;

			while (_red_stack_wait_for_reset_helper == 0) {
				pthread_cond_wait(&_red_stack_wait_for_reset_cond, &_red_stack_wait_for_reset_mutex);
			}

			pthread_mutex_unlock(&_red_stack_wait_for_reset_mutex);
		}

		if (_red_stack_reset_detected > 0) {
			red_stack_spi_handle_reset();
		}
	} while (_red_stack_reset_detected > 0);
}


// ----- RED STACK -----
// These functions run in brickd main thread

// Resets stack
static void red_stack_reset(void) {
	// Change mux of reset pin to output
	gpio_red_mux_configure(_red_stack_reset_stack_pin, GPIO_RED_MUX_OUTPUT);

	gpio_red_output_clear(_red_stack_reset_stack_pin);
	SLEEP_NS(0, 1000*1000*100); // Clear reset pin for 100ms to force reset
	gpio_red_output_set(_red_stack_reset_stack_pin);
	SLEEP_NS(1, 1000*1000*500); // Wait 1.5s so slaves can start properly

	// Change mux back to interrupt, so we can see if a human presses reset
	gpio_red_mux_configure(_red_stack_reset_stack_pin, GPIO_RED_MUX_6);
}

static int red_stack_init_spi(void) {
	uint8_t slave;
	const uint8_t mode = RED_STACK_SPI_CONFIG_MODE;
	const uint8_t lsb_first = RED_STACK_SPI_CONFIG_LSB_FIRST;
	const uint8_t bits_per_word = RED_STACK_SPI_CONFIG_BITS_PER_WORD;
	const uint32_t max_speed_hz = RED_STACK_SPI_CONFIG_MAX_SPEED_HZ;

	// Set Master High pin to low (so Master Bricks above RED Brick can
	// configure themselves as slave)
	gpio_red_mux_configure(_red_stack_master_high_pin, GPIO_RED_MUX_OUTPUT);
	gpio_red_output_clear(_red_stack_master_high_pin);

	// Initialize slaves
	for (slave = 0; slave < RED_STACK_SPI_MAX_SLAVES; slave++) {
		_red_stack.slaves[slave].stack_address = slave;
		_red_stack.slaves[slave].status = RED_STACK_SLAVE_STATUS_ABSENT;
		_red_stack.slaves[slave].slave_select_pin = _red_stack_slave_select_pins[slave];
		_red_stack.slaves[slave].sequence_number_master = 1;
		_red_stack.slaves[slave].sequence_number_slave = 0;

		// Bring slave in initial state (deselected)
		gpio_red_mux_configure(_red_stack.slaves[slave].slave_select_pin, GPIO_RED_MUX_OUTPUT);
		red_stack_spi_deselect(&_red_stack.slaves[slave]);
	}

	// Reset slaves and wait for slaves to be ready
	red_stack_reset();

	// Open spidev
	_red_stack_spi_fd = open(_red_stack_spi_device, O_RDWR);
	if (_red_stack_spi_fd < 0) {
		log_error("Could not open %s", _red_stack_spi_device);
		return -1;
	}

	if (ioctl(_red_stack_spi_fd, SPI_IOC_WR_MODE, &mode) < 0) {
		log_error("Could not configure SPI mode");
		return -1;
	}

	if (ioctl(_red_stack_spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &max_speed_hz) < 0) {
		log_error("Could not configure SPI max speed");
		return -1;
	}

	if (ioctl(_red_stack_spi_fd, SPI_IOC_WR_BITS_PER_WORD, &bits_per_word) < 0) {
		log_error("Could not configure SPI bits per word");
		return -1;
	}

	if (ioctl(_red_stack_spi_fd, SPI_IOC_WR_LSB_FIRST, &lsb_first) < 0) {
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
	int i;
	eventfd_t ev;
	REDStackResponse *response;

	(void)opaque;

	// handle at most 5 queued responses at once to avoid blocking the event
	// lopp for too long
	for (i = 0; i < 5; ++i) {
		if (eventfd_read(_red_stack_notification_event, &ev) < 0) {
			if (errno_would_block()) {
				return; // no queue responses left
			}

			log_error("Could not read from SPI notification event: %s (%d)",
			          get_errno_name(errno), errno);

			return;
		}

		mutex_lock(&_red_stack.response_queue_mutex);
		response = queue_peek(&_red_stack.response_queue);
		mutex_unlock(&_red_stack.response_queue_mutex);

		if (response == NULL) { // eventfd indicates a reponsed but queue is empty
			log_error("Response queue and notification event are out-of-sync");

			return;
		}

		// Update routing table (this is necessary for Co MCU Bricklets)
		if (response->packet.header.function_id == CALLBACK_ENUMERATE) {
			stack_add_recipient(&_red_stack.base, response->packet.header.uid, response->stack_address);
		}

		// Send message into brickd dispatcher
		network_dispatch_response(&response->packet);

		mutex_lock(&_red_stack.response_queue_mutex);
		queue_pop(&_red_stack.response_queue, NULL);
		mutex_unlock(&_red_stack.response_queue_mutex);
	}
}

// New packet from brickd event loop is queued to be written to stack via SPI
static int red_stack_dispatch_to_spi(Stack *stack, Packet *request, Recipient *recipient) {
	REDStackRequest *queued_request;

	(void)stack;

	if (request->header.uid == 0) {
		// UID = 0 -> Broadcast to all UIDs
		uint8_t is;

		for (is = 0; is < _red_stack.slave_num; is++) {
			mutex_lock(&_red_stack.slaves[is].request_queue_mutex);
			queued_request = queue_push(&_red_stack.slaves[is].request_queue);
			queued_request->status = RED_STACK_REQUEST_STATUS_ADDED;
			queued_request->slave = &_red_stack.slaves[is];
			memcpy(&queued_request->packet, request, request->header.length);
			mutex_unlock(&_red_stack.slaves[is].request_queue_mutex);

			log_packet_debug("Request is queued to be broadcast to slave %d (%s)",
			                 is, packet_get_request_signature(packet_signature, request));
		}
	} else if (recipient != NULL) {
		// Get slave for recipient opaque (== stack_address)
		REDStackSlave *slave = &_red_stack.slaves[recipient->opaque];

		mutex_lock(&slave->request_queue_mutex);
		queued_request = queue_push(&slave->request_queue);
		queued_request->status = RED_STACK_REQUEST_STATUS_ADDED;
		queued_request->slave = slave;
		memcpy(&queued_request->packet, request, request->header.length);
		mutex_unlock(&slave->request_queue_mutex);

		log_packet_debug("Packet is queued to be send to slave %d over SPI (%s)",
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
	if (robust_read(_red_stack_reset_fd, buf, 2) < 0) {} // ignore return value

	_red_stack_reset_detected++;
	log_debug("Reset button press detected (%d since last reset)", _red_stack_reset_detected);

	_red_stack_spi_thread_running = false;

	// If there is no slave we have to wake up the spi thread
	if (_red_stack.slave_num == 0) {
		pthread_mutex_lock(&_red_stack_wait_for_reset_mutex);
		_red_stack_wait_for_reset_helper = 1;
		pthread_cond_signal(&_red_stack_wait_for_reset_cond);
		pthread_mutex_unlock(&_red_stack_wait_for_reset_mutex);
	}
}

int red_stack_init(void) {
	int phase = 0;
	int i;
	int k;

	log_debug("Initializing RED Brick SPI Stack subsystem");

	_red_stack_spi_poll_delay = config_get_option_value("poll_delay.spi")->integer;

	if (gpio_sysfs_export(&red_stack_reset_pin) < 0) {
		// Just issue a warning, RED Brick will work without reset interrupt
		log_warn("Could not export GPIO_RED %d in sysfs, disabling reset interrupt",
		         red_stack_reset_pin.num);
	} else {
		if ((_red_stack_reset_fd = gpio_sysfs_get_input_fd(&red_stack_reset_pin)) < 0) {
			// Just issue a warning, RED Brick will work without reset interrupt
			log_warn("Could not retrieve fd for GPIO_RED %s in sysfs, disabling reset interrupt",
			         red_stack_reset_pin.name);
		} else {
			// If everything worked we can set the interrupt to falling.
			// We ignore the return value here, it may work despite error.
			gpio_sysfs_set_interrupt(&red_stack_reset_pin, GPIO_SYSFS_INTERRUPT_FALLING);
		}
	}

	// create base stack
	if (stack_create(&_red_stack.base, "red_stack", red_stack_dispatch_to_spi) < 0) {
		log_error("Could not create base stack for RED Brick SPI Stack: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 1;

	// add to stacks array
	if (hardware_add_stack(&_red_stack.base) < 0) {
		goto cleanup;
	}

	phase = 2;

	if ((_red_stack_notification_event = eventfd(0, EFD_NONBLOCK | EFD_SEMAPHORE)) < 0) {
		log_error("Could not create red stack notification event: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 3;

	// Add notification pipe as event source.
	// Event is used to dispatch packets.
	if (event_add_source(_red_stack_notification_event, EVENT_SOURCE_TYPE_GENERIC,
	                     "red-stack-notification", EVENT_READ,
	                     red_stack_dispatch_from_spi, NULL) < 0) {
		log_error("Could not add red stack notification pipe as event source");

		goto cleanup;
	}

	phase = 4;

	// Initialize SPI packet queues
	for (k = 0; k < RED_STACK_SPI_MAX_SLAVES; k++) {
		if (queue_create(&_red_stack.slaves[k].request_queue, sizeof(REDStackRequest)) < 0) {
			log_error("Could not create SPI request queue %d: %s (%d)",
			          k, get_errno_name(errno), errno);

			goto cleanup;
		}
	}

	phase = 5;

	for (i = 0; i < RED_STACK_SPI_MAX_SLAVES; i++) {
		mutex_create(&_red_stack.slaves[i].request_queue_mutex);
	}

	mutex_create(&_red_stack.response_queue_mutex);

	if (queue_create(&_red_stack.response_queue, sizeof(REDStackResponse)) < 0) {
		log_error("Could not create SPI response queue: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 6;

	if (red_stack_init_spi() < 0) {
		goto cleanup;
	}

	// Add reset interrupt as event source
	if (_red_stack_reset_fd > 0) {
		char buf[2];
		lseek(_red_stack_reset_fd, 0, SEEK_SET);
		if (robust_read(_red_stack_reset_fd, buf, 2) < 0) {} // ignore return value

		if (event_add_source(_red_stack_reset_fd, EVENT_SOURCE_TYPE_GENERIC,
		                     "red-stack-reset", EVENT_PRIO | EVENT_ERROR,
		                     red_stack_reset_handler, NULL) < 0) {
			log_error("Could not add reset fd event");

			goto cleanup;
		}
	}

	phase = 7;

cleanup:
	switch (phase) { // no breaks, all cases fall through intentionally
	case 6:
		queue_destroy(&_red_stack.response_queue, NULL);
		// fall through

	case 5:
		mutex_destroy(&_red_stack.response_queue_mutex);

		for (i = 0; i < RED_STACK_SPI_MAX_SLAVES; i++) {
			mutex_destroy(&_red_stack.slaves[i].request_queue_mutex);
		}

		// fall through

	case 4:
		for (k--; k >= 0; k--) {
			queue_destroy(&_red_stack.slaves[k].request_queue, NULL);
		}

		event_remove_source(_red_stack_notification_event, EVENT_SOURCE_TYPE_GENERIC);
		// fall through

	case 3:
		robust_close(_red_stack_notification_event);
		// fall through

	case 2:
		hardware_remove_stack(&_red_stack.base);
		// fall through

	case 1:
		stack_destroy(&_red_stack.base);
		// fall through

	default:
		break;
	}

	return phase == 7 ? 0 : -1;
}

void red_stack_exit(void) {
	int i;
	int slave;

	// Remove reset interrupt as event source
	if (_red_stack_reset_fd > 0) {
		event_remove_source(_red_stack_reset_fd, EVENT_SOURCE_TYPE_GENERIC);
	}

	// Remove event as possible poll source
	event_remove_source(_red_stack_notification_event, EVENT_SOURCE_TYPE_GENERIC);

	// Make sure that Thread shuts down properly
	if (_red_stack_spi_thread_running) {
		_red_stack_spi_thread_running = false;
		// Write in eventfd to make sure that we are not blocking the Thread
		eventfd_t ev = 1;
		eventfd_write(_red_stack_notification_event, ev);

		thread_join(&_red_stack_spi_thread);
		thread_destroy(&_red_stack_spi_thread);
	}

	// Thread is not running anymore, we make sure that all slaves are deselected
	for (slave = 0; slave < RED_STACK_SPI_MAX_SLAVES; slave++) {
		red_stack_spi_deselect(&_red_stack.slaves[slave]);
	}

	// We can also free the queue and stack now, nobody will use them anymore
	for (i = 0; i < RED_STACK_SPI_MAX_SLAVES; i++) {
		queue_destroy(&_red_stack.slaves[i].request_queue, NULL);
	}

	hardware_remove_stack(&_red_stack.base);
	stack_destroy(&_red_stack.base);

	for (i = 0; i < RED_STACK_SPI_MAX_SLAVES; i++) {
		mutex_destroy(&_red_stack.slaves[i].request_queue_mutex);
	}

	queue_destroy(&_red_stack.response_queue, NULL);
	mutex_destroy(&_red_stack.response_queue_mutex);

	// Close file descriptors
	robust_close(_red_stack_notification_event);
	robust_close(_red_stack_spi_fd);
}
