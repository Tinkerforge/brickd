/*
 * brickd
 * Copyright (C) 2014 Olaf Lüke <olaf@tinkerforge.com>
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
#include "event.h"

#define WEBSOCKET_MAX_LINE_LENGTH 100 // Line length > 100 are not interesting for us
#define WEBSOCKET_KEY_LENGTH 37 // Can be max 36
#define WEBSOCKET_CONCATKEY_LENGTH 65  // Can be max 36 (server key) + 28 (base64 hash) = 64

#define WEBSOCKET_CLIENT_KEY_STR "Sec-WebSocket-Key:"
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

#include "packed_begin.h"

typedef struct {
/*	uint8_t opcode : 4;
	uint8_t rsv1 : 1;
	uint8_t rsv2 : 1;
	uint8_t rsv3 : 1;
	uint8_t fin : 1;*/
	uint8_t opcode_rsv_fin;

/*	uint8_t payload_length : 7;
	uint8_t mask : 1;*/ // mask is 0, no masking key
	uint8_t payload_length_mask;
} ATTRIBUTE_PACKED WebsocketFrameServerToClient;

typedef struct {
	uint8_t opcode_rsv_fin;
	uint8_t payload_length_mask;

	uint8_t masking_key[WEBSOCKET_MASK_LENGTH]; // only used if mask = 1
} ATTRIBUTE_PACKED WebsocketFrame;

// Extended is used if payload_length = 126
typedef struct {
	uint8_t opcode_rsv_fin;
	uint8_t payload_length_mask;

	uint16_t payload_length_extended; // note endianess

	uint8_t masking_key[WEBSOCKET_MASK_LENGTH]; // only used if mask = 1
} ATTRIBUTE_PACKED WebsocketFrameExtended;

// Extended2 is used if payload_length = 127
typedef struct {
	uint8_t opcode_rsv_fin;
	uint8_t payload_length_mask;

	uint64_t payload_length_extended; // note endianess

	uint8_t masking_key[WEBSOCKET_MASK_LENGTH]; // only used if mask = 1
} ATTRIBUTE_PACKED WebsocketFrameExtended2;

#include "packed_end.h"

typedef enum {
	SOCKET_TYPE_NORMAL = 0,
	SOCKET_TYPE_WEBSOCKET
} SocketType;

typedef enum {
	WEBSOCKET_STATE_WAIT_FOR_HANDSHAKE = 0,
	WEBSOCKET_STATE_HANDSHAKE_FOUND_KEY,
	WEBSOCKET_STATE_HANDSHAKE_DONE,
	WEBSOCKET_STATE_WEBSOCKET_HEADER_DONE
} WebSocketState;

typedef struct {
	SocketType type;

	// WebSocket specific data
	WebSocketState websocket_state;
	char websockte_key[WEBSOCKET_KEY_LENGTH];

	char websocket_line[WEBSOCKET_MAX_LINE_LENGTH];
	int websocket_line_index;

	WebsocketFrame websocket_frame;
	int websocket_frame_index;
	int websocket_mask_index;

	int websocket_to_read;
} SocketStorage;

int websocket_frame_get_opcode(WebsocketFrame *wf);
void websocket_frame_set_opcode(WebsocketFrame *wf, int opcode);
int websocket_frame_get_fin(WebsocketFrame *wf);
void websocket_frame_set_fin(WebsocketFrame *wf, int fin);
int websocket_frame_get_payload_length(WebsocketFrame *wf);
void websocket_frame_set_payload_length(WebsocketFrame *wf, int payload_length);
int websocket_frame_get_mask(WebsocketFrame *wf);
void websocket_frame_set_mask(WebsocketFrame *wf, int mask);

void websocket_init_storage(SocketType type, SocketStorage *storage);
int websocket_answer_handshake_error(EventHandle handle);
int websocket_answer_handshake_ok(EventHandle handle, char *key, int length);
int websocket_parse_handshake_line(EventHandle handle, SocketStorage *storage, char *line, int length);
int websocket_parse_handshake(EventHandle handle, SocketStorage *storage, char *handshake_part, int length);
int websocket_parse_header(EventHandle handle, SocketStorage *storage, void *buffer, int length);
int websocket_parse_data(EventHandle handle, SocketStorage *storage, uint8_t *buffer, int length);
int websocket_receive(EventHandle handle, SocketStorage *storage, void *buffer, int length);
int websocket_send(EventHandle handle, SocketStorage *storage, void *buffer, int length);

#endif
