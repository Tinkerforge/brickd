/*
 * brickd
 * Copyright (C) 2016 Ishraq Ibne Ashraf <ishraq@tinkerforge.com>
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
#include <string.h>
#include <stdlib.h>

#include <daemonlib/array.h>
#include <daemonlib/log.h>
#include <daemonlib/utils.h>
#include <daemonlib/event.h>
#include <daemonlib/base58.h>

#include "hardware.h"
#include "network.h"
#include "mesh_stack.h"

Array mesh_stacks;
static LogSource _log_source = LOG_SOURCE_INITIALIZER;

static void handle_mesh_client_socket_incoming(void *opaque) {
  int read_len = 0;
  uint8_t mesh_pkt_type = 0;
  esp_mesh_header_t *mesh_header = NULL;
  pkt_mesh_hello_t *hello_mesh_pkt = NULL;
  MeshStack *mesh_stack = (MeshStack *)opaque;

  read_len = io_read(mesh_stack->io,
                     (uint8_t *)&mesh_stack->incoming_buffer + mesh_stack->incoming_buffer_used,
                     sizeof(mesh_stack->incoming_buffer) - mesh_stack->incoming_buffer_used);

  if(read_len == 0) {
    /*
     * Mark the stack for cleanup.
     *
     * Actual cleanup will be done after this event handler callback has returned.
     */
    mesh_stack->cleanup = true;

    log_info("Mesh client disconnected (N: %s)", mesh_stack->name);

    return;
  }

  if(read_len < 0) {
    if (read_len == IO_CONTINUE) {
      log_debug("No actual data received");
		}
    else if (errno_interrupted()) {
			log_debug("Receiving interrupted, retrying");
		}
    else if (errno_would_block()) {
			log_debug("Receiving would block, retrying");
		}
    else {
			log_error("Could not receive from mesh client, disconnecting client (N: %s)",
                mesh_stack->name);

      mesh_stack->cleanup = true;
		}

    return;
  }

  mesh_stack->incoming_buffer_used += read_len;

  while(!mesh_stack->cleanup && mesh_stack->incoming_buffer_used > 0) {
    if (mesh_stack->incoming_buffer_used < (int)sizeof(esp_mesh_header_t)) {
      // Wait for complete mesh header.
      log_debug("Waiting for complete mesh header");

      break;
    }

    // Now we have a complete mesh header.
    mesh_header = (esp_mesh_header_t *)&mesh_stack->incoming_buffer;

    if(!mesh_stack->mesh_header_checked) {
      if(!is_mesh_header_valid(mesh_header)) {
        log_error("Received invalid mesh header, disconnecting mesh client (N: %s)",
                  mesh_stack->name);

        mesh_stack->cleanup = true;

        return;
      }

      mesh_stack->mesh_header_checked = true;
    }

    read_len = mesh_header->len;

    if (mesh_stack->incoming_buffer_used < read_len) {
      // Wait for complete packet.
      log_debug("Waiting for complete mesh packet");

      break;
    }

    if(mesh_header->proto.flag_protocol != ESP_MESH_PAYLOAD_BIN) {
      log_error("ESP mesh payload is not of binary type");
    }
    else {
      // Handle the received packet based on packet type.
      mesh_pkt_type = mesh_stack->incoming_buffer[sizeof(esp_mesh_header_t)];

      if(mesh_pkt_type == MESH_PACKET_HELLO) {
        log_debug("Received mesh packet (T: HELLO, L: %d)", mesh_header->len);

        hello_mesh_pkt = (pkt_mesh_hello_t *)mesh_stack->incoming_buffer;

        if(hello_mesh_pkt->is_root_node) {
          log_info("Hello from root mesh node "FMT_DBG_RECV_HELLO"",
                   hello_mesh_pkt->firmware_version[0],
                   hello_mesh_pkt->firmware_version[1],
                   hello_mesh_pkt->firmware_version[2],
                   (char *)&hello_mesh_pkt->prefix,
                   hello_mesh_pkt->group_id[0],
                   hello_mesh_pkt->group_id[1],
                   hello_mesh_pkt->group_id[2],
                   hello_mesh_pkt->group_id[3],
                   hello_mesh_pkt->group_id[4],
                   hello_mesh_pkt->group_id[5]);

          if(!tfp_mesh_tfp_hello_root_recv_handler(mesh_stack)) {
            mesh_stack->cleanup = true;

            return;
          }
        }
        else {
          log_info("Hello from non-root mesh node");

          if(!tfp_mesh_tfp_hello_non_root_recv_handler(mesh_stack)) {
            mesh_stack->cleanup = true;

            return;
          }
        }
      }
      else if(mesh_pkt_type == MESH_PACKET_OLLEH) {
        // Currently not relevant for brickd.
        log_debug("Received mesh packet (T: OLLEH, L: %d)", mesh_header->len);
      }
      else if(mesh_pkt_type == MESH_PACKET_TFP) {
        log_debug("Received mesh packet (T: TFP, L: %d)", mesh_header->len);

        tfp_mesh_tfp_recv_handler(mesh_stack);
      }
      else {
        log_error("Unknown mesh packet type received");
        mesh_stack->cleanup = true;

        return;
      }
    }

    memmove(&mesh_stack->incoming_buffer,
            (uint8_t *)&mesh_stack->incoming_buffer + read_len,
           mesh_stack->incoming_buffer_used - read_len);

    mesh_stack->mesh_header_checked = false;
    mesh_stack->incoming_buffer_used -= read_len;
  }
}

