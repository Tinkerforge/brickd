/*
 * brickd
 * Copyright (C) 2012-2014 Matthias Bolte <matthias@tinkerforge.com>
 *
 * packet.h: Packet definiton for protocol version 2
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

#ifndef BRICKD_PACKET_H
#define BRICKD_PACKET_H

#include <stdint.h>

enum {
	UID_BRICK_DAEMON = 1
};

typedef enum {
	FUNCTION_GET_AUTHENTICATION_NONCE = 1,
	FUNCTION_AUTHENTICATE = 2
} BrickDaemonFunctionID;

typedef enum {
	FUNCTION_DISCONNECT_PROBE = 128,
	CALLBACK_ENUMERATE = 253
} CommonBrickFunctionID;

typedef enum {
	ENUMERATION_TYPE_AVAILABLE = 0,
	ENUMERATION_TYPE_CONNECTED = 1,
	ENUMERATION_TYPE_DISCONNECTED = 2
} EnumerateCallbackEnumerationType;

typedef enum {
	ERROR_CODE_OK = 0,
	ERROR_CODE_INVALID_PARAMETER,
	ERROR_CODE_FUNCTION_NOT_SUPPORTED,
	ERROR_CODE_UNKNOWN
} ErrorCode;

#define PACKET_MAX_SIGNATURE_LENGTH 64

#include "packed_begin.h"

typedef struct {
	uint32_t uid; // always little endian
	uint8_t length;
	uint8_t function_id;
	uint8_t sequence_number_and_options;
	uint8_t error_code_and_future_use;
} ATTRIBUTE_PACKED PacketHeader;

typedef struct {
	PacketHeader header;
	uint8_t payload[64];
	uint8_t optional_data[8];
} ATTRIBUTE_PACKED Packet;

typedef struct {
	PacketHeader header;
	char uid[8];
	char connected_uid[8];
	char position;
	uint8_t hardware_version[3];
	uint8_t firmware_version[3];
	uint16_t device_identifier; // always little endian
	uint8_t enumeration_type;
} ATTRIBUTE_PACKED EnumerateCallback;

typedef struct {
	PacketHeader header;
} ATTRIBUTE_PACKED ErrorCodeResponse;

typedef struct {
	PacketHeader header;
} ATTRIBUTE_PACKED GetAuthenticationNonceRequest;

typedef struct {
	PacketHeader header;
	uint8_t server_nonce[4];
} ATTRIBUTE_PACKED GetAuthenticationNonceResponse;

typedef struct {
	PacketHeader header;
	uint8_t client_nonce[4];
	uint8_t digest[20];
} ATTRIBUTE_PACKED AuthenticateRequest;

typedef struct {
	PacketHeader header;
} ATTRIBUTE_PACKED AuthenticateResponse;

#include "packed_end.h"

int packet_header_is_valid_request(PacketHeader *header, const char **message);
int packet_header_is_valid_response(PacketHeader *header, const char **message);

uint8_t packet_header_get_sequence_number(PacketHeader *header);
void packet_header_set_sequence_number(PacketHeader *header, uint8_t sequence_number);

int packet_header_get_response_expected(PacketHeader *header);
void packet_header_set_response_expected(PacketHeader *header, int response_expected);

ErrorCode packet_header_get_error_code(PacketHeader *header);
void packet_header_set_error_code(PacketHeader *header, ErrorCode error_code);

const char *packet_get_callback_type(Packet *packet);

char *packet_get_request_signature(char *signature, Packet *packet);
char *packet_get_response_signature(char *signature, Packet *packet);
char *packet_get_callback_signature(char *signature, Packet *packet);

int packet_is_matching_response(Packet *packet, PacketHeader *pending_request);

#endif // BRICKD_PACKET_H
