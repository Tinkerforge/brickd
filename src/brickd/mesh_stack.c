/*
 * brickd
 * Copyright (C) 2016-2017 Ishraq Ibne Ashraf <ishraq@tinkerforge.com>
 * Copyright (C) 2017-2019 Matthias Bolte <matthias@tinkerforge.com>
 *
 * mesh_stack.c: Mesh stack specific functions
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

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <daemonlib/array.h>
#include <daemonlib/log.h>
#include <daemonlib/utils.h>
#include <daemonlib/event.h>
#include <daemonlib/base58.h>

#include "mesh_stack.h"

#include "hardware.h"
#include "network.h"

static LogSource _log_source = LOG_SOURCE_INITIALIZER;

extern Array mesh_stacks;

static void mesh_stack_recv_handler(void *opaque) {
	int length = 0;
	uint8_t mesh_pkt_type = 0;
	MeshStack *mesh_stack = (MeshStack *)opaque;
	const char *message = NULL;
	char mesh_packet_dump[MESH_PACKET_MAX_DUMP_LENGTH];
	char packet_dump[PACKET_MAX_DUMP_LENGTH];

	if (mesh_stack->cleanup) {
		log_warn("Mesh stack (N: %s) already scheduled for cleanup, ignoring receive",
		         mesh_stack->name);

		return;
	}

	length = socket_receive(mesh_stack->sock,
	                        mesh_stack->response_buffer + mesh_stack->response_buffer_used,
	                        sizeof(mesh_stack->response_buffer) - mesh_stack->response_buffer_used);

	if (length == 0) {
		log_info("Mesh stack (N: %s) disconnected by peer",
		         mesh_stack->name);

		mesh_stack->cleanup = true;

		return;
	}

	if (length < 0) {
		if (length == IO_CONTINUE) {
			// no actual data received
		} else if (errno_interrupted()) {
			log_debug("Receiving from mesh stack (N: %s) was interrupted, retrying",
			          mesh_stack->name);
		} else if (errno_would_block()) {
			log_debug("Receiving from mesh stack (N: %s) would block, retrying",
			          mesh_stack->name);
		} else if (errno_connection_reset()) {
			log_info("Mesh stack (N: %s) disconnected by peer (connection reset)",
			         mesh_stack->name);

			mesh_stack->cleanup = true;
		} else {
			log_error("Could not receive from mesh stack (N: %s), disconnecting mesh stack: %s (%d)",
			          mesh_stack->name, get_errno_name(errno), errno);

			mesh_stack->cleanup = true;
		}

		return;
	}

	mesh_stack->response_buffer_used += length;

	while (!mesh_stack->cleanup && mesh_stack->response_buffer_used > 0) {
		if (mesh_stack->response_buffer_used < (int)sizeof(MeshPacketHeader)) {
			// wait for complete header
			break;
		}

		// Now we have a complete mesh header.
		if (!mesh_stack->response_header_checked) {
			if (!mesh_packet_header_is_valid_response(&mesh_stack->response_header, &message)) {
				log_error("Received invalid mesh response (packet: %s) from mesh stack (N: %s), disconnecting mesh stack: %s",
				          mesh_packet_get_dump(mesh_packet_dump, mesh_stack->response_buffer, mesh_stack->response_buffer_used),
				          mesh_stack->name, message);

				mesh_stack->cleanup = true;

				return;
			}

			mesh_stack->response_header_checked = true;
		}

		length = mesh_stack->response_header.length;

		// FIXME: add mesh_header->len validation

		if (mesh_stack->response_buffer_used < length) {
			// wait for complete packet
			break;
		}

		mesh_pkt_type = mesh_stack->response_header.type;

		// Handle mesh hello packet.
		if (mesh_pkt_type == MESH_PACKET_TYPE_HELLO) {
			hello_recv_handler(mesh_stack);
		}
		// Handle heart beat ping packet.
		else if (mesh_pkt_type == MESH_PACKET_TYPE_HEART_BEAT_PING) {
			hb_ping_recv_handler(mesh_stack);
		}
		// Handle heart beat pong packet.
		else if (mesh_pkt_type == MESH_PACKET_TYPE_HEART_BEAT_PONG) {
			hb_pong_recv_handler(mesh_stack);
		}
		// Handle TFP packet.
		else if (mesh_pkt_type == MESH_PACKET_TYPE_PAYLOAD) {
			if (mesh_stack->payload_response.header.length != sizeof(MeshPacketHeader) + mesh_stack->payload_response.payload.header.length) {
				log_error("Received mesh response (packet: %s) with length mismatch (outer: %d != header + inner: %d) from mesh stack (N: %s), disconnecting mesh stack",
				          mesh_packet_get_dump(mesh_packet_dump, mesh_stack->response_buffer, mesh_stack->response_buffer_used),
				          mesh_stack->payload_response.header.length,
				          (int)sizeof(MeshPacketHeader) + mesh_stack->payload_response.payload.header.length,
				          mesh_stack->name);

				mesh_stack->cleanup = true;

				return;
			}

			if (!packet_header_is_valid_response(&mesh_stack->payload_response.payload.header, &message)) {
				log_error("Received invalid response (packet: %s) from mesh stack (N: %s), disconnecting mesh stack: %s",
				          packet_get_dump(packet_dump, &mesh_stack->payload_response.payload, mesh_stack->response_buffer_used - sizeof(MeshPacketHeader)),
				          mesh_stack->name,
				          message);

				mesh_stack->cleanup = true;

				return;
			}

			tfp_recv_handler(mesh_stack);
		}
		// Packet type is unknown.
		else {
			log_error("Unknown mesh packet (packet: %s) type received: %d",
			          mesh_packet_get_dump(mesh_packet_dump, mesh_stack->response_buffer, mesh_stack->response_buffer_used),
			          mesh_pkt_type);
		}

		memmove(mesh_stack->response_buffer, mesh_stack->response_buffer + length,
		        mesh_stack->response_buffer_used - length);

		mesh_stack->response_buffer_used -= length;
		mesh_stack->response_header_checked = false;
	}
}

static void timer_wait_hello_handler(void *opaque) {
	MeshStack *mesh_stack = (MeshStack *)opaque;

	log_warn("Wait hello timed out, destroying mesh stack (N: %s)",
	         mesh_stack->name);

	// FIXME: don't send reset, just close the connection and destroy the stack immediately

	broadcast_reset_packet(mesh_stack);

	/*
	 * Schedule a cleanup of the stack after a certain delay.
	 *
	 * This is to make sure the the reset stack packet is received
	 * by all the nodes.
	 */
	arm_timer_cleanup_after_reset_sent(mesh_stack);
}