bool is_mesh_header_valid(esp_mesh_header_t *mesh_header) {
  if(mesh_header->len <= 0) {
    log_error("ESP mesh packet header length is zero");

    return false;
  }

  if(mesh_header->proto.flag_direction == 0) {
    log_error("ESP mesh packet header has downward direction");

    return false;
  }

  if(mesh_header->proto.flag_protocol != ESP_MESH_PAYLOAD_BIN) {
    log_error("ESP mesh packet payload type is not binary");

    return false;
  }

  return true;
}

/*
 * Handles packet to be sent into or downwards to a mesh network.
 *
 * The socket communication is with the root node of a mesh network.
 */
int mesh_stack_dispatch_request(Stack *stack, Packet *request, Recipient *recipient) {
  uint8_t dst_addr[ESP_MESH_ADDRESS_LEN];
  bool is_broadcast = true;
  pkt_mesh_tfp_t tfp_mesh_pkt;
  char base58[BASE58_MAX_LENGTH];
  esp_mesh_header_t *mesh_header = NULL;
  MeshStack *mesh_stack = (MeshStack *)stack;

  memset(&base58, 0, sizeof(base58));
  memset(&dst_addr, 0, sizeof(dst_addr));
  memset(&tfp_mesh_pkt, 0, sizeof(pkt_mesh_tfp_t));

  // Unicast.
  if(recipient != NULL) {
    is_broadcast = false;

    memcpy(&dst_addr, &recipient->opaque, sizeof(dst_addr));
  }

  mesh_header = (esp_mesh_header_t *)esp_mesh_get_packet_header(// Direction.
                                                                ESP_MESH_PACKET_DOWNWARDS,
                                                                // P2P.
                                                                false,
                                                                // ESP mesh payload protocol.
                                                                ESP_MESH_PAYLOAD_BIN,
                                                                // Length of the payload of the mesh packet.
                                                                request->header.length + 1,
                                                                // Destination address.
                                                                dst_addr,
                                                                // Source address.
                                                                mesh_stack->gw_addr);

  memcpy(&tfp_mesh_pkt.header, mesh_header, mesh_header->len);
  free(mesh_header);

  tfp_mesh_pkt.type = MESH_PACKET_TFP;

  memcpy(&tfp_mesh_pkt.pkt_tfp, request, request->header.length);

  if(!is_broadcast) {
    base58_encode(base58, uint32_from_le(recipient->uid));
  }

  // TODO: Integrate buffered IO write.
  if(io_write(mesh_stack->io, &tfp_mesh_pkt, tfp_mesh_pkt.header.len) < 0) {

    /*
    * These are debug instead of error because this error condition occures
    * easily for example when flashing. Mainly because there is no way yet
    * implemented to detecti mesh node disconnect.
    */
    if(is_broadcast) {
      log_debug("Mesh dispatch request failed (L: %d, B: %d)",
                request->header.length,
                is_broadcast);
    }
    else {
      log_debug("Mesh dispatch request failed (U: %s, L: %d, B: %d)",
                base58,
                request->header.length,
                is_broadcast);
    }

    return -1;
  }
  else {
    if(is_broadcast) {
      log_info("Mesh dispatch request OK (L: %d, B: %d)",
               request->header.length,
               is_broadcast);
    }
    else {
      log_info("Mesh dispatch request OK (U: %s, L: %d, B: %d)",
               base58,
               request->header.length,
               is_broadcast);
    }
  }

  return 0;
}

