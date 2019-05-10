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

#define CHECK_BIT(val, pos) ((val) & (1 << (pos)))

bool get_esp_mesh_header_flag_p2p(uint8_t *flags) {
	if (CHECK_BIT(flags[1], 0x01) > 0) {
		return true;
	} else {
		return false;
	}
}

bool get_esp_mesh_header_flag_direction(uint8_t *flags) {
	if (CHECK_BIT(flags[1], 0x00) > 0) {
		return true;
	} else {
		return false;
	}
}

uint8_t get_esp_mesh_header_flag_protocol(uint8_t *flags) {
	return (uint8_t)(flags[1] >> 0x02);
}

void set_esp_mesh_header_flag_p2p(uint8_t *flags, bool val) {
	if (val) {
		flags[1] = (flags[1] | 0x02);
	} else {
		flags[1] = (flags[1] & ~(0x02));
	}
}

void set_esp_mesh_header_flag_protocol(uint8_t *flags, uint8_t val) {
	flags[1] = flags[1] & 0x03;
	flags[1] = (flags[1] | (val << 0x02));
}

void set_esp_mesh_header_flag_direction(uint8_t *flags, uint8_t val) {
	if (val) {
		flags[1] = (flags[1] | 0x01);
	} else {
		flags[1] = (flags[1] & ~(0x01));
	}
}

bool is_mesh_header_valid(MeshPacketHeader *mesh_header, const char **message) {
	if (mesh_header->length < sizeof(MeshPacketHeader)) {
		*message = "ESP mesh packet header length is to small";

		return false;
	}

	if (get_esp_mesh_header_flag_direction((uint8_t *)&mesh_header->flags) == ESP_MESH_PACKET_DOWNWARDS) {
		*message = "ESP mesh packet header has downward direction";

		return false;
	}

	if (get_esp_mesh_header_flag_protocol((uint8_t *)&mesh_header->flags) != ESP_MESH_PAYLOAD_BIN) {
		*message = "ESP mesh packet payload type is not binary";

		return false;
	}

	return true;
}

void esp_mesh_get_packet_header(MeshPacketHeader *mesh_header,
                                uint8_t flag_direction,
                                bool flag_p2p,
                                uint8_t flag_protocol,
                                uint16_t length,
                                uint8_t *mesh_dst_addr,
                                uint8_t *mesh_src_addr,
                                uint8_t type) {
	memset(mesh_header, 0, sizeof(MeshPacketHeader));
	set_esp_mesh_header_flag_direction((uint8_t *)&mesh_header->flags, flag_direction);
	set_esp_mesh_header_flag_p2p((uint8_t *)&mesh_header->flags, flag_p2p);
	set_esp_mesh_header_flag_protocol((uint8_t *)&mesh_header->flags, flag_protocol);
	mesh_header->length = length;

	memcpy(&mesh_header->dst_addr, mesh_dst_addr, sizeof(mesh_header->dst_addr));
	memcpy(&mesh_header->src_addr, mesh_src_addr, sizeof(mesh_header->src_addr));
	mesh_header->type = type;
}
