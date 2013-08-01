/*
 * brickd
 * Copyright (C) 2012-2013 Matthias Bolte <matthias@tinkerforge.com>
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

#include "utils.h"

enum {
	FUNCTION_DISCONNECT_PROBE = 128,
	CALLBACK_ENUMERATE = 253
};

enum {
	ENUMERATION_TYPE_AVAILABLE = 0,
	ENUMERATION_TYPE_CONNECTED = 1,
	ENUMERATION_TYPE_DISCONNECTED = 2
};

#if defined _MSC_VER || defined __BORLANDC__
	#pragma pack(push)
	#pragma pack(1)
	#define ATTRIBUTE_PACKED
#elif defined __GNUC__
	#ifdef _WIN32
		// workaround struct packing bug in GCC 4.7 on Windows
		// http://gcc.gnu.org/bugzilla/show_bug.cgi?id=52991
		#define ATTRIBUTE_PACKED __attribute__((gcc_struct, packed))
	#else
		#define ATTRIBUTE_PACKED __attribute__((packed))
	#endif
#else
	#error unknown compiler, do not know how to enable struct packing
#endif

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

#if defined _MSC_VER || defined __BORLANDC__
	#pragma pack(pop)
#endif
#undef ATTRIBUTE_PACKED

STATIC_ASSERT(sizeof(PacketHeader) == 8, "PacketHeader has invalid size");
STATIC_ASSERT(sizeof(Packet) == 80, "Packet has invalid size");
STATIC_ASSERT(sizeof(EnumerateCallback) == 34, "EnumerateCallback has invalid size");

int packet_header_is_valid_request(PacketHeader *header, const char **message);

int packet_header_is_valid_response(PacketHeader *header, const char **message);

uint8_t packet_header_get_sequence_number(PacketHeader *header);

void packet_header_set_sequence_number(PacketHeader *header, uint8_t sequence_number);

uint8_t packet_header_get_response_expected(PacketHeader *header);

uint8_t packet_header_get_error_code(PacketHeader *header);

const char *packet_get_callback_type(Packet *packet);

int packet_is_matching_response(Packet *packet, Packet *pending_request);

#endif // BRICKD_PACKET_H
