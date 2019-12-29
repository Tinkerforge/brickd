/*
 * brickd
 * Copyright (C) 2018 Olaf Lüke <olaf@tinkerforge.com>
 * Copyright (C) 2018-2019 Matthias Bolte <matthias@tinkerforge.com>
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

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#ifdef __linux__
	#include <sys/eventfd.h>
#endif

#include <daemonlib/base58.h>
#include <daemonlib/config.h>
#include <daemonlib/event.h>
#include <daemonlib/io.h>
#include <daemonlib/log.h>
#include <daemonlib/packet.h>
#include <daemonlib/pearson_hash.h>
#include <daemonlib/pipe.h>

#include "bricklet_stack.h"

#include "hardware.h"
#include "network.h"

typedef enum {
	SPITFP_STATE_START,
	SPITFP_STATE_ACK_SEQUENCE_NUMBER,
	SPITFP_STATE_ACK_CHECKSUM,
	SPITFP_STATE_MESSAGE_SEQUENCE_NUMBER,
	SPITFP_STATE_MESSAGE_DATA,
	SPITFP_STATE_MESSAGE_CHECKSUM
} SPITFPState;

static LogSource _log_source = LOG_SOURCE_INITIALIZER;

static char packet_signature[PACKET_MAX_SIGNATURE_LENGTH];

extern int bricklet_stack_create_platform(BrickletStack *bricklet_stack);
extern int bricklet_stack_destroy_platform(BrickletStack *bricklet_stack);
extern int bricklet_stack_chip_select_gpio(BrickletStack *bricklet_stack, bool enable);
extern int bricklet_stack_notify(BrickletStack *bricklet_stack);
extern int bricklet_stack_wait(BrickletStack *bricklet_stack);
extern int bricklet_stack_spi_transceive(BrickletStack *bricklet_stack, uint8_t *write_buffer,
                                         uint8_t *read_buffer, int length);

// New packet from brickd event loop is queued to be written to BrickletStack via SPI
static int bricklet_stack_dispatch_to_spi(Stack *stack, Packet *request, Recipient *recipient) {
	BrickletStack *bricklet_stack = (BrickletStack *)stack;
	Packet *queued_request;

	if((request->header.uid != 0) && (recipient == NULL)) {
		return 0;
	}

	if (!bricklet_stack->data_seen) {
		return 0;
	}

	mutex_lock(&bricklet_stack->request_queue_mutex);
	queued_request = queue_push(&bricklet_stack->request_queue);
	memcpy(queued_request, request, request->header.length);
	mutex_unlock(&bricklet_stack->request_queue_mutex);

	log_packet_debug("Packet is queued to be send over SPI (%s)",
	                 packet_get_request_signature(packet_signature, request));

	return 0;
}

// New packet from BrickletStack is send into brickd event loop
static void bricklet_stack_dispatch_from_spi(void *opaque) {
	BrickletStack *bricklet_stack = opaque;
	int i;
	Packet *packet;

	// handle at most 5 queued responses at once to avoid blocking the event
	// loop for too long
	for (i = 0; i < 5; ++i) {
		if (bricklet_stack_wait(bricklet_stack) < 0) {
			return;
		}

		mutex_lock(&bricklet_stack->response_queue_mutex);
		packet = queue_peek(&bricklet_stack->response_queue);
		mutex_unlock(&bricklet_stack->response_queue_mutex);

		if (packet == NULL) { // response indicated but queue is empty
			log_error("Response queue and notification event are out-of-sync");

			return;
		}

		// Update routing table (this is necessary for Co-MCU Bricklets)
		if (packet->header.function_id == CALLBACK_ENUMERATE) {
			stack_add_recipient(&bricklet_stack->base, packet->header.uid, 0);
		}

		if ((packet->header.function_id == CALLBACK_ENUMERATE) ||
		    (packet->header.function_id == FUNCTION_GET_IDENTITY)) {
			EnumerateCallback *ec = (EnumerateCallback*)packet;

			// If the Bricklet is a HAT Brick (ID 111) or HAT Zero Brick (ID 112)
			// we update the connected_uid.
			if((ec->device_identifier == 111) || (ec->device_identifier == 112)) {
				*bricklet_stack->config.connected_uid = ec->header.uid;
			}

			// If the Bricklet is connected to an isolator we don't have to
			// update the position and the connected UID. this is already
			// done by the isolator itself.
			if(ec->position != 'Z' || ec->connected_uid[0] == '\0') {
				memcpy(ec->connected_uid, PACKET_NO_CONNECTED_UID_STR, PACKET_NO_CONNECTED_UID_STR_LENGTH);

				if((*bricklet_stack->config.connected_uid != 0) && (ec->device_identifier != 111) && (ec->device_identifier != 112)) {
					char base58[BASE58_MAX_LENGTH];
					base58_encode(base58, uint32_from_le(*bricklet_stack->config.connected_uid));
					strncpy(ec->connected_uid, base58, BASE58_MAX_LENGTH);
				}

				ec->position = 'a' + (char)bricklet_stack->config.index;
			}
		}

		// Send message into brickd dispatcher
		network_dispatch_response(packet);
		bricklet_stack->data_seen = true;

		mutex_lock(&bricklet_stack->response_queue_mutex);
		queue_pop(&bricklet_stack->response_queue, NULL);
		mutex_unlock(&bricklet_stack->response_queue_mutex);
	}
}

static bool bricklet_stack_is_time_elapsed_ms(const uint64_t start_measurement, const uint64_t time_to_be_elapsed) {
	return (millitime() - start_measurement) >= time_to_be_elapsed;
}

static uint16_t bricklet_stack_check_missing_length(BrickletStack *bricklet_stack) {
	// Peak into the buffer to get the message length.
	// Only call this before or after bricklet_co_mcu_check_recv.
	Ringbuffer *rb = &bricklet_stack->ringbuffer_recv;

	while(rb->start != rb->end) {
		uint8_t length = rb->buffer[rb->start];

		if((length < SPITFP_MIN_TFP_MESSAGE_LENGTH || length > SPITFP_MAX_TFP_MESSAGE_LENGTH) && length != SPITFP_PROTOCOL_OVERHEAD) {
			if(length != 0) {
				bricklet_stack->error_count_frame++;
			}

			ringbuffer_remove(rb, 1);
			continue;
		}

		int32_t ret = length - ringbuffer_get_used(rb);

		if((ret < 0) || (ret > TFP_MESSAGE_MAX_LENGTH)) {
			return 0;
		}

		return (uint16_t)ret;
	}

	return 0;
}

static uint8_t bricklet_stack_get_sequence_byte(BrickletStack *bricklet_stack, const bool increase) {
	if(increase) {
		bricklet_stack->current_sequence_number++;

		if(bricklet_stack->current_sequence_number > 0xF) {
			bricklet_stack->current_sequence_number = 2;
		}
	}

	return bricklet_stack->current_sequence_number | (bricklet_stack->last_sequence_number_seen << 4);
}

static void bricklet_stack_check_message_send_timeout(BrickletStack *bricklet_stack) {
	// If we are not currently sending a message
	// and there is still data in the buffer
	// and the timeout ran out we resend the message
	if((bricklet_stack->buffer_send_length > SPITFP_PROTOCOL_OVERHEAD) &&
	   (bricklet_stack_is_time_elapsed_ms(bricklet_stack->last_send_started, SPITFP_TIMEOUT) || bricklet_stack->ack_to_send)) {

		// Update sequence number of send buffer. We don't increase the current sequence
		// number, but if we have seen a new message from the master we insert
		// the updated "last seen sequence number".
		// If the number changed we also have to update the checksum.
		uint8_t new_sequence_byte = bricklet_stack_get_sequence_byte(bricklet_stack, false);

		if(new_sequence_byte != bricklet_stack->buffer_send[1]) {
			bricklet_stack->buffer_send[1] = new_sequence_byte;

			uint8_t checksum = 0;

			for(uint8_t i = 0; i < bricklet_stack->buffer_send[0]-1; i++) {
				PEARSON(checksum, bricklet_stack->buffer_send[i]);
			}

			bricklet_stack->buffer_send[bricklet_stack->buffer_send[0]-1] = checksum;
		}

		bricklet_stack->wait_for_ack = false;
		bricklet_stack->ack_to_send = false;
		bricklet_stack->last_send_started = millitime();
	}
}

static void bricklet_stack_send_ack_and_message(BrickletStack *bricklet_stack, uint8_t *data, const uint8_t length) {
	uint8_t checksum = 0;

	bricklet_stack->buffer_send_length = length + SPITFP_PROTOCOL_OVERHEAD;
	bricklet_stack->buffer_send[0] = bricklet_stack->buffer_send_length;
	PEARSON(checksum, bricklet_stack->buffer_send_length);

	bricklet_stack->buffer_send[1] = bricklet_stack_get_sequence_byte(bricklet_stack, true);
	PEARSON(checksum, bricklet_stack->buffer_send[1]);

	for(uint8_t i = 0; i < length; i++) {
		bricklet_stack->buffer_send[2+i] = data[i];
		PEARSON(checksum, bricklet_stack->buffer_send[2+i]);
	}

	bricklet_stack->buffer_send[length + SPITFP_PROTOCOL_OVERHEAD-1] = checksum;

	bricklet_stack->ack_to_send = false;
	bricklet_stack->last_send_started = millitime();
}

static void bricklet_stack_check_request_queue(BrickletStack *bricklet_stack) {
	if(bricklet_stack->buffer_send_length != 0) {
		return;
	}

	mutex_lock(&bricklet_stack->request_queue_mutex);
	Packet *request = queue_peek(&bricklet_stack->request_queue);
	mutex_unlock(&bricklet_stack->request_queue_mutex);

	if(request != NULL) {
		bricklet_stack_send_ack_and_message(bricklet_stack, (uint8_t*)request, request->header.length);

		mutex_lock(&bricklet_stack->request_queue_mutex);
		queue_pop(&bricklet_stack->request_queue, NULL);
		mutex_unlock(&bricklet_stack->request_queue_mutex);
	}
}

static void bricklet_stack_handle_protocol_error(BrickletStack *bricklet_stack) {
	// In case of error we completely empty the ringbuffer
	uint8_t data;
	while(ringbuffer_get(&bricklet_stack->ringbuffer_recv, &data));
}

static bool bricklet_stack_handle_message_from_bricklet(BrickletStack *bricklet_stack, uint8_t *data, const uint8_t length) {
	Packet *queued_response;

	mutex_lock(&bricklet_stack->response_queue_mutex);
	queued_response = queue_push(&bricklet_stack->response_queue);
	memcpy(queued_response, data, length);
	mutex_unlock(&bricklet_stack->response_queue_mutex);

	if (bricklet_stack_notify(bricklet_stack) < 0) {
		return false;
	}

	return true;
}

static void bricklet_stack_send_ack(BrickletStack *bricklet_stack) {
	// If there is a request to send, we can do it now and include the ACK
	bricklet_stack_check_request_queue(bricklet_stack);

	if(bricklet_stack->buffer_send_length > 0) {
		return;
	}

	// Set new sequence number and checksum for ACK
	bricklet_stack->buffer_send[0] = SPITFP_PROTOCOL_OVERHEAD;
	bricklet_stack->buffer_send[1] = bricklet_stack->last_sequence_number_seen << 4;
	bricklet_stack->buffer_send[2] = pearson_permutation[pearson_permutation[bricklet_stack->buffer_send[0]] ^ bricklet_stack->buffer_send[1]];

	bricklet_stack->buffer_send_length = SPITFP_PROTOCOL_OVERHEAD;

	bricklet_stack->ack_to_send = false;

	bricklet_stack->last_send_started = millitime();
}

static bool bricklet_stack_is_send_possible(BrickletStack *bricklet_stack) {
	 return bricklet_stack->buffer_send_length == 0;
}

static void bricklet_stack_check_message(BrickletStack *bricklet_stack) {
	// If the temporary buffer length is > 0 we still have a message to handle
	if(bricklet_stack->buffer_recv_tmp_length > 0) {
		// Try to send message to Bricklet
		if(bricklet_stack_handle_message_from_bricklet(bricklet_stack, bricklet_stack->buffer_recv_tmp, bricklet_stack->buffer_recv_tmp_length)) {
			bricklet_stack->buffer_recv_tmp_length = 0;

			// If we were able to send message to Bricklet, try to send ACK
			if(bricklet_stack_is_send_possible(bricklet_stack)) {
				bricklet_stack_send_ack(bricklet_stack);
			} else {
				// If we can't send the ack we set a flag here and the ACK is send later on.
				// If we aren't fast enough the slave may send us a duplicate of the message,
				// but the duplicate will be thrown away since the sequence number will not
				// be increased in the meantime.
				bricklet_stack->ack_to_send = true;
			}
		}
	}

	// Check if we didn't receive an ACK within the timeout time and resend the message if necessary.
	bricklet_stack_check_message_send_timeout(bricklet_stack);

	uint8_t message[TFP_MESSAGE_MAX_LENGTH] = {0};
	uint8_t message_position = 0;
	uint16_t num_to_remove_from_ringbuffer = 0;
	uint8_t checksum = 0;

	uint8_t data_sequence_number = 0;
	uint8_t data_length = 0;

	SPITFPState state = SPITFP_STATE_START;
	uint16_t used = ringbuffer_get_used(&bricklet_stack->ringbuffer_recv);
	uint16_t start = bricklet_stack->ringbuffer_recv.start;

	for(uint16_t i = start; i < start+used; i++) {
		const uint16_t index = i & BRICKLET_STACK_SPI_RECEIVE_BUFFER_MASK;
		const uint8_t data = bricklet_stack->buffer_recv[index];

		// Handle "standard case" first (we are sending data and Master has nothing to send)
		if(state == SPITFP_STATE_START && data == 0) {
			// equivalent (but faster) to "ringbuffer_remove(&bricklet_stack->ringbuffer_recv, 1);"
			bricklet_stack->ringbuffer_recv.start = (bricklet_stack->ringbuffer_recv.start + 1) & BRICKLET_STACK_SPI_RECEIVE_BUFFER_MASK;
			continue;
		}

		num_to_remove_from_ringbuffer++;

		switch(state) {
			case SPITFP_STATE_START: {
				checksum = 0;
				message_position = 0;

				if(data == SPITFP_PROTOCOL_OVERHEAD) {
					state = SPITFP_STATE_ACK_SEQUENCE_NUMBER;
				} else if(data >= SPITFP_MIN_TFP_MESSAGE_LENGTH && data <= SPITFP_MAX_TFP_MESSAGE_LENGTH) {
					state = SPITFP_STATE_MESSAGE_SEQUENCE_NUMBER;
				} else if(data == 0) {
					// equivalent (but faster) to "ringbuffer_remove(&bricklet_stack->ringbuffer_recv, 1);"
					bricklet_stack->ringbuffer_recv.start = (bricklet_stack->ringbuffer_recv.start + 1) & BRICKLET_STACK_SPI_RECEIVE_BUFFER_MASK;
					num_to_remove_from_ringbuffer--;
					break;
				} else {
					// If the length is not PROTOCOL_OVERHEAD or within [MIN_TFP_MESSAGE_LENGTH, MAX_TFP_MESSAGE_LENGTH]
					// or 0, something has gone wrong!
					bricklet_stack->error_count_frame++;
					bricklet_stack_handle_protocol_error(bricklet_stack);
					log_debug("Frame error (count=%u)", bricklet_stack->error_count_frame);
					return;
				}

				data_length = data;

				if((start+used - i) < data_length) {
					// There can't be enough data for a whole message, we can return here.
					return;
				}

				PEARSON(checksum, data_length);

				break;
			}

			case SPITFP_STATE_ACK_SEQUENCE_NUMBER: {
				data_sequence_number = data;
				PEARSON(checksum, data_sequence_number);
				state = SPITFP_STATE_ACK_CHECKSUM;
				break;
			}

			case SPITFP_STATE_ACK_CHECKSUM: {
				// Whatever happens here, we will go to start again and remove
				// data from ringbuffer
				state = SPITFP_STATE_START;
				ringbuffer_remove(&bricklet_stack->ringbuffer_recv, num_to_remove_from_ringbuffer);
				num_to_remove_from_ringbuffer = 0;

				if(checksum != data) {
					bricklet_stack->error_count_ack_checksum++;
					bricklet_stack_handle_protocol_error(bricklet_stack);
					log_debug("ACK checksum error (count=%u)", bricklet_stack->error_count_ack_checksum);
					return;
				}

				uint8_t last_sequence_number_seen_by_slave = (data_sequence_number & 0xF0) >> 4;

				if(last_sequence_number_seen_by_slave == bricklet_stack->current_sequence_number) {
					bricklet_stack->buffer_send_length = 0;
					bricklet_stack->wait_for_ack = false;
				}

				break;
			}

			case SPITFP_STATE_MESSAGE_SEQUENCE_NUMBER: {
				data_sequence_number = data;
				PEARSON(checksum, data_sequence_number);
				state = SPITFP_STATE_MESSAGE_DATA;
				break;
			}

			case SPITFP_STATE_MESSAGE_DATA: {
				message[message_position] = data;
				message_position++;

				PEARSON(checksum, data);

				if(message_position == data_length - SPITFP_PROTOCOL_OVERHEAD) {
					state = SPITFP_STATE_MESSAGE_CHECKSUM;
				}
				break;
			}

			case SPITFP_STATE_MESSAGE_CHECKSUM: {
				// Whatever happens here, we will go to start again
				state = SPITFP_STATE_START;

				// Remove data from ringbuffer. If we can't send it we can't handle
				// it at the moment we will wait for the SPI master to re-send it.
				ringbuffer_remove(&bricklet_stack->ringbuffer_recv, num_to_remove_from_ringbuffer);
				num_to_remove_from_ringbuffer = 0;

				if(checksum != data) {
					bricklet_stack->error_count_message_checksum++;
					bricklet_stack_handle_protocol_error(bricklet_stack);
					log_debug("Message checksum error (count=%u)", bricklet_stack->error_count_message_checksum);
					return;
				}

				uint8_t last_sequence_number_seen_by_slave = (data_sequence_number & 0xF0) >> 4;
				if(last_sequence_number_seen_by_slave == bricklet_stack->current_sequence_number) {
					bricklet_stack->buffer_send_length = 0;
					bricklet_stack->wait_for_ack = false;
				}

				// If we already have one recv message in the temporary buffer,
				// we don't handle the newly received message and just throw it away.
				// The SPI master will send it again.
				if(bricklet_stack->buffer_recv_tmp_length == 0) {
					// If sequence number is new, we can handle the message.
					// Otherwise we only ACK the already handled message again.
					const uint8_t message_sequence_number = data_sequence_number & 0x0F;

					if((message_sequence_number != bricklet_stack->last_sequence_number_seen) || (message_sequence_number == 1)) {
						// For the special case that the sequence number is 1 (only used for the very first message)
						// we always send an answer, even if we haven't seen anything else in between.
						// Otherwise it is not possible to reset the Master Brick if no messages were exchanged before
						// the reset

						bricklet_stack->last_sequence_number_seen = message_sequence_number;
						// The handle message function will send an ACK for the message
						// if it can handle the message at the current moment.
						// Otherwise it will save the message and length for it it be send
						// later on.
						if(bricklet_stack_handle_message_from_bricklet(bricklet_stack, message, message_position)) {
							if(bricklet_stack_is_send_possible(bricklet_stack)) {
								bricklet_stack_send_ack(bricklet_stack);
							} else {
								bricklet_stack->ack_to_send = true;
							}
						} else {
							bricklet_stack->buffer_recv_tmp_length = message_position;
							memcpy(bricklet_stack->buffer_recv_tmp, message, message_position);
						}
					} else {
						if(bricklet_stack_is_send_possible(bricklet_stack)) {
							bricklet_stack_send_ack(bricklet_stack);
						} else {
							bricklet_stack->ack_to_send = true;
						}
					}
				}

				return;
			}
		}
	}
}

static void bricklet_stack_transceive(BrickletStack *bricklet_stack) {
	// If we have not seen any data from the Bricklet we increase a counter.
	// If the counter reaches BRICKLET_STACK_FIRST_MESSAGE_TRIES we assume that
	// there is no Bricklet and we stop trying to send to initial message (if a
	// Bricklet is hotplugged it will send a enumerate itself).
	if(!bricklet_stack->data_seen) {
		if(bricklet_stack->first_message_tries < BRICKLET_STACK_FIRST_MESSAGE_TRIES) {
			bricklet_stack->first_message_tries++;
		} else {
			bricklet_stack->buffer_send_length = 0;
		}
	}

	const uint16_t length_read = bricklet_stack_check_missing_length(bricklet_stack);

	if(bricklet_stack->buffer_send_length == 0) {
		// If buffer is empty we try to send request from the queue.
		bricklet_stack_check_request_queue(bricklet_stack);

		if((bricklet_stack->buffer_send_length == 0) && (bricklet_stack->ack_to_send)) {
			// If there is no request in the queue (buffer still empty)
			// and we have to send an ACK still, we send the ACK.
			bricklet_stack_send_ack(bricklet_stack);
		}
	}

	uint16_t length_write = bricklet_stack->wait_for_ack ? 0 : bricklet_stack->buffer_send_length;
	uint16_t length = MAX(MAX(length_read, length_write), 1);

	uint8_t rx[SPITFP_MAX_TFP_MESSAGE_LENGTH] = {0};
	uint8_t tx[SPITFP_MAX_TFP_MESSAGE_LENGTH] = {0};

	if((length == 1) || (!bricklet_stack->data_seen)) {
		// If there is nothing to read or to write, we give the Bricklet some breathing
		// room before we start polling again.

		uint32_t sleep_us = 0;

		if(!bricklet_stack->data_seen) {
			// If we have never seen any data, we will first poll every 1ms with the StackEnumerate message
			// and switch to polling every 500ms after we tried BRICKLET_STACK_FIRST_MESSAGE_TRIES times.
			// In this case there is likely no Bricklet connected. If a Bricklet is hotplugged "data_seen"
			// will be true and we will switch to polling every 200us immediately.
			if(bricklet_stack->first_message_tries < BRICKLET_STACK_FIRST_MESSAGE_TRIES) {
				sleep_us = 1000;
			} else {
				sleep_us = 500000;
			}
		}

		// If we have nothing to send and we are currently not awaiting data from the Bricklet, we will
		// poll every Xus (default is 200us).
		sleep_us = MAX(bricklet_stack->config.sleep_between_reads, sleep_us);
		microsleep(sleep_us);
	}

	memcpy(tx, bricklet_stack->buffer_send, length_write);

	// Make sure that we only access SPI once at a time
	mutex_lock(bricklet_stack->config.mutex);

	// Do chip select by hand if necessary
	if(bricklet_stack->config.chip_select_driver == BRICKLET_CHIP_SELECT_DRIVER_GPIO) {
		if(bricklet_stack_chip_select_gpio(bricklet_stack, true) < 0) {
			log_error("Could not enable chip select");
			return;
		}
	}

	int rc = bricklet_stack_spi_transceive(bricklet_stack, tx, rx, length);

	// If the length is 1 (i.e. we wanted to see if the SPI slave has data for us)
	// and he does have data for us, we will immediately retrieve the data without
	// giving back the mutex.
	if((length == 1) && (rx[0] != 0) && (rc == length) && (length_write == 0)) {
		// First add the one byte of already received data to the ringbuffer
		ringbuffer_add(&bricklet_stack->ringbuffer_recv, rx[0]);

		// Set rc to 0, so if there is no more data to read, we don't get the
		// "unexpected result" error
		rc = 0;

		// Get length for rest of message
		length = bricklet_stack_check_missing_length(bricklet_stack);

		if(length != 0) {
			// Set first byte back to 0 and the new length, the rest was not touched
			// and we don't need to reinitialize it.
			rx[0] = 0;
			rc = bricklet_stack_spi_transceive(bricklet_stack, tx, rx, length);
		}
	}

	// Do chip deselect by hand if necessary
	if(bricklet_stack->config.chip_select_driver == BRICKLET_CHIP_SELECT_DRIVER_GPIO) {
		if(bricklet_stack_chip_select_gpio(bricklet_stack, false) < 0) {
			log_error("Could not disable chip select");
			return;
		}
	}

	mutex_unlock(bricklet_stack->config.mutex);

	if (rc < 0) {
		log_error("SPI transceive failed: %s (%d)", get_errno_name(errno), errno);
		return;
	}

	if (rc != length) {
		log_error("SPI transceive has unexpected result (actual: %d != expected: %d)", rc, length);
		return;
	}

	// We don't expect an ACK to be acked, so we can set the length to 0 here
	if(bricklet_stack->buffer_send_length == SPITFP_PROTOCOL_OVERHEAD) {
		bricklet_stack->buffer_send_length = 0;
	}

	if(bricklet_stack->buffer_send_length >= SPITFP_MIN_TFP_MESSAGE_LENGTH) {
		bricklet_stack->wait_for_ack = true;
	}

	for(uint16_t i = 0; i < length; i++) {
		ringbuffer_add(&bricklet_stack->ringbuffer_recv, rx[i]);
	}
}

static void bricklet_stack_spi_thread(void *opaque) {
	BrickletStack *bricklet_stack = opaque;

	bricklet_stack->spi_thread_running = true;

	// Depending on the configuration we wait on startup for
	// other Bricklets to identify themself first.
	millisleep(bricklet_stack->config.startup_wait_time);

	// Pre-fill the send buffer with the "StackEnumerate"-Packet.
	// This packet will trigger an initial enumeration in the Bricklet.
	// If the Brick Daemon is restarted, we need to
	// trigger the initial enumeration, since the Bricklet does not know
	// that it has to enumerate itself again.
	PacketHeader header = {
		.uid                         = 0,
		.length                      = sizeof(PacketHeader),
		.function_id                 = FUNCTION_STACK_ENUMERATE,
		.sequence_number_and_options = 0x08, // return expected
		.error_code_and_future_use   = 0
	};

	bricklet_stack_send_ack_and_message(bricklet_stack, (uint8_t*)&header, sizeof(PacketHeader));

	while (bricklet_stack->spi_thread_running) {
		bricklet_stack_transceive(bricklet_stack);
		bricklet_stack_check_message(bricklet_stack);
	}
}

int bricklet_stack_create(BrickletStack *bricklet_stack, BrickletStackConfig *config) {
	int phase = 0;
	int rc;
	char bricklet_stack_name[128];

	log_debug("Initializing Bricklet stack subsystem for '%s' (num %d)",
	          config->spidev, config->chip_select_gpio_num);

	// create bricklet_stack struct
	bricklet_stack->platform = NULL;
	bricklet_stack->spi_thread_running = false;

	memcpy(&bricklet_stack->config, config, sizeof(BrickletStackConfig));

	ringbuffer_init(&bricklet_stack->ringbuffer_recv,
	                BRICKLET_STACK_SPI_RECEIVE_BUFFER_LENGTH,
	                bricklet_stack->buffer_recv);

	// create base stack
	if (snprintf(bricklet_stack_name, sizeof(bricklet_stack_name), "Bricklet-%s", bricklet_stack->config.spidev) < 0) {
		goto cleanup;
	}

	if (stack_create(&bricklet_stack->base, bricklet_stack_name, bricklet_stack_dispatch_to_spi) < 0) {
		log_error("Could not create base stack for Bricklet stack: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 1;

	// add to stacks array
	if (hardware_add_stack(&bricklet_stack->base) < 0) {
		goto cleanup;
	}

	phase = 2;

	// create notification event/pipe
#ifdef __linux__
	rc = eventfd(0, EFD_NONBLOCK | EFD_SEMAPHORE);

	if (rc >= 0) {
		bricklet_stack->notification_event = rc;
	}
#else
	rc = pipe_create(&bricklet_stack->notification_pipe, PIPE_FLAG_NON_BLOCKING_READ);

	if (rc >= 0) {
		bricklet_stack->notification_event = bricklet_stack->notification_pipe.base.read_handle;
	}
#endif

	if (rc < 0) {
		log_error("Could not create Bricklet notification event/pipe: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 3;

	// Add notification pipe as event source.
	// Event is used to dispatch packets.
	if (event_add_source(bricklet_stack->notification_event, EVENT_SOURCE_TYPE_GENERIC,
	                     "bricklet-stack-notification", EVENT_READ,
	                     bricklet_stack_dispatch_from_spi, bricklet_stack) < 0) {
		log_error("Could not add Bricklet notification pipe as event source");

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

	if (bricklet_stack_create_platform(bricklet_stack) < 0) {
		goto cleanup;
	}

	thread_create(&bricklet_stack->spi_thread, bricklet_stack_spi_thread, bricklet_stack);

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
#ifdef __linux__
		robust_close(bricklet_stack->notification_event);
#else
		pipe_destroy(&bricklet_stack->notification_pipe);
#endif
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

	return phase == 7 ? 0 : -1;
}

void bricklet_stack_destroy(BrickletStack *bricklet_stack) {
	// Remove event as possible poll source
	event_remove_source(bricklet_stack->notification_event, EVENT_SOURCE_TYPE_GENERIC);

	// Make sure that Thread shuts down properly
	if (bricklet_stack->spi_thread_running) {
		bricklet_stack->spi_thread_running = false;

		thread_join(&bricklet_stack->spi_thread);
		thread_destroy(&bricklet_stack->spi_thread);
	}

	bricklet_stack_destroy_platform(bricklet_stack);

	hardware_remove_stack(&bricklet_stack->base);
	stack_destroy(&bricklet_stack->base);

	queue_destroy(&bricklet_stack->request_queue, NULL);
	mutex_destroy(&bricklet_stack->request_queue_mutex);

	queue_destroy(&bricklet_stack->response_queue, NULL);
	mutex_destroy(&bricklet_stack->response_queue_mutex);

	// Close file descriptors
#ifdef __linux__
	robust_close(bricklet_stack->notification_event);
#else
	pipe_destroy(&bricklet_stack->notification_pipe);
#endif
}
