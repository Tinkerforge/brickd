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

#include <stdlib.h>
#include <string.h>

#include "websocket.h"

#include "base64_encode.h"
#include "event.h"
#include "log.h"
#include "sha_1.h"
#include "socket.h"
#include "utils.h"

#define LOG_CATEGORY LOG_CATEGORY_WEBSOCKET

extern int socket_send_platform(Socket *socket, void *buffer, int length);

int websocket_frame_get_opcode(WebsocketFrame *wf) {
	return wf->opcode_rsv_fin & 0xF;
}

void websocket_frame_set_opcode(WebsocketFrame *wf, int opcode) {
	wf->opcode_rsv_fin &= ~0xF;
	wf->opcode_rsv_fin |= (opcode & 0xF);
}

int websocket_frame_get_fin(WebsocketFrame *wf) {
	return (wf->opcode_rsv_fin >> 7) & 0x1;
}

void websocket_frame_set_fin(WebsocketFrame *wf, int fin) {
	wf->opcode_rsv_fin &= ~(0x1 << 7);
	wf->opcode_rsv_fin |= ((fin << 7) & (0x1 << 7));
}

int websocket_frame_get_payload_length(WebsocketFrame *wf) {
	return wf->payload_length_mask & 0x7F;
}

void websocket_frame_set_payload_length(WebsocketFrame *wf, int payload_length) {
	wf->payload_length_mask &= ~0x7F;
	wf->payload_length_mask |= (payload_length & 0x7F);
}

int websocket_frame_get_mask(WebsocketFrame *wf) {
	return (wf->payload_length_mask >> 7) & 0x1;
}

void websocket_frame_set_mask(WebsocketFrame *wf, int mask) {
	wf->payload_length_mask &= ~(0x1 << 7);
	wf->payload_length_mask |= ((mask << 7) & (0x1 << 7));
}

static void websocket_prepare(Websocket *websocket) {
	websocket->base.receive_epilog = websocket_receive_epilog;
	websocket->base.send_override = websocket_send_override;

	websocket->websocket_frame_index = 0;
	websocket->websocket_line_index = 0;
	websocket->websocket_state = WEBSOCKET_STATE_WAIT_FOR_HANDSHAKE;

	memset(&websocket->websocket_frame, 0, sizeof(WebsocketFrame));
	memset(websocket->websocket_line, 0, WEBSOCKET_MAX_LINE_LENGTH);
	memset(websocket->websocket_key, 0, WEBSOCKET_KEY_LENGTH);
}

int websocket_create(Websocket *websocket, int family, int type, int protocol) {
	int rc = socket_create(&websocket->base, family, type, protocol);

	if (rc < 0) {
		return rc;
	}

	websocket_prepare(websocket);

	return 0;
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

	return SOCKET_CONTINUE;
}

int websocket_parse_handshake_line(Websocket *websocket, char *line, int length) {
	int i;
	char hash[20];
	char *ret;
	uint8_t concat_i = 0;

	// Find "\r\n"
	for(i = 0; i < length; i++) {
		if(line[i] == ' ' || line[i] == '\t') {
			continue;
		}

		if(line[i] == '\r' && line[i+1] == '\n') {
			char concatkey[WEBSOCKET_CONCATKEY_LENGTH] = {0};
			int base64_length;

			if(websocket->websocket_state < WEBSOCKET_STATE_HANDSHAKE_FOUND_KEY) {
				return websocket_answer_handshake_error(websocket);
			}

			// Concatenate client and server key
			strcpy(concatkey, websocket->websocket_key);
			strcpy(concatkey+strlen(websocket->websocket_key), WEBSOCKET_SERVER_KEY);

			// Calculate sha1 hash
			SHA1((unsigned char*)concatkey, strlen(concatkey), (unsigned char*)hash);

			// Caluclate base64 from hash
			memset(concatkey, 0, WEBSOCKET_CONCATKEY_LENGTH);
			base64_length = base64_encode_string(hash, 20, concatkey, WEBSOCKET_CONCATKEY_LENGTH);

			websocket->websocket_state = WEBSOCKET_STATE_HANDSHAKE_DONE;
			return websocket_answer_handshake_ok(websocket, concatkey, base64_length);
		} else {
			break;
		}
	}

	// Find "Sec-WebSocket-Key"
	ret = strcasestr(line, WEBSOCKET_CLIENT_KEY_STRING);
	if(ret != NULL) {
		memset(websocket->websocket_key, 0, WEBSOCKET_KEY_LENGTH);

		for(i = strlen(WEBSOCKET_CLIENT_KEY_STRING); i < length; i++) {
			if(line[i] != ' ' && line[i] != '\n' && line[i] != '\r') {
				websocket->websocket_key[concat_i] = line[i];
				concat_i++;
			}
		}

		websocket->websocket_state = WEBSOCKET_STATE_HANDSHAKE_FOUND_KEY;

		return SOCKET_CONTINUE;
	}

	return SOCKET_CONTINUE;
}

int websocket_parse_handshake(Websocket *websocket, char *handshake_part, int length) {
	int i;

	if(length <= 0) {
		return length;
	}

	for(i = 0; i < length; i++) {
		// If line > WEBSOCKET_MAX_LINE_LENGTH we just read over it until we find '\n'
		// The lines we are interested in can't be that long
		if(websocket->websocket_line_index < WEBSOCKET_MAX_LINE_LENGTH-1) {
			websocket->websocket_line[websocket->websocket_line_index] = handshake_part[i];
			websocket->websocket_line_index++;
		}
		if(handshake_part[i] == '\n') {
			int ret;
			ret = websocket_parse_handshake_line(websocket, websocket->websocket_line, websocket->websocket_line_index);
			memset(websocket->websocket_line, 0, WEBSOCKET_MAX_LINE_LENGTH);
			websocket->websocket_line_index = 0;

			if (ret == -1) {
				return ret;
			}
		}
	}

	return SOCKET_CONTINUE;
}