int mesh_stack_create(char *name, IO *io) {
  /*
   * Try to allocate a mesh specific stack and store it in the array of
   * mesh stacks.
   */
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

  // Add an event for mesh stack socket's incoming data.
  if(event_add_source(io->handle,
                      EVENT_SOURCE_TYPE_GENERIC,
                      EVENT_READ,
                      handle_mesh_client_socket_incoming,
                      mesh_stack) < 0) {
    log_error("Failed to add socket read event for mesh stack");

    array_remove(&mesh_stacks,
                 mesh_stacks.count - 1,
                 (ItemDestroyFunction)mesh_stack_destroy);

    return -1;
  }

  // Initialise the mesh stack.
  mesh_stack->io = io;
  mesh_stack->cleanup = false;
  mesh_stack->incoming_buffer_used = 0;
  mesh_stack->mesh_header_checked = false;

	snprintf(mesh_stack->name, sizeof(mesh_stack->name), "%s", name);

  log_info("Mesh stack is waiting for hello packet (N: %s)", mesh_stack->name);

  return 0;
}

void mesh_stack_destroy(MeshStack *mesh_stack) {
  // Remove mesh client socket event from event handler.
  event_remove_source(mesh_stack->io->handle,
                      EVENT_SOURCE_TYPE_GENERIC);

  // Remove the socket. Will this also disconnect the socket if it is connected?
  io_destroy(mesh_stack->io);
	free(mesh_stack->io);

  if(mesh_stack->state == MESH_STACK_STATE_OPERATIONAL) {
    /*
     * Announce disconnect for the UIDs that belonged to the mesh stack
     * that is being destroyed.
     */
    stack_announce_disconnect(&mesh_stack->base);

    // Remove the stack from the main stacks array.
    hardware_remove_stack(&mesh_stack->base);

    // Destroy the stack object
    stack_destroy(&mesh_stack->base);
  }

  if(mesh_stack->state == MESH_STACK_STATE_WAIT_HELLO) {
    log_info("Mesh stack %s released which was in waiting for hello state",
             mesh_stack->name);
  }
  else if(mesh_stack->state == MESH_STACK_STATE_OPERATIONAL) {
    log_info("Mesh stack %s released which was in operational state",
             mesh_stack->name);
  }
  else {
    log_info("Mesh stack %s released which was in unknown state",
             mesh_stack->name);
  }
}

bool esp_mesh_packet_init(esp_mesh_header_t *mesh_packet_header,
                          uint8_t flag_version,
                          uint8_t flag_option_exist,
                          uint8_t flag_piggyback_permit,
                          uint8_t flag_piggyback_request,
                          uint8_t flag_reserved,
                          uint8_t flag_direction,
                          uint8_t flag_p2p,
                          uint8_t flag_protocol,
                          uint8_t *mesh_dst_addr,
                          uint8_t *mesh_src_addr) {
  if(mesh_packet_header == NULL || mesh_dst_addr == NULL || mesh_src_addr == NULL) {
    return false;
  }

  mesh_packet_header->flag_version = flag_version;
  mesh_packet_header->flag_option_exist = flag_option_exist;
  mesh_packet_header->flag_piggyback_permit = flag_piggyback_permit;
  mesh_packet_header->flag_piggyback_request = flag_piggyback_request;
  mesh_packet_header->flag_reserved = flag_reserved;
  mesh_packet_header->proto.flag_direction = flag_direction;
  mesh_packet_header->proto.flag_p2p = flag_p2p;
  mesh_packet_header->proto.flag_protocol = flag_protocol;

  memcpy(&mesh_packet_header->dst_addr, mesh_dst_addr, ESP_MESH_ADDRESS_LEN);
  memcpy(&mesh_packet_header->src_addr, mesh_src_addr, ESP_MESH_ADDRESS_LEN);

  return true;
}

