/*
 * brickd
 * Copyright (C) 2016-2017 Ishraq Ibne Ashraf <ishraq@tinkerforge.com>
 * Copyright (C) 2017-2019 Matthias Bolte <matthias@tinkerforge.com>
 *
 * mesh_packet.c: Mesh packet definition
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

#include <stdio.h>
#include <string.h>

#include <daemonlib/macros.h>

#include "mesh_packet.h"

STATIC_ASSERT(sizeof(MeshPacketHeader) == 17, "MeshPacketHeader has invalid size");
STATIC_ASSERT(sizeof(MeshHelloPacket) == 43, "MeshHelloPacket has invalid size");
STATIC_ASSERT(sizeof(MeshOllehPacket) == 17, "MeshOllehPacket has invalid size");
STATIC_ASSERT(sizeof(MeshResetPacket) == 17, "MeshResetPacket has invalid size");
STATIC_ASSERT(sizeof(MeshHeartBeatPacket) == 17, "MeshHeartBeatPacket has invalid size");
STATIC_ASSERT(sizeof(MeshPayloadPacket) == 97, "MeshPayloadPacket has invalid size");

MeshPacketDirection mesh_packet_header_get_direction(MeshPacketHeader *header) {
	return ((header->flags >> 8) & 0x01) == 0x01;
}

void mesh_packet_header_set_direction(MeshPacketHeader *header, MeshPacketDirection direction) {
	header->flags &= ~(0x01 << 8);
	header->flags |= ((uint16_t)direction & 0x01) << 8;
}

bool mesh_packet_header_get_p2p(MeshPacketHeader *header) {
	return ((header->flags >> 9) & 0x01) == 0x01;
}

void mesh_packet_header_set_p2p(MeshPacketHeader *header, bool p2p) {
	if (p2p) {
		header->flags |= 0x01 << 9;
	} else {
		header->flags &= ~(0x01 << 9);
	}
}

MeshPacketProtocol mesh_packet_header_get_protocol(MeshPacketHeader *header) {
	return (header->flags >> 10) & 0x3F;
}

void mesh_packet_header_set_protocol(MeshPacketHeader *header, MeshPacketProtocol protocol) {
	header->flags &= ~(0x3F << 10);
	header->flags |= ((uint16_t)protocol & 0x3F) << 10;
}

bool mesh_packet_header_is_valid_response(MeshPacketHeader *header, const char **message) {
	if (header->length < sizeof(MeshPacketHeader)) {
		if (message != NULL) {
			*message = "Length is too small";
		}

		return false;
	}

	switch (header->type) {
	case MESH_PACKET_TYPE_HELLO:
		if (header->length != sizeof(MeshHelloPacket)) {
			if (message != NULL) {
				*message = "Length does not match packet type";
			}

			return false;
		}

		break;

	case MESH_PACKET_TYPE_OLLEH:
		if (header->length != sizeof(MeshOllehPacket)) {
			if (message != NULL) {
				*message = "Length does not match packet type";
			}

			return false;
		}

		break;

	case MESH_PACKET_TYPE_RESET:
		if (header->length != sizeof(MeshResetPacket)) {
			if (message != NULL) {
				*message = "Length does not match packet type";
			}

			return false;
		}

		break;

	case MESH_PACKET_TYPE_HEART_BEAT_PING:
	case MESH_PACKET_TYPE_HEART_BEAT_PONG:
		if (header->length != sizeof(MeshHeartBeatPacket)) {
			if (message != NULL) {
				*message = "Length does not match packet type";
			}

			return false;
		}

		break;

	case MESH_PACKET_TYPE_PAYLOAD:
		if (header->length < sizeof(MeshPacketHeader) + sizeof(PacketHeader)) {
			if (message != NULL) {
				*message = "Length is too small";
			}

			return false;
		}

		break;

	default:
		if (message != NULL) {
			*message = "Invalid packet type";
		}

		return false;
	}

	if (mesh_packet_header_get_direction(header) != MESH_PACKET_DIRECTION_UPWARD) {
		if (message != NULL) {
			*message = "Invalid packet direction";
		}

		return false;
	}

	if (mesh_packet_header_get_protocol(header) != MESH_PACKET_PROTOCOL_BINARY) {
		if (message != NULL) {
			*message = "Invalid packet protocol";
		}

		return false;
	}

	return true;
}

void mesh_packet_header_create(MeshPacketHeader *header, MeshPacketDirection direction,
                               bool p2p, MeshPacketProtocol protocol, uint16_t length,
                               uint8_t *dst_addr, uint8_t *src_addr, MeshPacketType type) {
	memset(header, 0, sizeof(MeshPacketHeader));

	mesh_packet_header_set_direction(header, direction);
	mesh_packet_header_set_p2p(header, p2p);
	mesh_packet_header_set_protocol(header, protocol);

	header->length = length;

	memcpy(header->dst_addr, dst_addr, sizeof(header->dst_addr));
	memcpy(header->src_addr, src_addr, sizeof(header->src_addr));

	header->type = type;
}

char *mesh_packet_get_dump(char *dump, uint8_t *packet, int length) {
	int i;

	if (length > (int)sizeof(MeshPayloadPacket)) {
		length = (int)sizeof(MeshPayloadPacket);
	}

	for (i = 0; i < length; ++i) {
		snprintf(dump + i * 3, 4, "%02X ", packet[i]);
	}

	if (length > 0) {
		dump[length * 3 - 1] = '\0';
	} else {
		dump[0] = '\0';
	}

	return dump;
}
