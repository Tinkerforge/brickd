/*
 * brickd
 * Copyright (C) 2016 Ishraq Ibne Ashraf <ishraq@tinkerforge.com>
 *
 * mesh_stack.h: Mesh stack specific functions
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

#ifndef BRICKD_MESH_STACK_H
#define BRICKD_MESH_STACK_H

#include <daemonlib/timer.h>
#include <daemonlib/socket.h>

#include "stack.h"
#include "daemonlib/packet.h"

#define MAX_MESH_STACKS 64

#define MESH_STACK_STATE_WAIT_HELLO 1
#define MESH_STACK_STATE_OPERATIONAL 2

// In microseconds.
#define TIME_HB_DO_PING 8000000
#define TIME_WAIT_HELLO 8000000
#define TIME_HB_WAIT_PONG 8000000
#define TIME_CLEANUP_AFTER_RESET_SENT 4000000

#define ESP_MESH_VERSION     0
#define ESP_MESH_ADDRESS_LEN 6

extern Array mesh_stacks;

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

// Packet types.
#include "daemonlib/packed_begin.h"
typedef struct {
  uint8_t flag_version:2; // Version of mesh.
  uint8_t flag_option_exist:1; // Option exist flag.
  uint8_t flag_piggyback_permit:1; // Piggyback congest permit in packet.
  uint8_t flag_piggyback_request:1; // Piggyback congest request in packet.
  uint8_t flag_reserved:3; // Future use.

  struct {
      uint8_t flag_direction:1; // Direction, 1:upwards, 0:downwards
      uint8_t flag_p2p:1; // Node to node packet.
      uint8_t flag_protocol:6; // Protocol used by user data.
  } ATTRIBUTE_PACKED proto;

	uint16_t len; // Packet total length (including mesh header).
	uint8_t dst_addr[ESP_MESH_ADDRESS_LEN]; // Destination address.
	uint8_t src_addr[ESP_MESH_ADDRESS_LEN]; // Source address.
} ATTRIBUTE_PACKED esp_mesh_header_t;

typedef struct {
  esp_mesh_header_t header;
  uint8_t type;
  bool is_root_node;
  uint8_t group_id[6];
  char prefix[16];
  uint8_t firmware_version[3];
} ATTRIBUTE_PACKED pkt_mesh_hello_t;

typedef struct {
  esp_mesh_header_t header;
  uint8_t type;
} ATTRIBUTE_PACKED pkt_mesh_olleh_t;

typedef struct {
	esp_mesh_header_t header;
  uint8_t type;
} ATTRIBUTE_PACKED pkt_mesh_reset_t;

typedef struct {
	esp_mesh_header_t header;
  uint8_t type;
} ATTRIBUTE_PACKED pkt_mesh_hb_t;

typedef struct {
	esp_mesh_header_t header;
  uint8_t type;
  Packet pkt_tfp;
} ATTRIBUTE_PACKED pkt_mesh_tfp_t;
#include "daemonlib/packed_end.h"

// Mesh stack struct.
typedef struct {
  /*
   * Generic stack structure, used to store the
   * stack in the central list of stacks.
   */
  Stack base;
  Socket *sock;
  bool cleanup;
  uint8_t state;
  char prefix[16];
  uint8_t group_id[6];
  Timer timer_wait_hello;
  Timer timer_hb_do_ping;
  Timer timer_hb_wait_pong;
  int incoming_buffer_used;
  bool mesh_header_checked;
  char name[STACK_MAX_NAME_LENGTH];
  Timer timer_cleanup_after_reset_sent;
  uint8_t root_node_firmware_version[3];
  uint8_t gw_addr[ESP_MESH_ADDRESS_LEN];
  uint8_t root_node_addr[ESP_MESH_ADDRESS_LEN];
  uint8_t incoming_buffer[sizeof(esp_mesh_header_t) + sizeof(Packet) + 1];
} MeshStack;

void timer_hb_do_ping_handler(void *opaque);
bool tfp_recv_handler(MeshStack *mesh_stack);
void timer_hb_wait_pong_handler(void *opaque);
void hello_recv_handler(MeshStack *mesh_stack);
void mesh_stack_destroy(MeshStack *mesh_stack);
int mesh_stack_create(char *name, Socket *sock);
void hb_ping_recv_handler(MeshStack *mesh_stack);
void hb_pong_recv_handler(MeshStack *mesh_stack);
void arm_timer_hb_do_ping(MeshStack *mesh_stack);
void broadcast_reset_packet(MeshStack *mesh_stack);
bool hello_root_recv_handler(MeshStack *mesh_stack);
bool hello_non_root_recv_handler(MeshStack *mesh_stack);
bool is_mesh_header_valid(esp_mesh_header_t *mesh_header);
void arm_timer_cleanup_after_reset_sent(MeshStack *mesh_stack);
int mesh_stack_dispatch_request(Stack *stack, Packet *request, Recipient *recipient);

// Generate a mesh packet header.
void *esp_mesh_get_packet_header(uint8_t flag_direction,
                                 uint8_t flag_p2p,
                                 uint8_t flag_protocol,
                                 uint16_t len,
                                 uint8_t *mesh_dst_addr,
                                 uint8_t *mesh_src_addr);

#endif // BRICKD_MESH_STACK_H