static void timer_cleanup_after_reset_sent_handler(void *opaque) {
	MeshStack *mesh_stack = (MeshStack *)opaque;

	log_debug("Cleaning up mesh stack (N: %s)", mesh_stack->name);

	mesh_stack->cleanup = true;
}

void timer_hb_do_ping_handler(void *opaque) {
	MeshStack *mesh_stack = (MeshStack *)opaque;
	MeshHeartBeatPacket pkt_mesh_hb;
	char mesh_packet_dump[MESH_PACKET_MAX_DUMP_LENGTH];

	memset(&pkt_mesh_hb, 0, sizeof(MeshHeartBeatPacket));
	mesh_packet_header_create(&pkt_mesh_hb.header,
	                             // Direction.
	                             MESH_PACKET_DIRECTION_DOWNWARD,
	                             // P2P.
	                             false,
	                             // ESP mesh payload protocol.
	                             MESH_PACKET_PROTOCOL_BINARY,
	                             // Length of the mesh packet.
	                             sizeof(MeshHeartBeatPacket),
	                             // Destination address.
	                             mesh_stack->root_node_addr,
	                             // Source address.
	                             mesh_stack->gw_addr,
	                             MESH_PACKET_TYPE_HEART_BEAT_PING);

	log_debug("Sending ping (packet: %s) to mesh root node of mesh stack (N: %s)",
	          mesh_packet_get_dump(mesh_packet_dump, (uint8_t *)&pkt_mesh_hb, pkt_mesh_hb.header.length),
	          mesh_stack->name);

	// TODO: Integrate buffered IO write.
	if (socket_send(mesh_stack->sock, &pkt_mesh_hb, pkt_mesh_hb.header.length) < 0) {
		log_error("Failed to send ping to mesh root node, cleaning up mesh stack (N: %s)",
		          mesh_stack->name);

		mesh_stack->cleanup = true;
	} else {
		log_debug("Arming wait pong timer for mesh stack (N: %s)",
		          mesh_stack->name);

		if (timer_configure(&mesh_stack->timer_hb_wait_pong,
		                    TIME_HB_WAIT_PONG, 0) < 0) {
			log_error("Failed to arm wait pong timer of mesh stack (N: %s), cleaning up the mesh stack",
			          mesh_stack->name);

			mesh_stack->cleanup = true;

			return;
		}
	}
}

void timer_hb_wait_pong_handler(void *opaque) {
	MeshStack *mesh_stack = (MeshStack *)opaque;

	log_warn("Wait pong timed out, cleaning up mesh stack (N: %s)",
	         mesh_stack->name);

	mesh_stack->cleanup = true;
}

void hello_recv_handler(MeshStack *mesh_stack) {
	char prefix_str[17];
	MeshHelloPacket *pkt_mesh_hello = &mesh_stack->hello_response;
	char mesh_packet_dump[MESH_PACKET_MAX_DUMP_LENGTH];

	log_debug("Received mesh packet (T: HELLO, L: %d, packet: %s) from mesh stack (N: %s)",
	          pkt_mesh_hello->header.length,
	          mesh_packet_get_dump(mesh_packet_dump, (uint8_t *)pkt_mesh_hello, pkt_mesh_hello->header.length),
	          mesh_stack->name);

	timer_configure(&mesh_stack->timer_wait_hello, 0, 0);

	if (pkt_mesh_hello->is_root_node) {
		memset(&prefix_str, 0, sizeof(prefix_str));
		memcpy(&prefix_str, &pkt_mesh_hello->prefix, sizeof(pkt_mesh_hello->prefix));

		log_info("Hello from root mesh node (F: %d.%d.%d, P: %s, G: %02X-%02X-%02X-%02X-%02X-%02X, A: %02X-%02X-%02X-%02X-%02X-%02X, packet: %s)",
		         pkt_mesh_hello->firmware_version[0],
		         pkt_mesh_hello->firmware_version[1],
		         pkt_mesh_hello->firmware_version[2],
		         (char *)&prefix_str,
		         pkt_mesh_hello->group_id[0],
		         pkt_mesh_hello->group_id[1],
		         pkt_mesh_hello->group_id[2],
		         pkt_mesh_hello->group_id[3],
		         pkt_mesh_hello->group_id[4],
		         pkt_mesh_hello->group_id[5],
		         pkt_mesh_hello->header.src_addr[0],
		         pkt_mesh_hello->header.src_addr[1],
		         pkt_mesh_hello->header.src_addr[2],
		         pkt_mesh_hello->header.src_addr[3],
		         pkt_mesh_hello->header.src_addr[4],
		         pkt_mesh_hello->header.src_addr[5],
		         mesh_packet_get_dump(mesh_packet_dump, (uint8_t *)pkt_mesh_hello, pkt_mesh_hello->header.length));

		if (!hello_root_recv_handler(mesh_stack)) {
			return;
		}
	} else {
		memset(&prefix_str, 0, sizeof(prefix_str));
		memcpy(&prefix_str, &pkt_mesh_hello->prefix, sizeof(pkt_mesh_hello->prefix));

		log_info("Hello from non-root mesh node (F: %d.%d.%d, P: %s, G: %02X-%02X-%02X-%02X-%02X-%02X, A: %02X-%02X-%02X-%02X-%02X-%02X, packet: %s)",
		         pkt_mesh_hello->firmware_version[0],
		         pkt_mesh_hello->firmware_version[1],
		         pkt_mesh_hello->firmware_version[2],
		         (char *)&prefix_str,
		         pkt_mesh_hello->group_id[0],
		         pkt_mesh_hello->group_id[1],
		         pkt_mesh_hello->group_id[2],
		         pkt_mesh_hello->group_id[3],
		         pkt_mesh_hello->group_id[4],
		         pkt_mesh_hello->group_id[5],
		         pkt_mesh_hello->header.src_addr[0],
		         pkt_mesh_hello->header.src_addr[1],
		         pkt_mesh_hello->header.src_addr[2],
		         pkt_mesh_hello->header.src_addr[3],
		         pkt_mesh_hello->header.src_addr[4],
		         pkt_mesh_hello->header.src_addr[5],
		         mesh_packet_get_dump(mesh_packet_dump, (uint8_t *)pkt_mesh_hello, pkt_mesh_hello->header.length));

		if (!hello_non_root_recv_handler(mesh_stack)) {
			return;
		}
	}
}

