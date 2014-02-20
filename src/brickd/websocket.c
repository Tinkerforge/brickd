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

#include "websocket.h"

#include <stdlib.h>
#include <string.h>

#include "event.h"
#include "socket.h"
#include "utils.h"
#include "sha_1.h"
#include "base64_encode.h"
#include "log.h"

#define LOG_CATEGORY LOG_CATEGORY_WEBSOCKET

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


void websocket_init_storage(SocketType type, SocketStorage *storage) {
	storage->type = type;
	storage->websocket_frame_index = 0;
	storage->websocket_line_index = 0;
	storage->websocket_state = WEBSOCKET_STATE_WAIT_FOR_HANDSHAKE;
	memset(&storage->websocket_frame, 0, sizeof(WebsocketFrame));
	memset(storage->websocket_line, 0, sizeof(WEBSOCKET_MAX_LINE_LENGTH));
	memset(storage->websockte_key, 0, sizeof(WEBSOCKET_KEY_LENGTH));
}

int websocket_answer_handshake_error(EventHandle handle) {
	socket_send(handle, NULL, WEBSOCKET_ERROR_STRING, strlen(WEBSOCKET_ERROR_STRING));
	return -1;
}

int websocket_answer_handshake_ok(EventHandle handle, char *key, int length) {
	int ret;

	ret = socket_send(handle, NULL, WEBSOCKET_ANSWER_STRING_1, strlen(WEBSOCKET_ANSWER_STRING_1));
	if(ret < 0) {
		return ret;
	}

	ret = socket_send(handle, NULL, key, length);
	if(ret < 0) {
		return ret;
	}

	ret = socket_send(handle, NULL, WEBSOCKET_ANSWER_STRING_2, strlen(WEBSOCKET_ANSWER_STRING_2));
	if(ret < 0) {
		return ret;
	}

	return SOCKET_CONTINUE;
}

int websocket_parse_handshake_line(EventHandle handle, SocketStorage *storage, char *line, int length) {
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

			if(storage->websocket_state < WEBSOCKET_STATE_HANDSHAKE_FOUND_KEY) {
				return websocket_answer_handshake_error(handle);
			}

			// Concatenate client and server key
			strcpy(concatkey, storage->websockte_key);
			strcpy(concatkey+strlen(storage->websockte_key), WEBSOCKET_SERVER_KEY);

			// Calculate sha1 hash
			SHA1((unsigned char*)concatkey, strlen(concatkey), (unsigned char*)hash);

			// Caluclate base64 from hash
			memset(concatkey, 0, WEBSOCKET_CONCATKEY_LENGTH);
			base64_length = base64_encode_string(hash, 20, concatkey, WEBSOCKET_CONCATKEY_LENGTH);

			storage->websocket_state = WEBSOCKET_STATE_HANDSHAKE_DONE;
			return websocket_answer_handshake_ok(handle, concatkey, base64_length);
		} else {
			break;
		}
	}

	// Find "Sec-WebSocket-Key"
	ret = strcasestr(line, WEBSOCKET_CLIENT_KEY_STR);
	if(ret != NULL) {
		memset(storage->websockte_key, 0, WEBSOCKET_KEY_LENGTH);

		for(i = strlen(WEBSOCKET_CLIENT_KEY_STR); i < length; i++) {
			if(line[i] != ' ' && line[i] != '\n' && line[i] != '\r') {
				storage->websockte_key[concat_i] = line[i];
				concat_i++;
			}
		}

		storage->websocket_state = WEBSOCKET_STATE_HANDSHAKE_FOUND_KEY;

		return SOCKET_CONTINUE;
	}

	return SOCKET_CONTINUE;
}

int websocket_parse_handshake(EventHandle handle, SocketStorage *storage, char *handshake_part, int length) {
	int i;

	if(length <= 0) {
		return length;
	}

	for(i = 0; i < length; i++) {
		// If line > WEBSOCKET_MAX_LINE_LENGTH we just read over it until we find '\n'
		// The lines we are interested in can't be that long
		if(storage->websocket_line_index < WEBSOCKET_MAX_LINE_LENGTH-1) {
			storage->websocket_line[storage->websocket_line_index] = handshake_part[i];
			storage->websocket_line_index++;
		}
		if(handshake_part[i] == '\n') {
			int ret;
			ret = websocket_parse_handshake_line(handle, storage, storage->websocket_line, storage->websocket_line_index);
			memset(storage->websocket_line, 0, WEBSOCKET_MAX_LINE_LENGTH);
			storage->websocket_line_index = 0;

			if (ret == -1) {
				return ret;
			}
		}
	}

	return SOCKET_CONTINUE;
}

