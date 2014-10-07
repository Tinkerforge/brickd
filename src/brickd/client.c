/*
 * brickd
 * Copyright (C) 2012-2014 Matthias Bolte <matthias@tinkerforge.com>
 * Copyright (C) 2014 Olaf Lüke <olaf@tinkerforge.com>
 *
 * client.c: Client specific functions
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

#include <daemonlib/config.h>
#include <daemonlib/event.h>
#include <daemonlib/log.h>
#include <daemonlib/utils.h>

#include "client.h"

#include "hardware.h"
#include "hmac.h"
#include "network.h"
#include "zombie.h"

#define LOG_CATEGORY LOG_CATEGORY_NETWORK

#define UID_BRICK_DAEMON 1

static void client_handle_get_authentication_nonce_request(Client *client, GetAuthenticationNonceRequest *request) {
	GetAuthenticationNonceResponse response;

	if (client->authentication_state == CLIENT_AUTHENTICATION_STATE_DISABLED) {
		log_error("Client ("CLIENT_SIGNATURE_FORMAT") tries to authenticate, but authentication is disabled, disconnecting client",
		          client_expand_signature(client));

		client->disconnected = true;

		return;
	}

	if (client->authentication_state == CLIENT_AUTHENTICATION_STATE_DONE) {
		log_debug("Already authenticated client ("CLIENT_SIGNATURE_FORMAT") tries to authenticate again",
		          client_expand_signature(client));

		client->authentication_state = CLIENT_AUTHENTICATION_STATE_ENABLED;
	}

	if (client->authentication_state != CLIENT_AUTHENTICATION_STATE_ENABLED) {
		log_error("Client ("CLIENT_SIGNATURE_FORMAT") performed invalid authentication sequence (%s -> %s), disconnecting client",
		          client_expand_signature(client),
		          client_get_authentication_state_name(client->authentication_state),
		          client_get_authentication_state_name(CLIENT_AUTHENTICATION_STATE_NONCE_SEND));

		client->disconnected = true;

		return;
	}

	response.header = request->header;
	response.header.length = sizeof(response);

	memcpy(response.server_nonce, &client->authentication_nonce, sizeof(response.server_nonce));

	client_dispatch_response(client, NULL, (Packet *)&response, false, true);

	client->authentication_state = CLIENT_AUTHENTICATION_STATE_NONCE_SEND;
}

static void client_handle_authenticate_request(Client *client, AuthenticateRequest *request) {
	uint32_t nonces[2];
	uint8_t digest[SHA1_DIGEST_LENGTH];
	const char *secret;
	char packet_signature[PACKET_MAX_SIGNATURE_LENGTH];
	AuthenticateResponse response;

	if (client->authentication_state == CLIENT_AUTHENTICATION_STATE_DISABLED) {
		log_error("Client ("CLIENT_SIGNATURE_FORMAT") tries to authenticate, but authentication is disabled, disconnecting client",
		          client_expand_signature(client));

		client->disconnected = true;

		return;
	}

	if (client->authentication_state != CLIENT_AUTHENTICATION_STATE_NONCE_SEND) {
		log_error("Client ("CLIENT_SIGNATURE_FORMAT") performed invalid authentication sequence (%s -> %s), disconnecting client",
		          client_expand_signature(client),
		          client_get_authentication_state_name(client->authentication_state),
		          client_get_authentication_state_name(CLIENT_AUTHENTICATION_STATE_DONE));

		client->disconnected = true;

		return;
	}

	memcpy(&nonces[0], &client->authentication_nonce, sizeof(client->authentication_nonce));
	memcpy(&nonces[1], request->client_nonce, sizeof(request->client_nonce));

	secret = config_get_option_value("authentication.secret")->string;

	hmac_sha1((uint8_t *)secret, strlen(secret),
	          (uint8_t *)nonces, sizeof(nonces), digest);

	if (memcmp(request->digest, digest, SHA1_DIGEST_LENGTH) != 0) {
		log_error("Authentication request (%s) from client ("CLIENT_SIGNATURE_FORMAT") did not contain the expected data, disconnecting client",
		          packet_get_request_signature(packet_signature, (Packet *)request),
		          client_expand_signature(client));

		client->disconnected = true;

		return;
	}

	client->authentication_state = CLIENT_AUTHENTICATION_STATE_DONE;

	log_info("Client ("CLIENT_SIGNATURE_FORMAT") successfully finished authentication",
	         client_expand_signature(client));

	if (packet_header_get_response_expected(&request->header)) {
		response.header = request->header;
		response.header.length = sizeof(response);

		packet_header_set_error_code(&response.header, PACKET_E_SUCCESS);

		client_dispatch_response(client, NULL, (Packet *)&response, false, false);
	}
}

static void client_handle_request(Client *client, Packet *request) {
	char packet_signature[PACKET_MAX_SIGNATURE_LENGTH];
	EmptyResponse response;

	// handle requests meant for brickd
	if (uint32_from_le(request->header.uid) == UID_BRICK_DAEMON) {
		// add as pending request if response is expected
		if (packet_header_get_response_expected(&request->header)) {
			network_client_expects_response(client, request);
		}

		if (request->header.function_id == FUNCTION_GET_AUTHENTICATION_NONCE) {
			if (request->header.length != sizeof(GetAuthenticationNonceRequest)) {
				log_error("Received authentication request (%s) from client ("CLIENT_SIGNATURE_FORMAT") with wrong length, disconnecting client",
				          packet_get_request_signature(packet_signature, request),
				          client_expand_signature(client));

				client->disconnected = true;

				return;
			}

			client_handle_get_authentication_nonce_request(client, (GetAuthenticationNonceRequest *)request);
		} else if (request->header.function_id == FUNCTION_AUTHENTICATE) {
			if (request->header.length != sizeof(AuthenticateRequest)) {
				log_error("Received authentication request (%s) from client ("CLIENT_SIGNATURE_FORMAT") with wrong length, disconnecting client",
				          packet_get_request_signature(packet_signature, request),
				          client_expand_signature(client));

				client->disconnected = true;

				return;
			}

			client_handle_authenticate_request(client, (AuthenticateRequest *)request);
		} else {
			response.header = request->header;
			response.header.length = sizeof(response);

			packet_header_set_error_code(&response.header,
			                             PACKET_E_FUNCTION_NOT_SUPPORTED);

			client_dispatch_response(client, NULL, (Packet *)&response, false, false);
		}
	} else if (client->authentication_state == CLIENT_AUTHENTICATION_STATE_DISABLED ||
	           client->authentication_state == CLIENT_AUTHENTICATION_STATE_DONE) {
		// add as pending request if response is expected...
		if (packet_header_get_response_expected(&request->header)) {
			network_client_expects_response(client, request);
		}

		// ...then dispatch it to the hardware
		hardware_dispatch_request(request);
	} else {
		log_debug("Client ("CLIENT_SIGNATURE_FORMAT") is not authenticated, dropping request (%s)",
		          client_expand_signature(client),
		          packet_get_request_signature(packet_signature, request));
	}
}

static void client_handle_read(void *opaque) {
	Client *client = opaque;
	int length;
	const char *message = NULL;
	char packet_signature[PACKET_MAX_SIGNATURE_LENGTH];

	length = io_read(client->io, (uint8_t *)&client->request + client->request_used,
	                 sizeof(Packet) - client->request_used);

	if (length == 0) {
		log_info("Client ("CLIENT_SIGNATURE_FORMAT") disconnected by peer",
		         client_expand_signature(client));

		client->disconnected = true;

		return;
	}

	if (length < 0) {
		if (length == IO_CONTINUE) {
			// no actual data received
		} else if (errno_interrupted()) {
			log_debug("Receiving from client ("CLIENT_SIGNATURE_FORMAT") was interrupted, retrying",
			          client_expand_signature(client));
		} else if (errno_would_block()) {
			log_debug("Receiving from client ("CLIENT_SIGNATURE_FORMAT") would block, retrying",
			          client_expand_signature(client));
		} else {
			log_error("Could not receive from client ("CLIENT_SIGNATURE_FORMAT"), disconnecting client: %s (%d)",
			          client_expand_signature(client), get_errno_name(errno), errno);

			client->disconnected = true;
		}

		return;
	}

	client->request_used += length;

	while (!client->disconnected && client->request_used > 0) {
		if (client->request_used < (int)sizeof(PacketHeader)) {
			// wait for complete header
			break;
		}

		if (!client->request_header_checked) {
			if (!packet_header_is_valid_request(&client->request.header, &message)) {
				// FIXME: include packet_get_content_dump output in the error message
				log_error("Received invalid request (%s) from client ("CLIENT_SIGNATURE_FORMAT"), disconnecting client: %s",
				          packet_get_request_signature(packet_signature, &client->request),
				          client_expand_signature(client), message);

				client->disconnected = true;

				return;
			}

			client->request_header_checked = true;
		}

		length = client->request.header.length;

		if (client->request_used < length) {
			// wait for complete packet
			break;
		}

		if (client->request.header.function_id == FUNCTION_DISCONNECT_PROBE) {
			log_debug("Received disconnect probe from client ("CLIENT_SIGNATURE_FORMAT"), dropping request",
			          client_expand_signature(client));
		} else {
			log_debug("Received request (%s) from client ("CLIENT_SIGNATURE_FORMAT")",
			          packet_get_request_signature(packet_signature, &client->request),
			          client_expand_signature(client));

			client_handle_request(client, &client->request);
		}

		memmove(&client->request, (uint8_t *)&client->request + length,
		        client->request_used - length);

		client->request_used -= length;
		client->request_header_checked = false;
	}
}

void pending_request_remove_and_free(PendingRequest *pending_request) {
	node_remove(&pending_request->global_node);
	node_remove(&pending_request->client_node);

	if (pending_request->client != NULL) {
		--pending_request->client->pending_request_count;
	}

	if (pending_request->zombie != NULL) {
		--pending_request->zombie->pending_request_count;
	}

	free(pending_request);
}

const char *client_get_authentication_state_name(ClientAuthenticationState state) {
	switch (state) {
	case CLIENT_AUTHENTICATION_STATE_DISABLED:   return "disabled";
	case CLIENT_AUTHENTICATION_STATE_ENABLED:    return "enabled";
	case CLIENT_AUTHENTICATION_STATE_NONCE_SEND: return "nonce-send";
	case CLIENT_AUTHENTICATION_STATE_DONE:       return "done";

	default:                                     return "<unknown>";
	}
}

static char *client_get_recipient_signature(char *signature, bool upper, void *opaque) {
	Client *client = opaque;

	snprintf(signature, WRITER_MAX_RECIPIENT_SIGNATURE_LENGTH,
	         "%client ("CLIENT_SIGNATURE_FORMAT")",
	         upper ? 'C' : 'c', client_expand_signature(client));

	return signature;
}

static void client_recipient_disconnect(void *opaque) {
	Client *client = opaque;

	client->disconnected = true;
}

int client_create(Client *client, const char *name, IO *io,
                  uint32_t authentication_nonce,
                  ClientDestroyDoneFunction destroy_done) {
	log_debug("Creating client from %s (handle: %d)", io->type, io->handle);

	string_copy(client->name, sizeof(client->name), name);

	client->io = io;
	client->disconnected = false;
	client->request_used = 0;
	client->request_header_checked = false;
	client->pending_request_count = 0;
	client->authentication_state = CLIENT_AUTHENTICATION_STATE_DISABLED;
	client->authentication_nonce = authentication_nonce;
	client->destroy_done = destroy_done;

	if (config_get_option_value("authentication.secret")->string != NULL) {
		client->authentication_state = CLIENT_AUTHENTICATION_STATE_ENABLED;
	}

	node_reset(&client->pending_request_sentinel);

	// create response writer
	if (writer_create(&client->response_writer, client->io,
	                  "response", packet_get_response_signature,
	                  "client", client_get_recipient_signature,
	                  client_recipient_disconnect, client) < 0) {
		log_error("Could not create response writer: %s (%d)",
		          get_errno_name(errno), errno);

		return -1;
	}

	// add I/O object as event source
	return event_add_source(client->io->handle, EVENT_SOURCE_TYPE_GENERIC,
	                        EVENT_READ, client_handle_read, client);
}

void client_destroy(Client *client) {
	bool destroy_pending_requests = false;
	PendingRequest *pending_request;

	if (client->pending_request_count > 0) {
		log_warn("Destroying client ("CLIENT_SIGNATURE_FORMAT") while %d request(s) are still pending",
		         client_expand_signature(client), client->pending_request_count);

		if (network_create_zombie(client) < 0) {
			log_error("Could not create zombie for %d pending request(s) of ("CLIENT_SIGNATURE_FORMAT")",
			          client->pending_request_count, client_expand_signature(client));

			destroy_pending_requests = true;
		}
	}

	writer_destroy(&client->response_writer);

	event_remove_source(client->io->handle, EVENT_SOURCE_TYPE_GENERIC);
	io_destroy(client->io);
	free(client->io);

	if (destroy_pending_requests) {
		while (client->pending_request_sentinel.next != &client->pending_request_sentinel) {
			pending_request = containerof(client->pending_request_sentinel.next, PendingRequest, client_node);

			pending_request_remove_and_free(pending_request);
		}
	}

	if (client->destroy_done != NULL) {
		client->destroy_done();
	}
}

void client_dispatch_response(Client *client, PendingRequest *pending_request,
                              Packet *response, bool force, bool ignore_authentication) {
	Node *pending_request_client_node = NULL;
	int enqueued = 0;
#ifdef BRICKD_WITH_PROFILING
	uint64_t elapsed;
#endif

	if (!ignore_authentication &&
	    client->authentication_state != CLIENT_AUTHENTICATION_STATE_DISABLED &&
	    client->authentication_state != CLIENT_AUTHENTICATION_STATE_DONE) {
		log_debug("Ignoring non-authenticated client ("CLIENT_SIGNATURE_FORMAT")",
		          client_expand_signature(client));

		goto cleanup;
	}

	// find matching pending request if not forced and no pending request is
	// already given. do this before the disconnect check to ensure that even
	// for a disconnected client the pending request list is updated correctly
	if (!force && pending_request == NULL) {
		pending_request_client_node = client->pending_request_sentinel.next;

		while (pending_request_client_node != &client->pending_request_sentinel) {
			pending_request = containerof(pending_request_client_node, PendingRequest, client_node);

			if (packet_is_matching_response(response, &pending_request->header)) {
				break;
			}

			pending_request_client_node = pending_request_client_node->next;
		}

		if (pending_request_client_node == &client->pending_request_sentinel) {
			pending_request = NULL;

			goto cleanup;
		}
	}

	if (client->disconnected) {
		log_debug("Ignoring disconnected client ("CLIENT_SIGNATURE_FORMAT")",
		          client_expand_signature(client));

		goto cleanup;
	}

	if (force || pending_request != NULL) {
		enqueued = writer_write(&client->response_writer, response);

		if (enqueued < 0) {
			goto cleanup;
		}

		if (force) {
			log_debug("Forced to %s response to client ("CLIENT_SIGNATURE_FORMAT")",
			          enqueued ? "enqueue" : "send", client_expand_signature(client));
		} else {
#ifdef BRICKD_WITH_PROFILING
			elapsed = microseconds() - pending_request->arrival_time;

			log_debug("%s response to client ("CLIENT_SIGNATURE_FORMAT"), was requested %u.%03u msec ago, %d request(s) still pending",
			          enqueued ? "Enqueued" : "Sent", client_expand_signature(client),
			          (unsigned int)(elapsed / 1000), (unsigned int)(elapsed % 1000),
			          client->pending_request_count - 1);
#else
			log_debug("%s response to client ("CLIENT_SIGNATURE_FORMAT"), %d request(s) still pending",
			          enqueued ? "Enqueued" : "Sent", client_expand_signature(client),
			          client->pending_request_count - 1);
#endif
		}
	}

cleanup:
	if (pending_request != NULL) {
		pending_request_remove_and_free(pending_request);
	}
}