void tfp_recv_handler(MeshStack *mesh_stack) {
	uint64_t mesh_src_addr = 0;
	MeshPayloadPacket *pkt_mesh_tfp = &mesh_stack->payload_response;
	char mesh_packet_dump[MESH_PACKET_MAX_DUMP_LENGTH];

	// FIXME: the stack is not fully initialized until the hello packet is received
	if (mesh_stack->state != MESH_STACK_STATE_OPERATIONAL) {
		log_warn("Dropping mesh packet (T: TFP, L: %d, A: %02X-%02X-%02X-%02X-%02X-%02X, packet: %s), because mesh stack (N: %s) is not operational yet",
		         pkt_mesh_tfp->header.length,
		         pkt_mesh_tfp->header.src_addr[0],
		         pkt_mesh_tfp->header.src_addr[1],
		         pkt_mesh_tfp->header.src_addr[2],
		         pkt_mesh_tfp->header.src_addr[3],
		         pkt_mesh_tfp->header.src_addr[4],
		         pkt_mesh_tfp->header.src_addr[5],
		         mesh_packet_get_dump(mesh_packet_dump, (uint8_t *)pkt_mesh_tfp, pkt_mesh_tfp->header.length),
		         mesh_stack->name);

		return;
	}

	log_debug("Received mesh packet (T: TFP, L: %d, A: %02X-%02X-%02X-%02X-%02X-%02X, packet: %s)",
	          pkt_mesh_tfp->header.length,
	          pkt_mesh_tfp->header.src_addr[0],
	          pkt_mesh_tfp->header.src_addr[1],
	          pkt_mesh_tfp->header.src_addr[2],
	          pkt_mesh_tfp->header.src_addr[3],
	          pkt_mesh_tfp->header.src_addr[4],
	          pkt_mesh_tfp->header.src_addr[5],
	          mesh_packet_get_dump(mesh_packet_dump, (uint8_t *)pkt_mesh_tfp, pkt_mesh_tfp->header.length));

	memcpy(&mesh_src_addr,
	       &pkt_mesh_tfp->header.src_addr,
	       sizeof(pkt_mesh_tfp->header.src_addr));

	if (stack_add_recipient(&mesh_stack->base, pkt_mesh_tfp->payload.header.uid, mesh_src_addr) < 0) {
		log_error("Failed to add recipient to mesh stack");

		return;
	}

	network_dispatch_response(&pkt_mesh_tfp->payload);

	log_debug("TFP packet dispatched (L: %d)", pkt_mesh_tfp->payload.header.length);
}

void mesh_stack_destroy(MeshStack *mesh_stack) {
	// Disable all running timers.
	timer_configure(&mesh_stack->timer_wait_hello, 0, 0);
	timer_configure(&mesh_stack->timer_hb_do_ping, 0, 0);
	timer_configure(&mesh_stack->timer_hb_wait_pong, 0, 0);
	timer_configure(&mesh_stack->timer_cleanup_after_reset_sent, 0, 0);

	// Cleanup the timers of the mesh stack.
	timer_destroy(&mesh_stack->timer_wait_hello);
	timer_destroy(&mesh_stack->timer_hb_do_ping);
	timer_destroy(&mesh_stack->timer_hb_wait_pong);
	timer_destroy(&mesh_stack->timer_cleanup_after_reset_sent);

	event_remove_source(mesh_stack->sock->handle, EVENT_SOURCE_TYPE_GENERIC);

	socket_destroy(mesh_stack->sock);
	free(mesh_stack->sock);

	if (mesh_stack->state == MESH_STACK_STATE_OPERATIONAL) {
		stack_announce_disconnect(&mesh_stack->base);
		hardware_remove_stack(&mesh_stack->base);
		stack_destroy(&mesh_stack->base);
	}

	if (mesh_stack->state == MESH_STACK_STATE_WAIT_HELLO) {
		log_info("Mesh stack %s released (S: WAIT_HELLO)",
		         mesh_stack->name);
	} else if (mesh_stack->state == MESH_STACK_STATE_OPERATIONAL) {
		log_info("Mesh stack %s released (S: OPERATIONAL)",
		         mesh_stack->name);
	} else {
		log_info("Mesh stack %s released (S: UNKNOWN)",
		         mesh_stack->name);
	}
}

