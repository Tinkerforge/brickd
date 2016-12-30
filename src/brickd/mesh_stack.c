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
#include <unistd.h>

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

static void mesh_stack_recv_handler(void *opaque) {
  int read_len = 0;
  uint8_t mesh_pkt_type = 0;
  esp_mesh_header_t *mesh_header = NULL;
  pkt_mesh_hello_t *hello_mesh_pkt = NULL;
  MeshStack *mesh_stack = (MeshStack *)opaque;

  read_len = socket_receive(mesh_stack->sock,
                            (uint8_t *)&mesh_stack->incoming_buffer + mesh_stack->incoming_buffer_used,
                            sizeof(mesh_stack->incoming_buffer) - mesh_stack->incoming_buffer_used);

  if(read_len == 0) {
    /*
     * Mark the stack for cleanup. Actual cleanup will be done after this
     * event handler callback has returned.
     */
    mesh_stack->cleanup = true;

    log_info("Mesh stack disconnected (N: %s)", mesh_stack->name);

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
			log_error("Could not receive from mesh client, disconnecting stack (N: %s)",
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
        log_error("Received invalid mesh header, disconnecting mesh stack (N: %s)",
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
      mesh_pkt_type = mesh_stack->incoming_buffer[sizeof(esp_mesh_header_t)];

      // Handle mesh hello packet.
      if(mesh_pkt_type == MESH_PACKET_HELLO) {
        log_info("Received mesh packet (T: HELLO, L: %d)", mesh_header->len);

        hello_mesh_pkt = (pkt_mesh_hello_t *)mesh_stack->incoming_buffer;

        timer_destroy(&mesh_stack->timer_wait_hello);

        if(hello_mesh_pkt->is_root_node) {
          log_info("Hello from root mesh node (F: %d.%d.%d, P: %s, G: %02X-%02X-%02X-%02X-%02X-%02X, A: %02X-%02X-%02X-%02X-%02X-%02X)",
                   hello_mesh_pkt->firmware_version[0],
                   hello_mesh_pkt->firmware_version[1],
                   hello_mesh_pkt->firmware_version[2],
                   (char *)&hello_mesh_pkt->prefix,
                   hello_mesh_pkt->group_id[0],
                   hello_mesh_pkt->group_id[1],
                   hello_mesh_pkt->group_id[2],
                   hello_mesh_pkt->group_id[3],
                   hello_mesh_pkt->group_id[4],
                   hello_mesh_pkt->group_id[5],
                   hello_mesh_pkt->header.src_addr[0],
                   hello_mesh_pkt->header.src_addr[1],
                   hello_mesh_pkt->header.src_addr[2],
                   hello_mesh_pkt->header.src_addr[3],
                   hello_mesh_pkt->header.src_addr[4],
                   hello_mesh_pkt->header.src_addr[5]);

          if(!hello_root_recv_handler(mesh_stack)) {
            mesh_stack->cleanup = true;

            return;
          }
        }
        else {
          log_info("Hello from non-root mesh node (A: %02X-%02X-%02X-%02X-%02X-%02X)",
                   hello_mesh_pkt->header.src_addr[0],
                   hello_mesh_pkt->header.src_addr[1],
                   hello_mesh_pkt->header.src_addr[2],
                   hello_mesh_pkt->header.src_addr[3],
                   hello_mesh_pkt->header.src_addr[4],
                   hello_mesh_pkt->header.src_addr[5]);

          if(!hello_non_root_recv_handler(mesh_stack)) {
            mesh_stack->cleanup = true;

            return;
          }
        }
      }
      // Handle heart beat ping packet.
      else if(mesh_pkt_type == MESH_PACKET_HB_PING) {
        log_debug("Received mesh ping packet (T: PING, L: %d, A: %02X-%02X-%02X-%02X-%02X-%02X)",
                  mesh_header->len,
                  mesh_header->src_addr[0],
                  mesh_header->src_addr[1],
                  mesh_header->src_addr[2],
                  mesh_header->src_addr[3],
                  mesh_header->src_addr[4],
                  mesh_header->src_addr[5]);

        pkt_mesh_hb_t pkt_mesh_hb;
        uint8_t dst[ESP_MESH_ADDRESS_LEN];
        uint8_t src[ESP_MESH_ADDRESS_LEN];

        memset(&dst, 0, sizeof(dst));
        memset(&src, 0, sizeof(src));
        memset(&pkt_mesh_hb, 0, sizeof(pkt_mesh_hb_t));

        mesh_header->proto.flag_direction =  ESP_MESH_PACKET_DOWNWARDS;
        memcpy(&mesh_header->dst_addr, &mesh_header->src_addr, sizeof(mesh_header->src_addr));
        memcpy(&mesh_header->src_addr, &mesh_stack->gw_addr, sizeof(mesh_stack->gw_addr));

        memcpy(&pkt_mesh_hb.header, mesh_header, sizeof(esp_mesh_header_t));

        /*
         * Don't call free() on mesh_header as it is in the incoming buffer of
         * the mesh stack!
         */

         pkt_mesh_hb.type = MESH_PACKET_HB_PONG;

        // TODO: Integrate buffered IO write.
        if(socket_send(mesh_stack->sock, &pkt_mesh_hb, pkt_mesh_hb.header.len) < 0) {
          log_error("Failed to send mesh pong packet");
        }
        else {
          log_debug("Sent mesh pong packet (A: %02X-%02X-%02X-%02X-%02X-%02X)",
                    mesh_header->dst_addr[0],
                    mesh_header->dst_addr[1],
                    mesh_header->dst_addr[2],
                    mesh_header->dst_addr[3],
                    mesh_header->dst_addr[4],
                    mesh_header->dst_addr[5]);
        }
      }
      // Handle TFP packet.
      else if(mesh_pkt_type == MESH_PACKET_TFP) {
        log_debug("Received mesh packet (T: TFP, L: %d, A: %02X-%02X-%02X-%02X-%02X-%02X)",
                  mesh_header->len,
                  mesh_header->src_addr[0],
                  mesh_header->src_addr[1],
                  mesh_header->src_addr[2],
                  mesh_header->src_addr[3],
                  mesh_header->src_addr[4],
                  mesh_header->src_addr[5]);

        tfp_recv_handler(mesh_stack);
      }
      //Packet type is unknown.
      else {
        log_error("Unknown mesh packet type received");
      }
    }

    memmove(&mesh_stack->incoming_buffer,
            (uint8_t *)&mesh_stack->incoming_buffer + read_len,
           mesh_stack->incoming_buffer_used - read_len);

    mesh_stack->mesh_header_checked = false;
    mesh_stack->incoming_buffer_used -= read_len;
  }
}

static void timer_wait_hello_handler(void *opaque) {
  MeshStack *mesh_stack = (MeshStack *)opaque;

  timer_destroy(&mesh_stack->timer_wait_hello);

  log_info("Wait hello timed out, destroying mesh stack (N: %s)",
           mesh_stack->name);

  broadcast_reset_packet(mesh_stack);

  // Schedule a cleanup of the mesh stack.
  mesh_stack->cleanup = true;

  // Wait for reset stack packet to be broadcasted to all the nodes.
  sleep(4);
}

bool tfp_recv_handler(MeshStack *mesh_stack) {
  uint64_t mesh_src_addr = 0;
  pkt_mesh_tfp_t *pkt_mesh_tfp = (pkt_mesh_tfp_t *)&mesh_stack->incoming_buffer;

  memcpy(&mesh_src_addr,
         &pkt_mesh_tfp->header.src_addr,
         sizeof(pkt_mesh_tfp->header.src_addr));

  if(stack_add_recipient(&mesh_stack->base, pkt_mesh_tfp->pkt_tfp.header.uid, mesh_src_addr) < 0) {
    log_error("Failed to add recipient to mesh stack");

    return false;
  }

  network_dispatch_response(&pkt_mesh_tfp->pkt_tfp);

  log_debug("TFP packet dispatched (L: %d)", pkt_mesh_tfp->pkt_tfp.header.length);

  return true;
}

void mesh_stack_destroy(MeshStack *mesh_stack) {
  event_remove_source(mesh_stack->sock->base.handle,
                      EVENT_SOURCE_TYPE_GENERIC);

  socket_destroy(mesh_stack->sock);

  if(mesh_stack->state == MESH_STACK_STATE_OPERATIONAL) {
    stack_announce_disconnect(&mesh_stack->base);
    hardware_remove_stack(&mesh_stack->base);
    stack_destroy(&mesh_stack->base);
  }

  if(mesh_stack->state == MESH_STACK_STATE_WAIT_HELLO) {
    log_info("Mesh stack %s released (S: WAIT_HELLO)",
             mesh_stack->name);
  }
  else if(mesh_stack->state == MESH_STACK_STATE_OPERATIONAL) {
    log_info("Mesh stack %s released (S: OPERATIONAL)",
             mesh_stack->name);
  }
  else {
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

  if(event_add_source(sock->base.handle,
                      EVENT_SOURCE_TYPE_GENERIC,
                      EVENT_READ,
                      mesh_stack_recv_handler,
                      mesh_stack) < 0) {
    log_error("Failed to add stack receive event");

    array_remove(&mesh_stacks,
                 mesh_stacks.count - 1,
                 (ItemDestroyFunction)mesh_stack_destroy);

    return -1;
  }

  // Initialise the mesh stack.
  mesh_stack->sock = sock;
  mesh_stack->cleanup = false;
  mesh_stack->incoming_buffer_used = 0;
  mesh_stack->mesh_header_checked = false;

	snprintf(mesh_stack->name, sizeof(mesh_stack->name), "%s", name);

  if(timer_create_(&mesh_stack->timer_wait_hello, timer_wait_hello_handler, mesh_stack) < 0) {
    log_error("Failed to start wait hello timer: %s (%d)",
              get_errno_name(errno),
              errno);

    timer_destroy(&mesh_stack->timer_wait_hello);

    array_remove(&mesh_stacks,
                 mesh_stacks.count - 1,
                 (ItemDestroyFunction)mesh_stack_destroy);

    return -1;
  }

  if(timer_configure(&mesh_stack->timer_wait_hello, TIME_WAIT_HELLO, 0) < 0) {
    log_error("Failed to configure wait hello timer: %s (%d)",
              get_errno_name(errno),
              errno);

    timer_destroy(&mesh_stack->timer_wait_hello);

    array_remove(&mesh_stacks,
                 mesh_stacks.count - 1,
                 (ItemDestroyFunction)mesh_stack_destroy);

    return -1;
   }

  log_info("Mesh stack is waiting for hello packet (N: %s)", mesh_stack->name);

  return 0;
}

void broadcast_reset_packet(MeshStack *mesh_stack) {
  pkt_mesh_reset_t pkt_mesh_reset;
  uint8_t addr[ESP_MESH_ADDRESS_LEN];
  esp_mesh_header_t *mesh_header = NULL;

  memset(&addr, 0, sizeof(addr));

  mesh_header = (esp_mesh_header_t *)esp_mesh_get_packet_header(// Direction.
                                                                ESP_MESH_PACKET_DOWNWARDS,
                                                                // P2P.
                                                                false,
                                                                // ESP mesh payload protocol.
                                                                ESP_MESH_PAYLOAD_BIN,
                                                                // Length of the payload of the mesh packet.
                                                                sizeof(pkt_mesh_reset_t) - sizeof(esp_mesh_header_t),
                                                                // Destination address.
                                                                addr,
                                                                // Source address.
                                                                addr);

  memset(&pkt_mesh_reset, 0, sizeof(pkt_mesh_reset_t));
  memcpy(&pkt_mesh_reset.header, mesh_header, sizeof(esp_mesh_header_t));

  free(mesh_header);

  pkt_mesh_reset.type = MESH_PACKET_RESET;

  if(socket_send(mesh_stack->sock, &pkt_mesh_reset, pkt_mesh_reset.header.len) < 0) {
    log_error("Failed to send broadcast reset stack packet, LEN=%d", pkt_mesh_reset.header.len);
  }
  else {
    log_info("Broadcast reset stack packet sent");
  }
}

bool hello_root_recv_handler(MeshStack *mesh_stack) {
  pkt_mesh_olleh_t olleh_mesh_pkt;
  esp_mesh_header_t *mesh_header = NULL;
  MeshStack *mesh_stack_from_list = NULL;
  pkt_mesh_hello_t *hello_mesh_pkt = \
    (pkt_mesh_hello_t *)&mesh_stack->incoming_buffer;

  memset(&olleh_mesh_pkt, 0, sizeof(pkt_mesh_olleh_t));

  #ifdef BRICKD_WITH_MESH_SINGLE_ROOT_NODE
    /*
     * Iterate the list of mesh stacks to check if there is already an
     * existing mesh stack from the same mesh network. If that is the case then
     * send reset packet to both of the stack's sockets and clean both of them up.
     */

    uint64_t gid_from_list = 0;
    uint64_t gid_from_hello_pkt = 0;

    for(uint32_t i = 0; i < mesh_stacks.count; ++i) {
      gid_from_list = 0;
      gid_from_hello_pkt = 0;
      pkt_mesh_reset_t pkt_mesh_reset;

      mesh_stack_from_list = (MeshStack *)array_get(&mesh_stacks, i);

      memcpy(&gid_from_hello_pkt,
             &hello_mesh_pkt->group_id,
             sizeof(hello_mesh_pkt->group_id));

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

        // Reset the mesh stack that was found on the list.
        mesh_header = (esp_mesh_header_t *)esp_mesh_get_packet_header(// Direction.
                                                                      ESP_MESH_PACKET_DOWNWARDS,
                                                                      // P2P.
                                                                      false,
                                                                      // ESP mesh payload protocol.
                                                                      ESP_MESH_PAYLOAD_BIN,
                                                                      // Length of the payload of the mesh packet.
                                                                      sizeof(pkt_mesh_reset_t) - sizeof(esp_mesh_header_t),
                                                                      // Destination address.
                                                                      mesh_stack_from_list->root_node_addr,
                                                                      // Source address.
                                                                      hello_mesh_pkt->header.dst_addr);

        memset(&pkt_mesh_reset, 0, sizeof(pkt_mesh_reset_t));
        memcpy(&pkt_mesh_reset.header, mesh_header, sizeof(pkt_mesh_reset_t));

        free(mesh_header);

        pkt_mesh_reset.type = MESH_PACKET_RESET;

        // TODO: Integrate buffered IO write.
        if(socket_send(mesh_stack_from_list->sock, &pkt_mesh_reset, pkt_mesh_reset.header.len) < 0) {
          log_error("Failed to send mesh stack reset packet (A: %02X-%02X-%02X-%02X-%02X-%02X)",
                    mesh_stack_from_list->root_node_addr[0],
                    mesh_stack_from_list->root_node_addr[1],
                    mesh_stack_from_list->root_node_addr[2],
                    mesh_stack_from_list->root_node_addr[3],
                    mesh_stack_from_list->root_node_addr[4],
                    mesh_stack_from_list->root_node_addr[5]);
        }
        else {
          log_warn("Sent mesh stack reset packet (A: %02X-%02X-%02X-%02X-%02X-%02X)",
                   mesh_stack_from_list->root_node_addr[0],
                   mesh_stack_from_list->root_node_addr[1],
                   mesh_stack_from_list->root_node_addr[2],
                   mesh_stack_from_list->root_node_addr[3],
                   mesh_stack_from_list->root_node_addr[4],
                   mesh_stack_from_list->root_node_addr[5]);
        }

        // Reset the mesh stack from which we just received.
        mesh_header = (esp_mesh_header_t *)esp_mesh_get_packet_header(// Direction.
                                                                      ESP_MESH_PACKET_DOWNWARDS,
                                                                      // P2P.
                                                                      false,
                                                                      // ESP mesh payload protocol.
                                                                      ESP_MESH_PAYLOAD_BIN,
                                                                      // Length of the payload of the mesh packet.
                                                                      sizeof(pkt_mesh_reset_t) - sizeof(esp_mesh_header_t),
                                                                      // Destination address.
                                                                      hello_mesh_pkt->header.src_addr,
                                                                      // Source address.
                                                                      hello_mesh_pkt->header.dst_addr);

        memset(&pkt_mesh_reset, 0, sizeof(pkt_mesh_reset_t));
        memcpy(&pkt_mesh_reset.header, mesh_header, sizeof(pkt_mesh_reset_t));

        free(mesh_header);

        pkt_mesh_reset.type = MESH_PACKET_RESET;

        // TODO: Integrate buffered IO write.
        if(socket_send(mesh_stack->sock, &pkt_mesh_reset, pkt_mesh_reset.header.len) < 0) {
          log_error("Failed to send mesh stack reset packet (A: %02X-%02X-%02X-%02X-%02X-%02X)",
                    hello_mesh_pkt->header.src_addr[0],
                    hello_mesh_pkt->header.src_addr[1],
                    hello_mesh_pkt->header.src_addr[2],
                    hello_mesh_pkt->header.src_addr[3],
                    hello_mesh_pkt->header.src_addr[4],
                    hello_mesh_pkt->header.src_addr[5]);
        }
        else {
          log_warn("Sent mesh stack reset packet (A: %02X-%02X-%02X-%02X-%02X-%02X)",
                   hello_mesh_pkt->header.src_addr[0],
                   hello_mesh_pkt->header.src_addr[1],
                   hello_mesh_pkt->header.src_addr[2],
                   hello_mesh_pkt->header.src_addr[3],
                   hello_mesh_pkt->header.src_addr[4],
                   hello_mesh_pkt->header.src_addr[5]);
        }

        /*
         * Make sure the stacks received the stack reset packets before
         * scheduling cleanups.
         */
        sleep(4);

        mesh_stack_from_list->cleanup = true;

        return false;
      }
    }
  #else
    uint64_t src_addr_from_hello_pkt = 0;
    uint64_t root_node_addr_from_existing_stack = 0;

    memcpy(&src_addr_from_hello_pkt,
           &hello_mesh_pkt->header.src_addr,
           sizeof(hello_mesh_pkt->header.src_addr));

    for(int32_t i = 0; i < mesh_stacks.count; ++i) {
      mesh_stack_from_list = NULL;

      mesh_stack_from_list = (MeshStack *)array_get(&mesh_stacks, i);

      memcpy(&root_node_addr_from_existing_stack,
             &mesh_stack_from_list->root_node_addr,
             sizeof(mesh_stack_from_list->root_node_addr));

      /*
       * Schedule a cleanup if there was already an existing mesh stack from
       * the same mesh node.
       */
      if(root_node_addr_from_existing_stack == src_addr_from_hello_pkt) {
        mesh_stack_from_list->cleanup = true;
      }
    }
  #endif

  // Create a new stack object for the mesh stack.
  if(stack_create(&mesh_stack->base,
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
  memset(&olleh_mesh_pkt, 0, sizeof(pkt_mesh_olleh_t));
  memcpy(&olleh_mesh_pkt.header, mesh_header, sizeof(esp_mesh_header_t));

  free(mesh_header);

  olleh_mesh_pkt.type = MESH_PACKET_OLLEH;

  if(socket_send(mesh_stack->sock, &olleh_mesh_pkt, olleh_mesh_pkt.header.len) < 0) {
    log_error("Failed to send mesh olleh packet (A: %02X-%02X-%02X-%02X-%02X-%02X)",
              hello_mesh_pkt->header.src_addr[0],
              hello_mesh_pkt->header.src_addr[1],
              hello_mesh_pkt->header.src_addr[2],
              hello_mesh_pkt->header.src_addr[3],
              hello_mesh_pkt->header.src_addr[4],
              hello_mesh_pkt->header.src_addr[5]);

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

  log_info("Mesh stack %s changed state to operational (F: %d.%d.%d, P: %s, G: %02X:%02X:%02X:%02X:%02X:%02X)",
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

  return true;
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

int mesh_stack_dispatch_request(Stack *stack, Packet *request, Recipient *recipient) {
  bool is_broadcast = true;
  pkt_mesh_tfp_t tfp_mesh_pkt;
  char base58[BASE58_MAX_LENGTH];
  esp_mesh_header_t *mesh_header = NULL;
  uint8_t dst_addr[ESP_MESH_ADDRESS_LEN];
  MeshStack *mesh_stack = (MeshStack *)stack;

  memset(&dst_addr, 0, sizeof(dst_addr));

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

  memset(&tfp_mesh_pkt, 0, sizeof(pkt_mesh_tfp_t));
  memcpy(&tfp_mesh_pkt.header, mesh_header, mesh_header->len);

  free(mesh_header);

  tfp_mesh_pkt.type = MESH_PACKET_TFP;

  memcpy(&tfp_mesh_pkt.pkt_tfp, request, request->header.length);

  if(!is_broadcast) {
    memset(&base58, 0, sizeof(base58));
    base58_encode(base58, uint32_from_le(recipient->uid));
  }

  // TODO: Integrate buffered IO write.
  if(socket_send(mesh_stack->sock, &tfp_mesh_pkt, tfp_mesh_pkt.header.len) < 0) {
    if(is_broadcast) {
      log_error("Failed to send TFP packet to mesh (L: %d, B: %d)",
                request->header.length,
                is_broadcast);
    }
    else {
      log_error("Failed to send TFP packet to mesh (U: %s, L: %d, B: %d, A: %02X-%02X-%02X-%02X-%02X-%02X)",
                base58,
                request->header.length,
                is_broadcast,
                dst_addr[0],
                dst_addr[1],
                dst_addr[2],
                dst_addr[3],
                dst_addr[4],
                dst_addr[5]);
    }

    return -1;
  }
  else {
    if(is_broadcast) {
      log_debug("TFP packet sent to mesh (L: %d, B: %d)",
               request->header.length,
               is_broadcast);
    }
    else {
      log_debug("TFP packet sent to mesh (U: %s, L: %d, B: %d, A: %02X-%02X-%02X-%02X-%02X-%02X)",
                base58,
                request->header.length,
                is_broadcast,
                dst_addr[0],
                dst_addr[1],
                dst_addr[2],
                dst_addr[3],
                dst_addr[4],
                dst_addr[5]);
    }
  }

  return 0;
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

bool hello_non_root_recv_handler(MeshStack *mesh_stack) {
  pkt_mesh_olleh_t olleh_mesh_pkt;
  esp_mesh_header_t *mesh_header = NULL;
  pkt_mesh_hello_t *hello_mesh_pkt = \
    (pkt_mesh_hello_t *)&mesh_stack->incoming_buffer;

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
  memset(&olleh_mesh_pkt, 0, sizeof(pkt_mesh_olleh_t));
  memcpy(&olleh_mesh_pkt.header, mesh_header, sizeof(esp_mesh_header_t));

  free(mesh_header);

  olleh_mesh_pkt.type = MESH_PACKET_OLLEH;

  if(socket_send(mesh_stack->sock, &olleh_mesh_pkt, olleh_mesh_pkt.header.len) < 0) {
    log_error("Olleh packet send failed (A: %02X-%02X-%02X-%02X-%02X-%02X)",
             hello_mesh_pkt->header.src_addr[0],
             hello_mesh_pkt->header.src_addr[1],
             hello_mesh_pkt->header.src_addr[2],
             hello_mesh_pkt->header.src_addr[3],
             hello_mesh_pkt->header.src_addr[4],
             hello_mesh_pkt->header.src_addr[5]);

    return false;
  }

  log_info("Olleh packet sent (A: %02X-%02X-%02X-%02X-%02X-%02X)",
           hello_mesh_pkt->header.src_addr[0],
           hello_mesh_pkt->header.src_addr[1],
           hello_mesh_pkt->header.src_addr[2],
           hello_mesh_pkt->header.src_addr[3],
           hello_mesh_pkt->header.src_addr[4],
           hello_mesh_pkt->header.src_addr[5]);

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