int websocket_parse_header(Websocket *websocket, uint8_t *buffer, int length) {
	int websocket_frame_length = sizeof(WebsocketFrame);
	int to_copy = MIN(length, websocket_frame_length - websocket->websocket_frame_index);

	if (to_copy <= 0) {
		log_error("Websocket frame index has invalid value (%d)", websocket->websocket_frame_index);
		return -1;
	}

	memcpy(((char*)&websocket->websocket_frame) + websocket->websocket_frame_index, buffer, to_copy);
	if (to_copy + websocket->websocket_frame_index < websocket_frame_length) {
		websocket->websocket_frame_index += to_copy;
		return SOCKET_CONTINUE;
	} else {
		int fin = websocket_frame_get_fin(&websocket->websocket_frame);
		int opcode = websocket_frame_get_opcode(&websocket->websocket_frame);
		int payload_length = websocket_frame_get_payload_length(&websocket->websocket_frame);
		int mask = websocket_frame_get_mask(&websocket->websocket_frame);

		log_debug("Websocket header received (fin: %d, opc: %d, len: %d, key: [%d %d %d %d])",
		          fin, opcode, payload_length,
		          websocket->websocket_frame.masking_key[0],
		          websocket->websocket_frame.masking_key[1],
		          websocket->websocket_frame.masking_key[2],
		          websocket->websocket_frame.masking_key[3]);

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
			websocket->websocket_mask_index = 0;
			websocket->websocket_frame_index = 0;
			websocket->websocket_to_read = payload_length;
			websocket->websocket_state = WEBSOCKET_STATE_WEBSOCKET_HEADER_DONE;
			if(length - to_copy > 0) {
				memmove(buffer, buffer + to_copy, length - to_copy);

				return websocket_parse_data(websocket, buffer, length - to_copy);
			}

			return SOCKET_CONTINUE;
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

	log_error("Unknown websocket opcode (%d)", websocket_frame_get_opcode(&websocket->websocket_frame));
	return -1;
}

int websocket_parse_data(Websocket *websocket, uint8_t *buffer, int length) {
	int i;
	int length_recursive_add = 0;
	int to_read = MIN(length, websocket->websocket_to_read);
	for(i = 0; i < to_read; i++) {
		buffer[i] ^= websocket->websocket_frame.masking_key[websocket->websocket_mask_index];

		websocket->websocket_mask_index++;
		if(websocket->websocket_mask_index >= WEBSOCKET_MASK_LENGTH) {
			websocket->websocket_mask_index = 0;
		}
	}

	websocket->websocket_to_read -= to_read;
	if(websocket->websocket_to_read < 0) {
		log_error("Websocket length mismatch (%d)", websocket->websocket_to_read);
		return -1;
	} else if(websocket->websocket_to_read == 0) {
		websocket->websocket_state = WEBSOCKET_STATE_HANDSHAKE_DONE;
		websocket->websocket_mask_index = 0;
		websocket->websocket_frame_index = 0;
	}

	if(length > to_read) {
		length_recursive_add = websocket_receive_epilog(&websocket->base, buffer + to_read, length - to_read);
		if(length_recursive_add < 0) {
			if(length_recursive_add == SOCKET_CONTINUE) {
				length_recursive_add = 0;
			} else {
				return length_recursive_add;
			}
		}
	}

	return to_read + length_recursive_add;
}

int websocket_accept_epilog(Socket *accepted_socket) {
	websocket_prepare((Websocket *)accepted_socket);

	return 0;
}

int websocket_receive_epilog(Socket *socket, void *buffer, int length) {
	Websocket *websocket = (Websocket *)socket;

	switch(websocket->websocket_state) {
	case WEBSOCKET_STATE_WAIT_FOR_HANDSHAKE:
	case WEBSOCKET_STATE_HANDSHAKE_FOUND_KEY:
		return websocket_parse_handshake(websocket, buffer, length);

	case WEBSOCKET_STATE_HANDSHAKE_DONE:
		return websocket_parse_header(websocket, buffer, length);

	case WEBSOCKET_STATE_WEBSOCKET_HEADER_DONE:
		return websocket_parse_data(websocket, buffer, length);
	}

	log_error("In invalid websocket state (%d)", websocket->websocket_state);

	return -1;
}

int websocket_send_override(Socket *socket, void *buffer, int length) {
	WebsocketFrameServerToClientWithPayload wfstc;

	if (length > WEBSOCKET_MAX_UNEXTENDED_PAYLOAD_DATA_LENGTH) {
		// currently length should never exceed 80 (the current maximum packet
		// size). so this is just a safeguard for possible later changes to the
		// maximum packet size that might require agjustments here.

		log_error("Payload to large for unextended websocket frame (%d)", length);

		return -1;
	}

	wfstc.header.opcode_rsv_fin = 0;
	wfstc.header.payload_length_mask = 0;
	websocket_frame_set_fin((WebsocketFrame*)&wfstc.header, 1);
	websocket_frame_set_opcode((WebsocketFrame*)&wfstc.header, 2);
	websocket_frame_set_mask((WebsocketFrame*)&wfstc.header, 0);
	websocket_frame_set_payload_length((WebsocketFrame*)&wfstc.header, length);
	memcpy(wfstc.payload_data, buffer, length);

	return socket_send_platform(socket, &wfstc, sizeof(WebsocketFrameServerToClient) + length);
}