int websocket_parse_header(EventHandle handle, SocketStorage *storage, void *buffer, int length) {
	int i;
	int websocket_frame_length = sizeof(WebsocketFrame);
	int to_copy = MIN(length, websocket_frame_length-storage->websocket_frame_index);
	if (to_copy <= 0) {
		log_error("Websocket frame index has invalid value (%d)", storage->websocket_frame_index);
		return -1;
	}

	memcpy(((char*)&storage->websocket_frame)+storage->websocket_frame_index, buffer, to_copy);
	if (to_copy + storage->websocket_frame_index < websocket_frame_length) {
		storage->websocket_frame_index += to_copy;
		return SOCKET_CONTINUE;
	} else {
		int fin = websocket_frame_get_fin(&storage->websocket_frame);
		int opcode = websocket_frame_get_opcode(&storage->websocket_frame);
		int payload_length = websocket_frame_get_payload_length(&storage->websocket_frame);
		int mask = websocket_frame_get_mask(&storage->websocket_frame);

		log_debug("Websocket header received (fin: %d, opc: %d, len: %d, key: [%d %d %d %d])",
		          fin, opcode, payload_length,
		          storage->websocket_frame.masking_key[0],
		          storage->websocket_frame.masking_key[1],
		          storage->websocket_frame.masking_key[2],
		          storage->websocket_frame.masking_key[3]);

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
			storage->websocket_mask_index = 0;
			storage->websocket_frame_index = 0;
			storage->websocket_to_read = payload_length;
			storage->websocket_state = WEBSOCKET_STATE_WEBSOCKET_HEADER_DONE;
			if(length - to_copy > 0) {
				for(i = 0; i < length - websocket_frame_length; i++) {
					((char*)buffer)[i] = ((char*)buffer)[i+websocket_frame_length];
				}

				return websocket_parse_data(handle, storage, buffer, length - websocket_frame_length);
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

	log_error("Unknown websocket opcode (%d)", websocket_frame_get_opcode(&storage->websocket_frame));
	return -1;
}

int websocket_parse_data(EventHandle handle, SocketStorage *storage, uint8_t *buffer, int length) {
	int i;
	int length_recursive_add = 0;
	int to_read = MIN(length, storage->websocket_to_read);
	for(i = 0; i < to_read; i++) {
		buffer[i] ^= storage->websocket_frame.masking_key[storage->websocket_mask_index];

		storage->websocket_mask_index++;
		if(storage->websocket_mask_index >= WEBSOCKET_MASK_LENGTH) {
			storage->websocket_mask_index = 0;
		}
	}

	storage->websocket_to_read -= to_read;
	if(storage->websocket_to_read < 0) {
		log_error("Websocket length mismatch (%d)", storage->websocket_to_read);
		return -1;
	} else if(storage->websocket_to_read == 0) {
		storage->websocket_state = WEBSOCKET_STATE_HANDSHAKE_DONE;
		storage->websocket_mask_index = 0;
		storage->websocket_frame_index = 0;
	}

	if(length > to_read) {
		length_recursive_add = websocket_receive(handle, storage, buffer+to_read, length - to_read);
		if(length_recursive_add < 0) {
			if(length_recursive_add == SOCKET_CONTINUE) {
				length_recursive_add = 0;
			} else {
				return length_recursive_add;
			}
		}
	}

	return length + length_recursive_add;
}

int websocket_receive(EventHandle handle, SocketStorage *storage, void *buffer, int length) {
	switch(storage->websocket_state) {
	case WEBSOCKET_STATE_WAIT_FOR_HANDSHAKE:
	case WEBSOCKET_STATE_HANDSHAKE_FOUND_KEY:
		return websocket_parse_handshake(handle, storage, buffer, length);

	case WEBSOCKET_STATE_HANDSHAKE_DONE:
		return websocket_parse_header(handle, storage, buffer, length);

	case WEBSOCKET_STATE_WEBSOCKET_HEADER_DONE:
		return websocket_parse_data(handle, storage, buffer, length);
	}

	return -1;
}

int websocket_send(EventHandle handle, SocketStorage *storage, void *buffer, int length) {
	int ret;
	int length_to_send = sizeof(WebsocketFrameServerToClient) + length;
	char *websocket_data = malloc(length_to_send);
	WebsocketFrameServerToClient wfstc;

	(void)storage;

	wfstc.opcode_rsv_fin = 0;
	wfstc.payload_length_mask = 0;
	websocket_frame_set_fin((WebsocketFrame*)&wfstc, 1);
	websocket_frame_set_opcode((WebsocketFrame*)&wfstc, 2);
	websocket_frame_set_mask((WebsocketFrame*)&wfstc, 0);
	websocket_frame_set_payload_length((WebsocketFrame*)&wfstc, length);

	memcpy((void*)websocket_data, (void*)&wfstc, sizeof(WebsocketFrameServerToClient));
	memcpy((void*)(websocket_data + sizeof(WebsocketFrameServerToClient)), buffer, length);

	ret = socket_send(handle, NULL, websocket_data, length_to_send);

	free(websocket_data);

	if(ret < 0) {
		return ret;
	}

	return length;
}
