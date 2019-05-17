/*
 * brickd
 * Copyright (C) 2016-2017 Ishraq Ibne Ashraf <ishraq@tinkerforge.com>
 * Copyright (C) 2017-2019 Matthias Bolte <matthias@tinkerforge.com>
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
#include "mesh_packet.h"

#define MAX_MESH_STACKS 64

#define MESH_STACK_STATE_WAIT_HELLO 1
#define MESH_STACK_STATE_OPERATIONAL 2

// In microseconds.
#define TIME_HB_DO_PING 8000000
#define TIME_WAIT_HELLO 8000000
#define TIME_HB_WAIT_PONG (TIME_HB_DO_PING/2)
#define TIME_CLEANUP_AFTER_RESET_SENT 4000000

// Mesh stack struct.
typedef struct {
	/*
	 * Generic stack structure, used to store the
	 * stack in the central list of stacks.
	 */
	Stack base;
	Socket *sock; // FIXME_ does this have to be a pointer?
	bool cleanup;
	uint8_t state;
	char prefix[16];
	uint8_t group_id[6];
	Timer timer_wait_hello;
	Timer timer_hb_do_ping;
	Timer timer_hb_wait_pong;
	char name[STACK_MAX_NAME_LENGTH];
	Timer timer_cleanup_after_reset_sent;
	uint8_t root_node_firmware_version[3];
	uint8_t gw_addr[ESP_MESH_ADDRESS_LEN];
	uint8_t root_node_addr[ESP_MESH_ADDRESS_LEN];
	union {
		uint8_t response_buffer[512];
		MeshPacketHeader response_header;
		MeshHelloPacket hello_response;
		MeshHeartBeatPacket heart_beat_response;
		MeshPayloadPacket payload_response;
	};
	int response_buffer_used;
	bool response_header_checked;
} MeshStack;

void timer_hb_do_ping_handler(void *opaque);
void tfp_recv_handler(MeshStack *mesh_stack);
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
void arm_timer_cleanup_after_reset_sent(MeshStack *mesh_stack);
int mesh_stack_dispatch_request(Stack *stack, Packet *request, Recipient *recipient);

#endif // BRICKD_MESH_STACK_H
