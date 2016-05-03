/*
 * brickd
 * Copyright (C) 2014 Olaf Lüke <olaf@tinkerforge.com>
 * Copyright (C) 2014-2016 Matthias Bolte <matthias@tinkerforge.com>
 *
 * websocket.h: Miniature websocket server implementation
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

#ifndef BRICKD_WEBSOCKET_H
#define BRICKD_WEBSOCKET_H

#include <stdint.h>

#include <daemonlib/queue.h>
#include <daemonlib/socket.h>

#define WEBSOCKET_MAX_LINE_LENGTH 100 // Line length > 100 are not interesting for us
#define WEBSOCKET_CLIENT_KEY_LENGTH 37 // Can be max 36
#define WEBSOCKET_BASE64_DIGEST_LENGTH 30 // Can be max 30 for a 20 byte digest

#define WEBSOCKET_CLIENT_KEY_STRING "Sec-WebSocket-Key:"
#define WEBSOCKET_SERVER_KEY "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

#define WEBSOCKET_ANSWER_STRING_1 "HTTP/1.1 101 Switching Protocols\r\nAccess-Control-Allow-Origin: *\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: "
#define WEBSOCKET_ANSWER_STRING_2 "\r\nSec-WebSocket-Protocol: tfp\r\n\r\n"

#define WEBSOCKET_ERROR_STRING "HTTP/1.1 200 OK\r\nContent-Length: 270\r\nContent-Type: text/html\r\n\r\n<html><head><title>This is a Websocket</title></head><body>Dear Sir or Madam,<br/><br/>I regret to inform you that there is no webserver here.<br/>This port is exclusively used for Websockets.<br/><br/>Yours faithfully,<blockquote>Brick Daemon</blockquote></body></html>"

#define WEBSOCKET_OPCODE_CONTINUATION_FRAME  0
#define WEBSOCKET_OPCODE_TEXT_FRAME          1
#define WEBSOCKET_OPCODE_BINARY_FRAME        2
#define WEBSOCKET_OPCODE_CLOSE_FRAME         8
#define WEBSOCKET_OPCODE_PING_FRAME          9
#define WEBSOCKET_OPCODE_PONG_FRAME         10

#define WEBSOCKET_MASK_LENGTH 4

#define WEBSOCKET_MAX_UNEXTENDED_PAYLOAD_DATA_LENGTH 125

#include <daemonlib/packed_begin.h>

typedef struct {
	uint8_t opcode_rsv_fin; // opcode: 4, rsv1: 1, rsv2: 1, rsv3: 1, fin: 1
	uint8_t payload_length_mask; // payload_length: 7, mask: 1
} ATTRIBUTE_PACKED WebsocketFrameHeader;

typedef struct {
	WebsocketFrameHeader header;
	uint8_t payload_data[WEBSOCKET_MAX_UNEXTENDED_PAYLOAD_DATA_LENGTH];
} ATTRIBUTE_PACKED WebsocketFrameWithPayload;

typedef struct {
	WebsocketFrameHeader header;
	uint8_t masking_key[WEBSOCKET_MASK_LENGTH]; // only used if mask = 1
} ATTRIBUTE_PACKED WebsocketFrame;

// Extended is used if payload_length = 126
typedef struct {
	WebsocketFrameHeader header;
	uint16_t payload_length_extended; // note endianess
	uint8_t masking_key[WEBSOCKET_MASK_LENGTH]; // only used if mask = 1
} ATTRIBUTE_PACKED WebsocketFrameExtended;

// Extended2 is used if payload_length = 127
typedef struct {
	WebsocketFrameHeader header;
	uint64_t payload_length_extended; // note endianess
	uint8_t masking_key[WEBSOCKET_MASK_LENGTH]; // only used if mask = 1
} ATTRIBUTE_PACKED WebsocketFrameExtended2;

#include <daemonlib/packed_end.h>

typedef enum {
	WEBSOCKET_STATE_WAIT_FOR_HANDSHAKE = 0,
	WEBSOCKET_STATE_FOUND_HANDSHAKE_KEY,
	WEBSOCKET_STATE_HANDSHAKE_DONE,
	WEBSOCKET_STATE_HEADER_DONE
} WebsocketState;

typedef struct {
	Socket base;

	// WebSocket specific data
	WebsocketState state;
	char client_key[WEBSOCKET_CLIENT_KEY_LENGTH];

	char line[WEBSOCKET_MAX_LINE_LENGTH];
	int line_index;

	WebsocketFrame frame;
	int frame_index;
	int mask_index;

	int to_read;

	Queue send_queue;
} Websocket;

int websocket_frame_get_opcode(WebsocketFrameHeader *header);
void websocket_frame_set_opcode(WebsocketFrameHeader *header, int opcode);
int websocket_frame_get_fin(WebsocketFrameHeader *header);
void websocket_frame_set_fin(WebsocketFrameHeader *header, int fin);
int websocket_frame_get_payload_length(WebsocketFrameHeader *header);
void websocket_frame_set_payload_length(WebsocketFrameHeader *header, int payload_length);
int websocket_frame_get_mask(WebsocketFrameHeader *header);
void websocket_frame_set_mask(WebsocketFrameHeader *header, int mask);

int websocket_answer_handshake_error(Websocket *websocket);
int websocket_answer_handshake_ok(Websocket *websocket, char *key, int length);
int websocket_parse_handshake_line(Websocket *websocket, char *line, int length);
int websocket_parse_handshake(Websocket *websocket, char *handshake_part, int length);
int websocket_parse_header(Websocket *websocket, uint8_t *buffer, int length);
int websocket_parse_data(Websocket *websocket, uint8_t *buffer, int length);
int websocket_parse(Websocket *websocket, void *buffer, int length);

int websocket_create(Websocket *websocket);
Socket *websocket_create_allocated(void);
void websocket_destroy(Socket *socket);
int websocket_receive(Socket *socket, void *buffer, int length);
int websocket_send(Socket *socket, const void *buffer, int length);

#endif // BRICKD_WEBSOCKET_H
