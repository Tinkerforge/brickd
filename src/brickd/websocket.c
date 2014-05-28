/*
 * brickd
 * Copyright (C) 2014 Olaf Lüke <olaf@tinkerforge.com>
 * Copyright (C) 2014 Matthias Bolte <matthias@tinkerforge.com>
 *
 * websocket.c: Miniature websocket server implementation
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
#include <stdlib.h>
#include <string.h>

#include "websocket.h"

#include "base64_encode.h"
#include "event.h"
#include "log.h"
#include "sha1.h"
#include "socket.h"
#include "utils.h"

#define LOG_CATEGORY LOG_CATEGORY_WEBSOCKET

extern int socket_receive_platform(Socket *socket, void *buffer, int length);
extern int socket_send_platform(Socket *socket, void *buffer, int length);

int websocket_frame_get_opcode(WebsocketFrameHeader *header) {
	return header->opcode_rsv_fin & 0xF;
}

void websocket_frame_set_opcode(WebsocketFrameHeader *header, int opcode) {
	header->opcode_rsv_fin &= ~0xF;
	header->opcode_rsv_fin |= opcode & 0xF;
}

int websocket_frame_get_fin(WebsocketFrameHeader *header) {
	return (header->opcode_rsv_fin >> 7) & 0x1;
}

void websocket_frame_set_fin(WebsocketFrameHeader *header, int fin) {
	header->opcode_rsv_fin &= ~(0x1 << 7);
	header->opcode_rsv_fin |= (fin << 7) & (0x1 << 7);
}

int websocket_frame_get_payload_length(WebsocketFrameHeader *header) {
	return header->payload_length_mask & 0x7F;
}

void websocket_frame_set_payload_length(WebsocketFrameHeader *header, int payload_length) {
	header->payload_length_mask &= ~0x7F;
	header->payload_length_mask |= payload_length & 0x7F;
}

int websocket_frame_get_mask(WebsocketFrameHeader *header) {
	return (header->payload_length_mask >> 7) & 0x1;
}

void websocket_frame_set_mask(WebsocketFrameHeader *header, int mask) {
	header->payload_length_mask &= ~(0x1 << 7);
	header->payload_length_mask |= ((mask << 7) & (0x1 << 7));
}

int websocket_answer_handshake_error(Websocket *websocket) {
	socket_send_platform(&websocket->base, WEBSOCKET_ERROR_STRING, strlen(WEBSOCKET_ERROR_STRING));

	return -1;
}

int websocket_answer_handshake_ok(Websocket *websocket, char *key, int length) {
	int ret;

	ret = socket_send_platform(&websocket->base, WEBSOCKET_ANSWER_STRING_1, strlen(WEBSOCKET_ANSWER_STRING_1));
	if(ret < 0) {
		return ret;
	}

	ret = socket_send_platform(&websocket->base, key, length);
	if(ret < 0) {
		return ret;
	}

	ret = socket_send_platform(&websocket->base, WEBSOCKET_ANSWER_STRING_2, strlen(WEBSOCKET_ANSWER_STRING_2));
	if(ret < 0) {
		return ret;
	}

	return IO_CONTINUE;
}

int websocket_parse_handshake_line(Websocket *websocket, char *line, int length) {
	int i;
	SHA1 sha1;
	uint8_t digest[SHA1_DIGEST_LENGTH];
	char base64[WEBSOCKET_BASE64_DIGEST_LENGTH];
	int base64_length;
	int k;

	// Find "\r\n"
	for(i = 0; i < length; i++) {
		if(line[i] == ' ' || line[i] == '\t') {
			continue;
		}

		if(line[i] == '\r' && line[i+1] == '\n') {
			if(websocket->state < WEBSOCKET_STATE_HANDSHAKE_FOUND_KEY) {
				return websocket_answer_handshake_error(websocket);
			}

			// calculate SHA1 over client and server key
			sha1_init(&sha1);
			sha1_update(&sha1, (uint8_t *)websocket->client_key, strlen(websocket->client_key));
			sha1_update(&sha1, (uint8_t *)WEBSOCKET_SERVER_KEY, strlen(WEBSOCKET_SERVER_KEY));
			sha1_final(&sha1, digest);

			// BASE64 encode SHA1 digest
			base64_length = base64_encode_string((char *)digest, SHA1_DIGEST_LENGTH,
			                                     base64, WEBSOCKET_BASE64_DIGEST_LENGTH);

			websocket->state = WEBSOCKET_STATE_HANDSHAKE_DONE;

			return websocket_answer_handshake_ok(websocket, base64, base64_length);
		} else {
			break;
		}
	}

	// Find "Sec-WebSocket-Key"
	if(strcasestr(line, WEBSOCKET_CLIENT_KEY_STRING) != NULL) {
		memset(websocket->client_key, 0, WEBSOCKET_CLIENT_KEY_LENGTH);

		for(i = strlen(WEBSOCKET_CLIENT_KEY_STRING), k = 0; i < length; i++) {
			if(line[i] != ' ' && line[i] != '\n' && line[i] != '\r') {
				websocket->client_key[k++] = line[i];
			}
		}

		websocket->state = WEBSOCKET_STATE_HANDSHAKE_FOUND_KEY;

		return IO_CONTINUE;
	}

	return IO_CONTINUE;
}

int websocket_parse_handshake(Websocket *websocket, char *handshake_part, int length) {
	int i;

	if(length <= 0) {
		return length;
	}

	for(i = 0; i < length; i++) {
		// If line > WEBSOCKET_MAX_LINE_LENGTH we just read over it until we find '\n'
		// The lines we are interested in can't be that long
		if(websocket->line_index < WEBSOCKET_MAX_LINE_LENGTH-1) {
			websocket->line[websocket->line_index] = handshake_part[i];
			websocket->line_index++;
		}
		if(handshake_part[i] == '\n') {
			int ret;
			ret = websocket_parse_handshake_line(websocket, websocket->line, websocket->line_index);
			memset(websocket->line, 0, WEBSOCKET_MAX_LINE_LENGTH);
			websocket->line_index = 0;

			if (ret == -1) {
				return ret;
			}
		}
	}

	return IO_CONTINUE;
}

int websocket_parse_header(Websocket *websocket, uint8_t *buffer, int length) {
	int websocket_frame_length = sizeof(WebsocketFrame);
	int to_copy = MIN(length, websocket_frame_length - websocket->frame_index);

	if (to_copy <= 0) {
		log_error("Websocket frame index has invalid value (%d)", websocket->frame_index);
		return -1;
	}

	memcpy(((char*)&websocket->frame) + websocket->frame_index, buffer, to_copy);
	if (to_copy + websocket->frame_index < websocket_frame_length) {
		websocket->frame_index += to_copy;
		return IO_CONTINUE;
	} else {
		int fin = websocket_frame_get_fin(&websocket->frame.header);
		int opcode = websocket_frame_get_opcode(&websocket->frame.header);
		int payload_length = websocket_frame_get_payload_length(&websocket->frame.header);
		int mask = websocket_frame_get_mask(&websocket->frame.header);

		log_debug("Websocket header received (fin: %d, opc: %d, len: %d, key: [%d %d %d %d])",
		          fin, opcode, payload_length,
		          websocket->frame.masking_key[0],
		          websocket->frame.masking_key[1],
		          websocket->frame.masking_key[2],
		          websocket->frame.masking_key[3]);

		if(mask != 1) {
			log_error("Websocket frame has invalid mask (%d)", mask);
			return -1;
		}
		if(payload_length == 126 || payload_length == 127) {
			log_error("Websocket frame with extended payload length not supported (%d)", payload_length);
			return -1;
		}

		switch(opcode) {
		case WEBSOCKET_OPCODE_CONTINUATION_FRAME:
		case WEBSOCKET_OPCODE_TEXT_FRAME:
			log_error("Websocket opcodes 'continuation' and 'text' not supported");
			return -1;

		case WEBSOCKET_OPCODE_BINARY_FRAME: {
			websocket->mask_index = 0;
			websocket->frame_index = 0;
			websocket->to_read = payload_length;
			websocket->state = WEBSOCKET_STATE_WEBSOCKET_HEADER_DONE;
			if(length - to_copy > 0) {
				memmove(buffer, buffer + to_copy, length - to_copy);

				return websocket_parse_data(websocket, buffer, length - to_copy);
			}

			return IO_CONTINUE;
		}

		case WEBSOCKET_OPCODE_CLOSE_FRAME:
			log_debug("Websocket opcode 'close frame'");
			return 0;

		case WEBSOCKET_OPCODE_PING_FRAME:
			log_error("Websocket opcode 'ping' not supported");
			return -1;

		case WEBSOCKET_OPCODE_PONG_FRAME:
			log_error("Websocket opcode 'pong' not supported");
			return -1;
		}
	}

	log_error("Unknown websocket opcode (%d)", websocket_frame_get_opcode(&websocket->frame.header));
	return -1;
}

int websocket_parse_data(Websocket *websocket, uint8_t *buffer, int length) {
	int i;
	int length_recursive_add = 0;
	int to_read = MIN(length, websocket->to_read);
	for(i = 0; i < to_read; i++) {
		buffer[i] ^= websocket->frame.masking_key[websocket->mask_index];

		websocket->mask_index++;
		if(websocket->mask_index >= WEBSOCKET_MASK_LENGTH) {
			websocket->mask_index = 0;
		}
	}

	websocket->to_read -= to_read;
	if(websocket->to_read < 0) {
		log_error("Websocket length mismatch (%d)", websocket->to_read);
		return -1;
	} else if(websocket->to_read == 0) {
		websocket->state = WEBSOCKET_STATE_HANDSHAKE_DONE;
		websocket->mask_index = 0;
		websocket->frame_index = 0;
	}

	if(length > to_read) {
		length_recursive_add = websocket_parse(websocket, buffer + to_read, length - to_read);
		if(length_recursive_add < 0) {
			if(length_recursive_add == IO_CONTINUE) {
				length_recursive_add = 0;
			} else {
				return length_recursive_add;
			}
		}
	}

	return to_read + length_recursive_add;
}

int websocket_parse(Websocket *websocket, void *buffer, int length) {
	switch(websocket->state) {
	case WEBSOCKET_STATE_WAIT_FOR_HANDSHAKE:
	case WEBSOCKET_STATE_HANDSHAKE_FOUND_KEY:
		return websocket_parse_handshake(websocket, buffer, length);

	case WEBSOCKET_STATE_HANDSHAKE_DONE:
		return websocket_parse_header(websocket, buffer, length);

	case WEBSOCKET_STATE_WEBSOCKET_HEADER_DONE:
		return websocket_parse_data(websocket, buffer, length);
	}

	log_error("In invalid websocket state (%d)", websocket->state);

	return -1;
}

// sets errno on error
int websocket_create(Websocket *websocket) {
	int rc = socket_create(&websocket->base);

	if (rc < 0) {
		return rc;
	}

	websocket->base.base.type = "WebSocket";
	websocket->base.receive = websocket_receive;
	websocket->base.send = websocket_send;

	websocket->frame_index = 0;
	websocket->line_index = 0;
	websocket->state = WEBSOCKET_STATE_WAIT_FOR_HANDSHAKE;

	memset(&websocket->frame, 0, sizeof(WebsocketFrame));
	memset(websocket->line, 0, WEBSOCKET_MAX_LINE_LENGTH);
	memset(websocket->client_key, 0, WEBSOCKET_CLIENT_KEY_LENGTH);

	return 0;
}

// sets errno on error
Socket *websocket_create_allocated(void) {
	Websocket *websocket = calloc(1, sizeof(Websocket));

	if (websocket == NULL) {
		errno = ENOMEM;

		return NULL;
	}

	if (websocket_create(websocket) < 0) {
		free(websocket);

		return NULL;
	}

	return &websocket->base;
}

// sets errno on error
int websocket_receive(Socket *socket, void *buffer, int length) {
	Websocket *websocket = (Websocket *)socket;

	length = socket_receive_platform(socket, buffer, length);

	if (length <= 0) {
		return length;
	}

	return websocket_parse(websocket, buffer, length);
}

// sets errno on error
int websocket_send(Socket *socket, void *buffer, int length) {
	WebsocketFrameWithPayload frame;

	if (length > WEBSOCKET_MAX_UNEXTENDED_PAYLOAD_DATA_LENGTH) {
		// currently length should never exceed 80 (the current maximum packet
		// size). so this is just a safeguard for possible later changes to the
		// maximum packet size that might require adjustments here.
		errno = E2BIG;

		return -1;
	}

	frame.header.opcode_rsv_fin = 0;
	frame.header.payload_length_mask = 0;
	websocket_frame_set_fin(&frame.header, 1);
	websocket_frame_set_opcode(&frame.header, 2);
	websocket_frame_set_mask(&frame.header, 0);
	websocket_frame_set_payload_length(&frame.header, length);
	memcpy(frame.payload_data, buffer, length);

	return socket_send_platform(socket, &frame, sizeof(WebsocketFrameHeader) + length);
}
