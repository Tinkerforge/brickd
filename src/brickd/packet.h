/*
 * brickd
 * Copyright (C) 2012 Matthias Bolte <matthias@tinkerforge.com>
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

#define CALLBACK_ENUMERATE 253

#define ENUMERATION_AVAILABLE 0
#define ENUMERATION_CONNECTED 1
#define ENUMERATION_DISCONNECTED 2

#if defined _MSC_VER || defined __BORLANDC__
	#pragma pack(push)
	#pragma pack(1)
	#define ATTRIBUTE_PACKED
#elif defined __GNUC__
	#define ATTRIBUTE_PACKED __attribute__((packed))
#else
	#error unknown compiler, do not know how to enable struct packing
#endif

typedef struct {
	uint32_t uid;
	uint8_t length;
	uint8_t function_id;
	uint8_t other_options : 2,
	        authentication : 1,
	        response_expected : 1,
	        sequence_number : 4;
	uint8_t error_code : 2,
	        future_use : 6;
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
	uint16_t device_identifier;
	uint8_t enumeration_type;
} ATTRIBUTE_PACKED EnumerateCallback;

#if defined _MSC_VER || defined __BORLANDC__
	#pragma pack(pop)
#endif
#undef ATTRIBUTE_PACKED

static_assert(sizeof(PacketHeader) == 8, "PacketHeader has invalid size");
static_assert(sizeof(Packet) == 80, "Packet has invalid size");

#endif // BRICKD_PACKET_H