bool tfp_mesh_tfp_recv_handler(MeshStack *mesh_stack) {
  uint64_t mesh_src_addr = 0;
  pkt_mesh_tfp_t *tfp_mesh_pkt = (pkt_mesh_tfp_t *)&mesh_stack->incoming_buffer;

  memcpy(&mesh_src_addr,
         &tfp_mesh_pkt->header.src_addr,
         sizeof(tfp_mesh_pkt->header.src_addr));

  if(stack_add_recipient(&mesh_stack->base, tfp_mesh_pkt->pkt_tfp.header.uid, mesh_src_addr) < 0) {
    log_error("Failed to add recipient to mesh stack");

    return false;
  }

  network_dispatch_response((Packet *)&tfp_mesh_pkt->pkt_tfp);

  log_debug("TFP packet dispatched (L: %d)", tfp_mesh_pkt->pkt_tfp.header.length);

  return true;
}

bool tfp_mesh_tfp_hello_root_recv_handler(MeshStack *mesh_stack) {
  uint64_t gid_from_list = 0;
  uint64_t gid_from_hello_pkt = 0;
  pkt_mesh_olleh_t olleh_mesh_pkt;
  esp_mesh_header_t *mesh_header = NULL;
  MeshStack *mesh_stack_from_list = NULL;
  pkt_mesh_hello_t *hello_mesh_pkt = (pkt_mesh_hello_t *)&mesh_stack->incoming_buffer;

  memset(&olleh_mesh_pkt, 0, sizeof(pkt_mesh_olleh_t));

  /*
   * Iterate the list of mesh stacks to check if there is already an
   * existing mesh stack from the same mesh network. If that is the case then
   * disconnect the current socket from the mesh client and also properl
   * disconnected and cleanup the existing mesh stack.
   *
   * Normally this situation should not occur but the ESP mesh library shows this
   * behaviour where a same mesh network can have two root nodes even though it
   * must be eventually automatically resolved by the mesh network.
   */

  for(int32_t i = 0; i < mesh_stacks.count; ++i) {
    gid_from_list = 0;
    gid_from_hello_pkt = 0;
    mesh_stack_from_list = NULL;

    mesh_stack_from_list = (MeshStack *)array_get(&mesh_stacks, i);

    memcpy(&gid_from_hello_pkt, &hello_mesh_pkt->group_id, sizeof(hello_mesh_pkt->group_id));
    memcpy(&gid_from_list,
           &mesh_stack_from_list->group_id,
           sizeof(mesh_stack_from_list->group_id));
     if(gid_from_list == gid_from_hello_pkt) {
     log_warn("Hello from root node of existing mesh stack (G: %02x-%02x-%02x-%02x-%02x-%02x)",
              hello_mesh_pkt->group_id[0],
              hello_mesh_pkt->group_id[1],
              hello_mesh_pkt->group_id[2],
              hello_mesh_pkt->group_id[3],
              hello_mesh_pkt->group_id[4],
              hello_mesh_pkt->group_id[5]);

     mesh_stack_from_list->cleanup = true;

     return false;
    }
  }

  // Create a new stack object for the mesh stack.
  if(stack_create(&mesh_stack->base,
                  mesh_stack->name,
                  mesh_stack_dispatch_request) < 0) {
    log_error("Could not create base stack for mesh client %s: %s (%d)",
              mesh_stack->name,
              get_errno_name(errno),
              errno);
    return false;
  }

  // Add to main stacks array.
  if (hardware_add_stack(&mesh_stack->base) < 0) {
    stack_destroy(&mesh_stack->base);

    log_error("Could not add mesh stack to main stacks array");

    return false;
  }

  log_info("Mesh stack %s changed state to operational "FMT_DBG_RECV_HELLO"",
           mesh_stack->name,
           hello_mesh_pkt->firmware_version[0],
           hello_mesh_pkt->firmware_version[1],
           hello_mesh_pkt->firmware_version[2],
           (char *)&hello_mesh_pkt->prefix,
           hello_mesh_pkt->group_id[0],
           hello_mesh_pkt->group_id[1],
           hello_mesh_pkt->group_id[2],
           hello_mesh_pkt->group_id[3],
           hello_mesh_pkt->group_id[4],
           hello_mesh_pkt->group_id[5]);

  mesh_header = (esp_mesh_header_t *)esp_mesh_get_packet_header(// Direction.
                                                                ESP_MESH_PACKET_DOWNWARDS,
                                                                // P2P.
                                                                false,
                                                                // ESP mesh payload protocol.
                                                                ESP_MESH_PAYLOAD_BIN,
                                                                // Length of the payload of the mesh packet.
                                                                sizeof(pkt_mesh_olleh_t) - sizeof(esp_mesh_header_t),
                                                                // Destination address.
                                                                hello_mesh_pkt->header.src_addr,
                                                                // Source address.
                                                                hello_mesh_pkt->header.dst_addr);

  // Prepare the olleh packet.
  memcpy(&olleh_mesh_pkt.header, mesh_header, sizeof(esp_mesh_header_t));
  free(mesh_header);

  olleh_mesh_pkt.type = MESH_PACKET_OLLEH;

  if(io_write(mesh_stack->io, &olleh_mesh_pkt, olleh_mesh_pkt.header.len) < 0) {
    log_error("Sending olleh packet failed, disconnecting mesh client");

    return false;
  }

  log_info("Olleh packet sent (L: %d)", olleh_mesh_pkt.header.len);

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

  return true;
}