int mesh_stack_create(char *name, Socket *sock) {
	MeshStack *mesh_stack = NULL;
	mesh_stack = array_append(&mesh_stacks);

	if (mesh_stack == NULL) {
		log_error("Could not append to mesh stacks array: %s (%d)",
		          get_errno_name(errno),
		          errno);

		return -1;
	}

	/*
	 * Already set stack state so in case the event registration fails error can
	 * be reported with current stack state.
	 */
	mesh_stack->state = MESH_STACK_STATE_WAIT_HELLO;

	if (event_add_source(sock->handle, EVENT_SOURCE_TYPE_GENERIC, "mesh-stack",
	                     EVENT_READ, mesh_stack_recv_handler, mesh_stack) < 0) {
		log_error("Failed to add stack receive event");

		array_remove(&mesh_stacks,
		             mesh_stacks.count - 1,
		             (ItemDestroyFunction)mesh_stack_destroy);

		return -1;
	}

	// Initialise the mesh stack.
	mesh_stack->sock = sock;
	mesh_stack->cleanup = false;
	mesh_stack->response_buffer_used = 0;
	mesh_stack->response_header_checked = false;

	snprintf(mesh_stack->name, sizeof(mesh_stack->name), "%s", name);

	// Initialise timers.
	if (timer_create_(&mesh_stack->timer_wait_hello, timer_wait_hello_handler, mesh_stack) < 0) {
		log_error("Failed to initialise wait hello timer: %s (%d)",
		          get_errno_name(errno),
		          errno);

		array_remove(&mesh_stacks,
		             mesh_stacks.count - 1,
		             (ItemDestroyFunction)mesh_stack_destroy);

		return -1;
	}

	if (timer_create_(&mesh_stack->timer_hb_do_ping, timer_hb_do_ping_handler, mesh_stack) < 0) {
		log_error("Failed to initialise do ping timer: %s (%d)",
		          get_errno_name(errno),
		          errno);

		array_remove(&mesh_stacks,
		             mesh_stacks.count - 1,
		             (ItemDestroyFunction)mesh_stack_destroy);

		return -1;
	}

	if (timer_create_(&mesh_stack->timer_hb_wait_pong, timer_hb_wait_pong_handler, mesh_stack) < 0) {
		log_error("Failed to initialise wait pong timer: %s (%d)",
		          get_errno_name(errno),
		          errno);

		array_remove(&mesh_stacks,
		             mesh_stacks.count - 1,
		             (ItemDestroyFunction)mesh_stack_destroy);

		return -1;
	}

	if (timer_create_(&mesh_stack->timer_cleanup_after_reset_sent,
	                  timer_cleanup_after_reset_sent_handler,
	                  mesh_stack) < 0) {
		log_error("Failed to initialise cleanup after reset sent timer: %s (%d)",
		          get_errno_name(errno),
		          errno);

		array_remove(&mesh_stacks,
		             mesh_stacks.count - 1,
		             (ItemDestroyFunction)mesh_stack_destroy);

		return -1;
	}

	// Initially disable all the timers.
	timer_configure(&mesh_stack->timer_wait_hello, 0, 0);
	timer_configure(&mesh_stack->timer_hb_do_ping, 0, 0);
	timer_configure(&mesh_stack->timer_hb_wait_pong, 0, 0);
	timer_configure(&mesh_stack->timer_cleanup_after_reset_sent, 0, 0);

	if (timer_configure(&mesh_stack->timer_wait_hello, TIME_WAIT_HELLO, 0) < 0) {
		log_error("Failed to start wait hello timer: %s (%d)",
		          get_errno_name(errno),
		          errno);

		array_remove(&mesh_stacks,
		             mesh_stacks.count - 1,
		             (ItemDestroyFunction)mesh_stack_destroy);

		return -1;
	}

	log_debug("Mesh stack is waiting for hello packet (N: %s)", mesh_stack->name);

	return 0;
}

void hb_ping_recv_handler(MeshStack *mesh_stack) {
	MeshHeartBeatPacket pkt_mesh_hb_pong;
	MeshHeartBeatPacket *pkt_mesh_hb_ping = &mesh_stack->heart_beat_response;
	char mesh_packet_dump[MESH_PACKET_MAX_DUMP_LENGTH];

	log_debug("Received mesh ping packet (T: PING, L: %d, A: %02X-%02X-%02X-%02X-%02X-%02X, packet: %s)",
	          pkt_mesh_hb_ping->header.length,
	          pkt_mesh_hb_ping->header.src_addr[0],
	          pkt_mesh_hb_ping->header.src_addr[1],
	          pkt_mesh_hb_ping->header.src_addr[2],
	          pkt_mesh_hb_ping->header.src_addr[3],
	          pkt_mesh_hb_ping->header.src_addr[4],
	          pkt_mesh_hb_ping->header.src_addr[5],
	          mesh_packet_get_dump(mesh_packet_dump, (uint8_t *)pkt_mesh_hb_ping, pkt_mesh_hb_ping->header.length));

	memcpy(&pkt_mesh_hb_pong, pkt_mesh_hb_ping, sizeof(MeshHeartBeatPacket));
	mesh_packet_header_set_direction(&pkt_mesh_hb_pong.header, MESH_PACKET_DIRECTION_DOWNWARD);
	memcpy(&pkt_mesh_hb_pong.header.dst_addr, &pkt_mesh_hb_ping->header.src_addr, sizeof(pkt_mesh_hb_pong.header.src_addr));
	memcpy(&pkt_mesh_hb_pong.header.src_addr, &mesh_stack->gw_addr, sizeof(mesh_stack->gw_addr));

	pkt_mesh_hb_pong.header.type = MESH_PACKET_TYPE_HEART_BEAT_PONG;

	// TODO: Integrate buffered IO write.
	if (socket_send(mesh_stack->sock, &pkt_mesh_hb_pong, pkt_mesh_hb_pong.header.length) < 0) {
		log_error("Failed to send mesh pong packet");
	} else {
		log_debug("Sent mesh pong packet (A: %02X-%02X-%02X-%02X-%02X-%02X, packet: %s)",
		          pkt_mesh_hb_pong.header.dst_addr[0],
		          pkt_mesh_hb_pong.header.dst_addr[1],
		          pkt_mesh_hb_pong.header.dst_addr[2],
		          pkt_mesh_hb_pong.header.dst_addr[3],
		          pkt_mesh_hb_pong.header.dst_addr[4],
		          pkt_mesh_hb_pong.header.dst_addr[5],
		          mesh_packet_get_dump(mesh_packet_dump, (uint8_t *)&pkt_mesh_hb_pong, pkt_mesh_hb_pong.header.length));
	}
}

