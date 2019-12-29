/*
 * brickd
 * Copyright (C) 2016-2017 Ishraq Ibne Ashraf <ishraq@tinkerforge.com>
 * Copyright (C) 2017-2019 Matthias Bolte <matthias@tinkerforge.com>
 *
 * mesh_packet.h: Mesh packet definition
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

#ifndef BRICKD_MESH_PACKET_H
#define BRICKD_MESH_PACKET_H

#include "daemonlib/packet.h"

#define ESP_MESH_ADDRESS_LEN 6

typedef enum {
	MESH_PACKET_DIRECTION_DOWNWARD = 0,
	MESH_PACKET_DIRECTION_UPWARD = 1
} MeshPacketDirection;

typedef enum {
	MESH_PACKET_PROTOCOL_NONE = 0,
	MESH_PACKET_PROTOCOL_HTTP = 1,
	MESH_PACKET_PROTOCOL_JSON = 2,
	MESH_PACKET_PROTOCOL_MQTT = 3,
	MESH_PACKET_PROTOCOL_BINARY = 4
} MeshPacketProtocol;

typedef enum {
	MESH_PACKET_TYPE_HELLO = 1,
	MESH_PACKET_TYPE_OLLEH = 2,
	MESH_PACKET_TYPE_RESET = 3,
	MESH_PACKET_TYPE_HEART_BEAT_PING = 4,
	MESH_PACKET_TYPE_HEART_BEAT_PONG = 5,
	MESH_PACKET_TYPE_PAYLOAD = 6
} MeshPacketType;

#include <daemonlib/packed_begin.h>

typedef struct {
	uint16_t flags; // 6 bit protocol, 1 bit p2p, 1 bit direction, 8 bit unused
	uint16_t length; // packet length including header
	uint8_t dst_addr[ESP_MESH_ADDRESS_LEN]; // Destination address.
	uint8_t src_addr[ESP_MESH_ADDRESS_LEN]; // Source address.
	uint8_t type;
} ATTRIBUTE_PACKED MeshPacketHeader;

typedef struct {
	MeshPacketHeader header;
	uint8_t is_root_node; // bool
	uint8_t group_id[6];
	char prefix[16];
	uint8_t firmware_version[3];
} ATTRIBUTE_PACKED MeshHelloPacket;

typedef struct {
	MeshPacketHeader header;
} ATTRIBUTE_PACKED MeshOllehPacket;

typedef struct {
	MeshPacketHeader header;
} ATTRIBUTE_PACKED MeshResetPacket;

typedef struct {
	MeshPacketHeader header;
} ATTRIBUTE_PACKED MeshHeartBeatPacket;

typedef struct {
	MeshPacketHeader header;
	Packet payload;
} ATTRIBUTE_PACKED MeshPayloadPacket;

#include <daemonlib/packed_end.h>

#define MESH_PACKET_MAX_DUMP_LENGTH ((int)sizeof(MeshPayloadPacket) * 3 + 1)

MeshPacketDirection mesh_packet_header_get_direction(MeshPacketHeader *header);
void mesh_packet_header_set_direction(MeshPacketHeader *header, MeshPacketDirection direction);

bool mesh_packet_header_get_p2p(MeshPacketHeader *header);
void mesh_packet_header_set_p2p(MeshPacketHeader *header, bool p2p);

MeshPacketProtocol mesh_packet_header_get_protocol(MeshPacketHeader *header);
void mesh_packet_header_set_protocol(MeshPacketHeader *header, MeshPacketProtocol protocol);

bool mesh_packet_header_is_valid_response(MeshPacketHeader *header, const char **message);

void mesh_packet_header_create(MeshPacketHeader *header, MeshPacketDirection direction,
                               bool p2p, MeshPacketProtocol protocol, uint16_t length,
                               uint8_t *dst_addr, uint8_t *src_addr, MeshPacketType type);

char *mesh_packet_get_dump(char *dump, uint8_t *packet, int length);

#endif // BRICKD_MESH_PACKET_H