bool tfp_mesh_tfp_hello_non_root_recv_handler(MeshStack *mesh_stack) {
  pkt_mesh_olleh_t olleh_mesh_pkt;
  esp_mesh_header_t *mesh_header = NULL;
  pkt_mesh_hello_t *hello_mesh_pkt = \
    (pkt_mesh_hello_t *)&mesh_stack->incoming_buffer;

  memset(&olleh_mesh_pkt, 0, sizeof(pkt_mesh_olleh_t));

  mesh_header = \
    (esp_mesh_header_t *)esp_mesh_get_packet_header(// Direction.
                                                    ESP_MESH_PACKET_DOWNWARDS,
                                                    // P2P.
                                                    false,
                                                    // ESP mesh payload protocol.
                                                    ESP_MESH_PAYLOAD_BIN,
                                                    // Length of the payload of the mesh packet.
                                                    sizeof(pkt_mesh_olleh_t) - sizeof(esp_mesh_header_t),
                                                    // Destination address.
                                                    hello_mesh_pkt->header.src_addr,
                                                    // Source address.
                                                    mesh_stack->gw_addr);

  // Prepare the olleh packet.
  memcpy(&olleh_mesh_pkt.header, mesh_header, sizeof(esp_mesh_header_t));
  free(mesh_header);

  olleh_mesh_pkt.type = MESH_PACKET_OLLEH;

  if(io_write(mesh_stack->io, &olleh_mesh_pkt, olleh_mesh_pkt.header.len) < 0) {
    log_info("Olleh packet send failed (A: %02X-%02X-%02X-%02X-%02X-%02X, L: %d)",
             hello_mesh_pkt->header.src_addr[0],
             hello_mesh_pkt->header.src_addr[1],
             hello_mesh_pkt->header.src_addr[2],
             hello_mesh_pkt->header.src_addr[3],
             hello_mesh_pkt->header.src_addr[4],
             hello_mesh_pkt->header.src_addr[5],
             hello_mesh_pkt->header.len);

    return false;
  }

  log_info("Olleh packet sent (A: %02X-%02X-%02X-%02X-%02X-%02X, L: %d)",
           hello_mesh_pkt->header.src_addr[0],
           hello_mesh_pkt->header.src_addr[1],
           hello_mesh_pkt->header.src_addr[2],
           hello_mesh_pkt->header.src_addr[3],
           hello_mesh_pkt->header.src_addr[4],
           hello_mesh_pkt->header.src_addr[5],
           hello_mesh_pkt->header.len);

  return true;
}

void *esp_mesh_get_packet_header(uint8_t flag_direction,
                                 uint8_t flag_p2p,
                                 uint8_t flag_protocol,
                                 uint16_t len,
                                 uint8_t *mesh_dst_addr,
                                 uint8_t *mesh_src_addr) {
  esp_mesh_header_t *mesh_header = \
    (esp_mesh_header_t *)malloc(sizeof(esp_mesh_header_t));
  memset(mesh_header, 0, sizeof(esp_mesh_header_t));

  mesh_header->flag_version = ESP_MESH_VERSION;
  mesh_header->proto.flag_direction = flag_direction;
  mesh_header->proto.flag_p2p = flag_p2p;
  mesh_header->proto.flag_protocol = flag_protocol;
  mesh_header->len = sizeof(esp_mesh_header_t) + len;
  memcpy(&mesh_header->dst_addr, mesh_dst_addr, sizeof(mesh_header->dst_addr));
  memcpy(&mesh_header->src_addr, mesh_src_addr, sizeof(mesh_header->src_addr));

  return (void *)mesh_header;
}