void hb_pong_recv_handler(MeshStack *mesh_stack) {
	MeshHeartBeatPacket *pkt_mesh_hb = &mesh_stack->heart_beat_response;
	char mesh_packet_dump[MESH_PACKET_MAX_DUMP_LENGTH];

	timer_configure(&mesh_stack->timer_hb_wait_pong, 0, 0);

	log_debug("Received mesh pong packet (T: PONG, L: %d, A: %02X-%02X-%02X-%02X-%02X-%02X, packet: %s)",
	          pkt_mesh_hb->header.length,
	          pkt_mesh_hb->header.src_addr[0],
	          pkt_mesh_hb->header.src_addr[1],
	          pkt_mesh_hb->header.src_addr[2],
	          pkt_mesh_hb->header.src_addr[3],
	          pkt_mesh_hb->header.src_addr[4],
	          pkt_mesh_hb->header.src_addr[5],
	          mesh_packet_get_dump(mesh_packet_dump, (uint8_t *)pkt_mesh_hb, pkt_mesh_hb->header.length));
}

void arm_timer_hb_do_ping(MeshStack *mesh_stack) {
	if (timer_configure(&mesh_stack->timer_hb_do_ping, 0, TIME_HB_DO_PING) < 0) {
		log_error("Failed to arm do ping timer (N: %s), cleaning up the mesh stack",
		          mesh_stack->name);

		mesh_stack->cleanup = true;

		return;
	}

	log_debug("Do ping timer armed (N: %s)", mesh_stack->name);
}

void broadcast_reset_packet(MeshStack *mesh_stack) {
	MeshResetPacket pkt_mesh_reset;
	uint8_t addr[ESP_MESH_ADDRESS_LEN];
	char mesh_packet_dump[MESH_PACKET_MAX_DUMP_LENGTH];

	memset(&addr, 0, sizeof(addr));
	memset(&pkt_mesh_reset, 0, sizeof(MeshResetPacket));
	mesh_packet_header_create(&pkt_mesh_reset.header,
	                             // Direction.
	                             MESH_PACKET_DIRECTION_DOWNWARD,
	                             // P2P.
	                             false,
	                             // ESP mesh payload protocol.
	                             MESH_PACKET_PROTOCOL_BINARY,
	                             // Length of the mesh packet.
	                             sizeof(MeshResetPacket),
	                             // Destination address.
	                             addr,
	                             // Source address.
	                             addr,
	                             MESH_PACKET_TYPE_RESET);

	// TODO: Integrate buffered IO write.
	if (socket_send(mesh_stack->sock, &pkt_mesh_reset, pkt_mesh_reset.header.length) < 0) {
		log_error("Failed to send broadcast reset stack packet (packet: %s)",
		          mesh_packet_get_dump(mesh_packet_dump, (uint8_t *)&pkt_mesh_reset, pkt_mesh_reset.header.length));
	} else {
		log_debug("Broadcast reset stack packet sent (packet: %s)",
		          mesh_packet_get_dump(mesh_packet_dump, (uint8_t *)&pkt_mesh_reset, pkt_mesh_reset.header.length));
	}
}

