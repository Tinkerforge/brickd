/*
 * brickd
 * Copyright (C) 2016-2017 Ishraq Ibne Ashraf <ishraq@tinkerforge.com>
 * Copyright (C) 2017-2019 Matthias Bolte <matthias@tinkerforge.com>
 *
 * mesh_packet.c: Mesh packet definiton
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

#include <string.h>

#include "mesh_packet.h"


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

bool mesh_packet_header_is_valid_response(MeshPacketHeader *mesh_header, const char **message) {
	if (mesh_header->length < sizeof(MeshPacketHeader)) {
		*message = "ESP mesh packet header length is to small";

		return false;
	}

	if (mesh_packet_header_get_direction(mesh_header) != MESH_PACKET_DIRECTION_UPWARD) {
		*message = "ESP mesh packet header has downward direction";

		return false;
	}

	if (mesh_packet_header_get_protocol(mesh_header) != MESH_PACKET_PROTOCOL_BINARY) {
		*message = "ESP mesh packet payload type is not binary";

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
