/*
 * brickd
 * Copyright (C) 2016-2017 Ishraq Ibne Ashraf <ishraq@tinkerforge.com>
 * Copyright (C) 2017-2019 Matthias Bolte <matthias@tinkerforge.com>
 *
 * mesh_packet.h: Mesh packet definiton
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

enum {
	ESP_MESH_PACKET_DOWNWARDS = 0,
	ESP_MESH_PACKET_UPWARDS,
};

enum {
	ESP_MESH_PAYLOAD_NONE = 0,
	ESP_MESH_PAYLOAD_HTTP,
	ESP_MESH_PAYLOAD_JSON,
	ESP_MESH_PAYLOAD_MQTT,
	ESP_MESH_PAYLOAD_BIN,
};

enum {
	MESH_PACKET_HELLO = 1,
	MESH_PACKET_OLLEH,
	MESH_PACKET_RESET,
	MESH_PACKET_HB_PING,
	MESH_PACKET_HB_PONG,
	MESH_PACKET_TFP
};

#include "daemonlib/packed_begin.h"

typedef struct {
	/*
	 * Flag bit size and assignment,
	 *
	 * version          :2
	 * option_exist     :1
	 * piggyback_permit :1
	 * piggyback_request:1
	 * reserved         :3
	 * direction        :1, Upwards = 1, Downwards = 0
	 * p2p              :1
	 * protocol         :6
	 */
	uint16_t flags;
	uint16_t length; // Packet total length (including mesh header).
	uint8_t dst_addr[ESP_MESH_ADDRESS_LEN]; // Destination address.
	uint8_t src_addr[ESP_MESH_ADDRESS_LEN]; // Source address.
	uint8_t type;
} ATTRIBUTE_PACKED MeshPacketHeader;

typedef struct {
	MeshPacketHeader header;
	bool is_root_node;
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

#include "daemonlib/packed_end.h"

bool get_esp_mesh_header_flag_p2p(uint8_t *flags);
bool get_esp_mesh_header_flag_direction(uint8_t *flags);
bool is_mesh_header_valid(MeshPacketHeader *mesh_header, const char **message);
uint8_t get_esp_mesh_header_flag_protocol(uint8_t *flags);
void set_esp_mesh_header_flag_p2p(uint8_t *flags, bool val);
void set_esp_mesh_header_flag_protocol(uint8_t *flags, uint8_t val);
void set_esp_mesh_header_flag_direction(uint8_t *flags, uint8_t val);
void esp_mesh_get_packet_header(MeshPacketHeader *mesh_header,
                                uint8_t flag_direction,
                                bool flag_p2p,
                                uint8_t flag_protocol,
                                uint16_t len,
                                uint8_t *mesh_dst_addr,
                                uint8_t *mesh_src_addr,
                                uint8_t type);

#endif // BRICKD_MESH_PACKET_H