bool hello_root_recv_handler(MeshStack *mesh_stack) {
	char prefix_str[17];
	MeshOllehPacket olleh_mesh_pkt;
	MeshStack *mesh_stack_from_list = NULL;
	MeshHelloPacket *hello_mesh_pkt = &mesh_stack->hello_response;
	int i;
	char mesh_packet_dump[MESH_PACKET_MAX_DUMP_LENGTH];

#ifdef BRICKD_WITH_MESH_SINGLE_ROOT_NODE
	/*
	 * Iterate the list of mesh stacks to check if there is already an
	 * existing mesh stack from the same mesh network. If that is the case then
	 * send reset packet to both of the stack's sockets and clean both of them up.
	 */

	uint64_t gid_from_list = 0;
	uint64_t gid_from_hello_pkt = 0;

	for (i = 0; i < mesh_stacks.count; ++i) {
		gid_from_list = 0;
		gid_from_hello_pkt = 0;
		MeshResetPacket pkt_mesh_reset;

		mesh_stack_from_list = (MeshStack *)array_get(&mesh_stacks, i);

		memcpy(&gid_from_hello_pkt,
		       &hello_mesh_pkt->group_id,
		       sizeof(hello_mesh_pkt->group_id));

		memcpy(&gid_from_list,
		       &mesh_stack_from_list->group_id,
		       sizeof(mesh_stack_from_list->group_id));

		if (gid_from_list == gid_from_hello_pkt) {
			log_warn("Hello from root node of existing mesh stack (G: %02x-%02x-%02x-%02x-%02x-%02x)",
			         hello_mesh_pkt->group_id[0],
			         hello_mesh_pkt->group_id[1],
			         hello_mesh_pkt->group_id[2],
			         hello_mesh_pkt->group_id[3],
			         hello_mesh_pkt->group_id[4],
			         hello_mesh_pkt->group_id[5]);

			// Reset the mesh stack that was found on the list.
			memset(&pkt_mesh_reset, 0, sizeof(MeshResetPacket));
			mesh_packet_header_create(&pkt_mesh_reset.header,
			                             // Direction.
			                             MESH_PACKET_DIRECTION_DOWNWARD,
			                             // P2P.
			                             false,
			                             // ESP mesh payload protocol.
			                             MESH_PACKET_PROTOCOL_BINARY,
			                             // Length of the payload of the mesh packet.
			                             sizeof(MeshResetPacket),
			                             // Destination address.
			                             mesh_stack_from_list->root_node_addr,
			                             // Source address.
			                             hello_mesh_pkt->header.dst_addr,
			                             MESH_PACKET_TYPE_RESET);

			// TODO: Integrate buffered IO write.
			if (socket_send(mesh_stack_from_list->sock, &pkt_mesh_reset, pkt_mesh_reset.header.length) < 0) {
				log_error("Failed to send mesh stack reset packet (A: %02X-%02X-%02X-%02X-%02X-%02X)",
				          mesh_stack_from_list->root_node_addr[0],
				          mesh_stack_from_list->root_node_addr[1],
				          mesh_stack_from_list->root_node_addr[2],
				          mesh_stack_from_list->root_node_addr[3],
				          mesh_stack_from_list->root_node_addr[4],
				          mesh_stack_from_list->root_node_addr[5]);
			} else {
				log_warn("Sent mesh stack reset packet (A: %02X-%02X-%02X-%02X-%02X-%02X)",
				         mesh_stack_from_list->root_node_addr[0],
				         mesh_stack_from_list->root_node_addr[1],
				         mesh_stack_from_list->root_node_addr[2],
				         mesh_stack_from_list->root_node_addr[3],
				         mesh_stack_from_list->root_node_addr[4],
				         mesh_stack_from_list->root_node_addr[5]);
			}

			// Reset the mesh stack from which we just received.
			memset(&pkt_mesh_reset, 0, sizeof(MeshResetPacket));
			mesh_packet_header_create(&pkt_mesh_reset.header,
			                             // Direction.
			                             MESH_PACKET_DIRECTION_DOWNWARD,
			                             // P2P.
			                             false,
			                             // ESP mesh payload protocol.
			                             MESH_PACKET_PROTOCOL_BINARY,
			                             // Length of the mesh packet.
			                             sizeof(MeshResetPacket),
			                             // Destination address.
			                             hello_mesh_pkt->header.src_addr,
			                             // Source address.
			                             hello_mesh_pkt->header.dst_addr,
			                             MESH_PACKET_TYPE_RESET);

			// TODO: Integrate buffered IO write.
			if (socket_send(mesh_stack->sock, &pkt_mesh_reset, pkt_mesh_reset.header.length) < 0) {
				log_error("Failed to send mesh stack reset packet (A: %02X-%02X-%02X-%02X-%02X-%02X)",
				          hello_mesh_pkt->header.src_addr[0],
				          hello_mesh_pkt->header.src_addr[1],
				          hello_mesh_pkt->header.src_addr[2],
				          hello_mesh_pkt->header.src_addr[3],
				          hello_mesh_pkt->header.src_addr[4],
				          hello_mesh_pkt->header.src_addr[5]);
			} else {
				log_warn("Sent mesh stack reset packet (A: %02X-%02X-%02X-%02X-%02X-%02X)",
				         hello_mesh_pkt->header.src_addr[0],
				         hello_mesh_pkt->header.src_addr[1],
				         hello_mesh_pkt->header.src_addr[2],
				         hello_mesh_pkt->header.src_addr[3],
				         hello_mesh_pkt->header.src_addr[4],
				         hello_mesh_pkt->header.src_addr[5]);
			}

			/*
			 * Schedule a cleanup of the stack after a certain delay.
			 *
			 * This is to make sure the the reset stack packet is received
			 * by all the nodes.
			 */
			arm_timer_cleanup_after_reset_sent(mesh_stack);
			arm_timer_cleanup_after_reset_sent(mesh_stack_from_list);

			return false;
		}
	}
#else
	uint64_t src_addr_from_hello_pkt = 0;
	uint64_t root_node_addr_from_existing_stack = 0;

	memcpy(&src_addr_from_hello_pkt,
	       &hello_mesh_pkt->header.src_addr,
	       sizeof(hello_mesh_pkt->header.src_addr));

	for (i = 0; i < mesh_stacks.count; ++i) {
		mesh_stack_from_list = NULL;

		mesh_stack_from_list = (MeshStack *)array_get(&mesh_stacks, i);

		memcpy(&root_node_addr_from_existing_stack,
		       &mesh_stack_from_list->root_node_addr,
		       sizeof(mesh_stack_from_list->root_node_addr));

		/*
		 * Schedule a cleanup if there was already an existing mesh stack from
		 * the same mesh node.
		 */
		if (root_node_addr_from_existing_stack == src_addr_from_hello_pkt) {
			log_debug("Removing previously existing mesh stack");

			mesh_stack_from_list->cleanup = true;
		}
	}
#endif

	// Create a new stack object for the mesh stack.
	// FIXME: creating the stack here is too late. either create the stack earlier
	//        or drop all TFP packets until a hello is received to avoid calling
	//        stack functions (e.g. stack_add_recipient) before the stack object
	//        is created
	if (stack_create(&mesh_stack->base,
	                 mesh_stack->name,
	                 mesh_stack_dispatch_request) < 0) {
		log_error("Failed to create base stack for mesh client %s: %s (%d)",
		          mesh_stack->name,
		          get_errno_name(errno),
		          errno);

		return false;
	}

	// Add to main stacks array.
	if (hardware_add_stack(&mesh_stack->base) < 0) {
		stack_destroy(&mesh_stack->base);

		log_error("Failed to add mesh stack to main stacks array");

		return false;
	}

	// Prepare the olleh packet.
	memset(&olleh_mesh_pkt, 0, sizeof(MeshOllehPacket));
	mesh_packet_header_create(&olleh_mesh_pkt.header,
	                             // Direction.
	                             MESH_PACKET_DIRECTION_DOWNWARD,
	                             // P2P.
	                             false,
	                             // ESP mesh payload protocol.
	                             MESH_PACKET_PROTOCOL_BINARY,
	                             // Length of the mesh packet.
	                             sizeof(MeshOllehPacket),
	                             // Destination address.
	                             hello_mesh_pkt->header.src_addr,
	                             // Source address.
	                             hello_mesh_pkt->header.dst_addr,
	                             MESH_PACKET_TYPE_OLLEH);

	// TODO: Integrate buffered IO write.
	if (socket_send(mesh_stack->sock, &olleh_mesh_pkt, olleh_mesh_pkt.header.length) < 0) {
		log_error("Failed to send mesh olleh packet (A: %02X-%02X-%02X-%02X-%02X-%02X, packet: %s)",
		          olleh_mesh_pkt.header.dst_addr[0],
		          olleh_mesh_pkt.header.dst_addr[1],
		          olleh_mesh_pkt.header.dst_addr[2],
		          olleh_mesh_pkt.header.dst_addr[3],
		          olleh_mesh_pkt.header.dst_addr[4],
		          olleh_mesh_pkt.header.dst_addr[5],
		          mesh_packet_get_dump(mesh_packet_dump, (uint8_t *)&olleh_mesh_pkt, olleh_mesh_pkt.header.length));

		return false;
	}

	log_debug("Olleh packet sent (packet: %s)",
	          mesh_packet_get_dump(mesh_packet_dump, (uint8_t *)&olleh_mesh_pkt, olleh_mesh_pkt.header.length));

	// Update mesh stack parameters.
	memcpy(&mesh_stack->prefix,
	       &hello_mesh_pkt->prefix,
	       sizeof(hello_mesh_pkt->prefix));

	memcpy(&mesh_stack->group_id,
	       &hello_mesh_pkt->group_id,
	       sizeof(hello_mesh_pkt->group_id));

	memcpy(&mesh_stack->root_node_firmware_version,
	       &hello_mesh_pkt->firmware_version,
	       sizeof(hello_mesh_pkt->firmware_version));

	memcpy(&mesh_stack->root_node_addr,
	       &hello_mesh_pkt->header.src_addr,
	       sizeof(hello_mesh_pkt->header.src_addr));

	memcpy(&mesh_stack->gw_addr,
	       &hello_mesh_pkt->header.dst_addr,
	       sizeof(hello_mesh_pkt->header.dst_addr));

	mesh_stack->state = MESH_STACK_STATE_OPERATIONAL;

	memset(&prefix_str, 0, sizeof(prefix_str));
	memcpy(&prefix_str, &hello_mesh_pkt->prefix, sizeof(hello_mesh_pkt->prefix));

	log_info("Mesh stack %s changed state to operational (F: %d.%d.%d, P: %s, G: %02X:%02X:%02X:%02X:%02X:%02X, packet: %s)",
	         mesh_stack->name,
	         hello_mesh_pkt->firmware_version[0],
	         hello_mesh_pkt->firmware_version[1],
	         hello_mesh_pkt->firmware_version[2],
	         (char *)&prefix_str,
	         hello_mesh_pkt->group_id[0],
	         hello_mesh_pkt->group_id[1],
	         hello_mesh_pkt->group_id[2],
	         hello_mesh_pkt->group_id[3],
	         hello_mesh_pkt->group_id[4],
	         hello_mesh_pkt->group_id[5],
	         mesh_packet_get_dump(mesh_packet_dump, (uint8_t *)hello_mesh_pkt, hello_mesh_pkt->header.length));

	arm_timer_hb_do_ping(mesh_stack);

	return true;
}

int mesh_stack_dispatch_request(Stack *stack, Packet *request, Recipient *recipient) {
	int ret = 0;
	bool is_broadcast = true;
	MeshPayloadPacket tfp_mesh_pkt;
	char base58[BASE58_MAX_LENGTH];
	uint8_t dst_addr[ESP_MESH_ADDRESS_LEN];
	MeshStack *mesh_stack = (MeshStack *)stack;
	char mesh_packet_dump[MESH_PACKET_MAX_DUMP_LENGTH];

	memset(&dst_addr, 0, sizeof(dst_addr));

	// Unicast.
	if (recipient != NULL) {
		is_broadcast = false;

		memcpy(&dst_addr, &recipient->opaque, sizeof(dst_addr));
	}

	memset(&tfp_mesh_pkt, 0, sizeof(MeshPayloadPacket));
	mesh_packet_header_create(&tfp_mesh_pkt.header,
	                             // Direction.
	                             MESH_PACKET_DIRECTION_DOWNWARD,
	                             // P2P.
	                             false,
	                             // ESP mesh payload protocol.
	                             MESH_PACKET_PROTOCOL_BINARY,
	                             // Length of the mesh packet.
	                             sizeof(MeshPacketHeader) + request->header.length,
	                             // Destination address.
	                             dst_addr,
	                             // Source address.
	                             mesh_stack->gw_addr,
	                             MESH_PACKET_TYPE_PAYLOAD);

	memcpy(&tfp_mesh_pkt.payload, request, request->header.length);

	if (!is_broadcast) {
		memset(&base58, 0, sizeof(base58));
		base58_encode(base58, uint32_from_le(recipient->uid));
	}

	// TODO: Integrate buffered IO write.
	ret = socket_send(mesh_stack->sock, &tfp_mesh_pkt, tfp_mesh_pkt.header.length);

	if (ret < 0) {
		if (is_broadcast) {
			log_error("Failed to send TFP packet to mesh (E: %d, L: %d, B: %d, packet: %s)",
			          ret,
			          request->header.length,
			          is_broadcast,
			          mesh_packet_get_dump(mesh_packet_dump, (uint8_t *)&tfp_mesh_pkt, tfp_mesh_pkt.header.length));
		} else {
			log_error("Failed to send TFP packet to mesh (E: %d, U: %s, L: %d, B: %d, A: %02X-%02X-%02X-%02X-%02X-%02X, packet: %s)",
			          ret,
			          base58,
			          request->header.length,
			          is_broadcast,
			          dst_addr[0],
			          dst_addr[1],
			          dst_addr[2],
			          dst_addr[3],
			          dst_addr[4],
			          dst_addr[5],
			          mesh_packet_get_dump(mesh_packet_dump, (uint8_t *)&tfp_mesh_pkt, tfp_mesh_pkt.header.length));
		}

		log_debug("Marking mesh stack for cleanup (N: %s)", mesh_stack->name);
		mesh_stack->cleanup = true;

		return -1;
	} else {
		if (is_broadcast) {
			log_debug("TFP packet sent to mesh (L: %d, B: %d, packet: %s)",
			          request->header.length,
			          is_broadcast,
			          mesh_packet_get_dump(mesh_packet_dump, (uint8_t *)&tfp_mesh_pkt, tfp_mesh_pkt.header.length));
		} else {
			log_debug("TFP packet sent to mesh (U: %s, L: %d, B: %d, A: %02X-%02X-%02X-%02X-%02X-%02X, packet: %s)",
			          base58,
			          request->header.length,
			          is_broadcast,
			          dst_addr[0],
			          dst_addr[1],
			          dst_addr[2],
			          dst_addr[3],
			          dst_addr[4],
			          dst_addr[5],
			          mesh_packet_get_dump(mesh_packet_dump, (uint8_t *)&tfp_mesh_pkt, tfp_mesh_pkt.header.length));
		}
	}

	return 0;
}

void arm_timer_cleanup_after_reset_sent(MeshStack *mesh_stack) {
	if (timer_configure(&mesh_stack->timer_cleanup_after_reset_sent,
	                    TIME_CLEANUP_AFTER_RESET_SENT,
	                    0) < 0) {
		log_warn("Failed to arm stack cleanup timer (N: %s)", mesh_stack->name);

		mesh_stack->cleanup = true;

		return;
	}

	log_debug("Stack cleanup timer armed (N: %s)", mesh_stack->name);
}

bool hello_non_root_recv_handler(MeshStack *mesh_stack) {
	MeshOllehPacket olleh_mesh_pkt;
	MeshHelloPacket *hello_mesh_pkt = &mesh_stack->hello_response;
	char mesh_packet_dump[MESH_PACKET_MAX_DUMP_LENGTH];

	// Prepare the olleh packet.
	memset(&olleh_mesh_pkt, 0, sizeof(MeshOllehPacket));
	mesh_packet_header_create(&olleh_mesh_pkt.header,
	                             // Direction.
	                             MESH_PACKET_DIRECTION_DOWNWARD,
	                             // P2P.
	                             false,
	                             // ESP mesh payload protocol.
	                             MESH_PACKET_PROTOCOL_BINARY,
	                             // Length of the mesh packet.
	                             sizeof(MeshOllehPacket),
	                             // Destination address.
	                             hello_mesh_pkt->header.src_addr,
	                             // Source address.
	                             mesh_stack->gw_addr,
	                             MESH_PACKET_TYPE_OLLEH);

	// TODO: Integrate buffered IO write.
	if (socket_send(mesh_stack->sock, &olleh_mesh_pkt, olleh_mesh_pkt.header.length) < 0) {
		log_error("Olleh packet send failed (A: %02X-%02X-%02X-%02X-%02X-%02X, packet: %s)",
		          hello_mesh_pkt->header.src_addr[0],
		          hello_mesh_pkt->header.src_addr[1],
		          hello_mesh_pkt->header.src_addr[2],
		          hello_mesh_pkt->header.src_addr[3],
		          hello_mesh_pkt->header.src_addr[4],
		          hello_mesh_pkt->header.src_addr[5],
		          mesh_packet_get_dump(mesh_packet_dump, (uint8_t *)&olleh_mesh_pkt, olleh_mesh_pkt.header.length));

		return false;
	}

	log_debug("Olleh packet sent (A: %02X-%02X-%02X-%02X-%02X-%02X, packet: %s)",
	          hello_mesh_pkt->header.src_addr[0],
	          hello_mesh_pkt->header.src_addr[1],
	          hello_mesh_pkt->header.src_addr[2],
	          hello_mesh_pkt->header.src_addr[3],
	          hello_mesh_pkt->header.src_addr[4],
	          hello_mesh_pkt->header.src_addr[5],
	          mesh_packet_get_dump(mesh_packet_dump, (uint8_t *)&olleh_mesh_pkt, olleh_mesh_pkt.header.length));

	return true;
}
