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

#include "client.h"

#include "array.h"
#include "config.h"
#include "hardware.h"
#include "log.h"
#include "socket.h"
#include "utils.h"

#define LOG_CATEGORY LOG_CATEGORY_NETWORK

#define MAX_PENDING_REQUESTS 512
#define MAX_QUEUED_SENDS 512

static const char *_unknown_peer_name = "<unknown>";

typedef struct {
	PacketHeader header;
#ifdef BRICKD_WITH_PROFILING
	uint64_t arrival_time; // in usec
#endif
} PendingRequest;

static void client_handle_get_authentication_nonce_request(Client *client, GetAuthenticationNonceRequest *request) {
	GetAuthenticationNonceResponse response;

	if (client->authentication_state == CLIENT_AUTHENTICATION_STATE_DISABLED) {
		log_error("Client ("CLIENT_INFO_FORMAT") tries to authenticate, but authentication is disabled, disconnecting client",
		          client_expand_info(client));

		client->disconnected = 1;

		return;
	}

	if (client->authentication_state == CLIENT_AUTHENTICATION_STATE_DONE) {
		log_debug("Already authenticated client ("CLIENT_INFO_FORMAT") tries to authenticate again",
		          client_expand_info(client));

		client->authentication_state = CLIENT_AUTHENTICATION_STATE_ENABLED;
	}

	if (client->authentication_state != CLIENT_AUTHENTICATION_STATE_ENABLED) {
		log_error("Client ("CLIENT_INFO_FORMAT") performed invalid authentication sequence (%s -> %s), disconnecting client",
		          client_expand_info(client),
		          client_get_authentication_state_name(client->authentication_state),
		          client_get_authentication_state_name(CLIENT_AUTHENTICATION_STATE_NONCE_SEND));

		client->disconnected = 1;

		return;
	}

	response.header = request->header;
	response.header.length = sizeof(response);

	memcpy(response.server_nonce, &client->authentication_nonce, sizeof(response.server_nonce));

	if (client_dispatch_response(client, (Packet *)&response, 0, 1) > 0) {
		client->authentication_state = CLIENT_AUTHENTICATION_STATE_NONCE_SEND;
	}
}

static void client_handle_authenticate_request(Client *client, AuthenticateRequest *request) {
	uint32_t nonces[2];
	uint8_t digest[SHA1_DIGEST_LENGTH];
	char packet_signature[PACKET_MAX_SIGNATURE_LENGTH];
	AuthenticateResponse response;

	if (client->authentication_state == CLIENT_AUTHENTICATION_STATE_DISABLED) {
		log_error("Client ("CLIENT_INFO_FORMAT") tries to authenticate, but authentication is disabled, disconnecting client",
		          client_expand_info(client));

		client->disconnected = 1;

		return;
	}

	if (client->authentication_state != CLIENT_AUTHENTICATION_STATE_NONCE_SEND) {
		log_error("Client ("CLIENT_INFO_FORMAT") performed invalid authentication sequence (%s -> %s), disconnecting client",
		          client_expand_info(client),
		          client_get_authentication_state_name(client->authentication_state),
		          client_get_authentication_state_name(CLIENT_AUTHENTICATION_STATE_DONE));

		client->disconnected = 1;

		return;
	}

	memcpy(&nonces[0], &client->authentication_nonce, sizeof(client->authentication_nonce));
	memcpy(&nonces[1], request->client_nonce, sizeof(request->client_nonce));

	hmac_sha1((uint8_t *)config_get_authentication_secret(),
	          strlen(config_get_authentication_secret()),
	          (uint8_t *)nonces, sizeof(nonces), digest);

	if (memcmp(request->digest, digest, SHA1_DIGEST_LENGTH) != 0) {
		log_error("Authentication request (%s) of client ("CLIENT_INFO_FORMAT") did not contain the expected data, disconnecting client",
		          packet_get_request_signature(packet_signature, (Packet *)request),
		          client_expand_info(client));

		client->disconnected = 1;

		return;
	}

	client->authentication_state = CLIENT_AUTHENTICATION_STATE_DONE;

	log_info("Client ("CLIENT_INFO_FORMAT") successfully finished authentication",
	         client_expand_info(client));

	if (packet_header_get_response_expected(&request->header) != 0) {
		response.header = request->header;
		response.header.length = sizeof(response);

		packet_header_set_error_code(&response.header, ERROR_CODE_OK);

		client_dispatch_response(client, (Packet *)&response, 0, 0);
	}
}

static void client_handle_request(Client *client, Packet *request) {
	char packet_signature[PACKET_MAX_SIGNATURE_LENGTH];
	ErrorCodeResponse response;

	if (uint32_from_le(request->header.uid) == UID_BRICK_DAEMON) {
		if (request->header.function_id == FUNCTION_GET_AUTHENTICATION_NONCE) {
			if (request->header.length != sizeof(GetAuthenticationNonceRequest)) {
				log_error("Received authentication request (%s) from client ("CLIENT_INFO_FORMAT") with wrong length, disconnecting client",
				          packet_get_request_signature(packet_signature, request),
				          client_expand_info(client));

				client->disconnected = 1;

				return;
			}

			client_handle_get_authentication_nonce_request(client, (GetAuthenticationNonceRequest *)request);
		} else if (request->header.function_id == FUNCTION_AUTHENTICATE) {
			if (request->header.length != sizeof(AuthenticateRequest)) {
				log_error("Received authentication request (%s) from client ("CLIENT_INFO_FORMAT") with wrong length, disconnecting client",
				          packet_get_request_signature(packet_signature, request),
				          client_expand_info(client));

				client->disconnected = 1;

				return;
			}

			client_handle_authenticate_request(client, (AuthenticateRequest *)request);
		} else {
			response.header = request->header;
			response.header.length = sizeof(response);

			packet_header_set_error_code(&response.header,
			                             ERROR_CODE_FUNCTION_NOT_SUPPORTED);

			client_dispatch_response(client, (Packet *)&response, 0, 0);
		}
	} else if (client->authentication_state == CLIENT_AUTHENTICATION_STATE_DISABLED ||
	           client->authentication_state == CLIENT_AUTHENTICATION_STATE_DONE) {
		hardware_dispatch_request(request);
	} else {
		log_debug("Client ("CLIENT_INFO_FORMAT") is not authenticated, dropping request (%s)",
		          client_expand_info(client),
		          packet_get_request_signature(packet_signature, request));
	}
}

static void client_handle_send(void *opaque) {
	Client *client = opaque;
	Packet *response;
	char packet_signature[PACKET_MAX_SIGNATURE_LENGTH];

	if (client->send_queue.count == 0) {
		return;
	}

	response = queue_peek(&client->send_queue);

	if (socket_send(client->socket, response, response->header.length) < 0) {
		log_error("Could not send queued response (%s) to client ("CLIENT_INFO_FORMAT"), disconnecting client: %s (%d)",
		          packet_get_request_signature(packet_signature, response),
		          client_expand_info(client), get_errno_name(errno), errno);

		client->disconnected = 1;

		return;
	}

	queue_pop(&client->send_queue, NULL);

	log_debug("Sent queued response (%s) to client ("CLIENT_INFO_FORMAT"), %d response(s) left in send queue",
	          packet_get_request_signature(packet_signature, response),
	          client_expand_info(client), client->send_queue.count);

	if (client->send_queue.count == 0) {
		// last queued response handled, deregister for write events
		event_remove_source(client->socket->handle, EVENT_SOURCE_TYPE_GENERIC, EVENT_WRITE);
	}
}

static void client_handle_receive(void *opaque) {
	Client *client = opaque;
	int length;
	const char *message = NULL;
	char packet_signature[PACKET_MAX_SIGNATURE_LENGTH];
	PendingRequest *pending_request;

	length = socket_receive(client->socket,
	                        (uint8_t *)&client->request + client->request_used,
	                        sizeof(Packet) - client->request_used);

	if (length == 0) {
		log_info("Client ("CLIENT_INFO_FORMAT") disconnected by peer",
		         client_expand_info(client));

		client->disconnected = 1;

		return;
	}

	if (length < 0) {
		if (length == SOCKET_CONTINUE) {
			// no actual data received
		} else if (errno_interrupted()) {
			log_debug("Receiving from client ("CLIENT_INFO_FORMAT") was interrupted, retrying",
			          client_expand_info(client));
		} else if (errno_would_block()) {
			log_debug("Receiving from client ("CLIENT_INFO_FORMAT") would block, retrying",
			          client_expand_info(client));
		} else {
			log_error("Could not receive from client ("CLIENT_INFO_FORMAT"), disconnecting client: %s (%d)",
			          client_expand_info(client), get_errno_name(errno), errno);

			client->disconnected = 1;
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
				log_error("Got invalid request (%s) from client ("CLIENT_INFO_FORMAT"), disconnecting client: %s",
				          packet_get_request_signature(packet_signature, &client->request),
				          client_expand_info(client), message);

				client->disconnected = 1;

				return;
			}

			client->request_header_checked = 1;
		}

		length = client->request.header.length;

		if (client->request_used < length) {
			// wait for complete packet
			break;
		}

		if (client->request.header.function_id == FUNCTION_DISCONNECT_PROBE) {
			log_debug("Got disconnect probe from client ("CLIENT_INFO_FORMAT"), dropping request",
			          client_expand_info(client));
		} else {
			log_debug("Got request (%s) from client ("CLIENT_INFO_FORMAT")",
			          packet_get_request_signature(packet_signature, &client->request),
			          client_expand_info(client));

			if (packet_header_get_response_expected(&client->request.header)) {
				if (client->pending_requests.count >= MAX_PENDING_REQUESTS) {
					log_warn("Pending requests array of client ("CLIENT_INFO_FORMAT") is full, dropping %d pending request(s)",
					         client_expand_info(client),
					         client->pending_requests.count - MAX_PENDING_REQUESTS + 1);

					while (client->pending_requests.count >= MAX_PENDING_REQUESTS) {
						array_remove(&client->pending_requests, 0, NULL);
					}
				}

				pending_request = array_append(&client->pending_requests);

				if (pending_request == NULL) {
					log_error("Could not append to pending requests array of client ("CLIENT_INFO_FORMAT"): %s (%d)",
					          client_expand_info(client), get_errno_name(errno), errno);
				} else {
					memcpy(&pending_request->header, &client->request.header,
					       sizeof(PacketHeader));

#ifdef BRICKD_WITH_PROFILING
					pending_request->arrival_time = microseconds();
#endif

					log_debug("Added pending request (%s) for client ("CLIENT_INFO_FORMAT")",
					          packet_get_request_signature(packet_signature, &client->request),
					          client_expand_info(client));
				}
			}

			client_handle_request(client, &client->request);
		}

		memmove(&client->request, (uint8_t *)&client->request + length,
		        client->request_used - length);

		client->request_used -= length;
		client->request_header_checked = 0;
	}
}

const char *client_get_authentication_state_name(ClientAuthenticationState state) {
	switch (state) {
	case CLIENT_AUTHENTICATION_STATE_DISABLED:
		return "disabled";

	case CLIENT_AUTHENTICATION_STATE_ENABLED:
		return "enabled";

	case CLIENT_AUTHENTICATION_STATE_NONCE_SEND:
		return "nonce-send";

	case CLIENT_AUTHENTICATION_STATE_DONE:
		return "done";

	default:
		return "<unknown>";
	}
}

static int client_push_response_to_send_queue(Client *client, Packet *response) {
	Packet *queued_response;
	char packet_signature[PACKET_MAX_SIGNATURE_LENGTH];

	log_debug("Client ("CLIENT_INFO_FORMAT") is not ready to send, pushing response to send queue (count: %d +1)",
	          client_expand_info(client), client->send_queue.count);

	if (client->send_queue.count >= MAX_QUEUED_SENDS) {
		log_warn("Send queue of client ("CLIENT_INFO_FORMAT") is full, dropping %d queued response(s)",
		         client_expand_info(client),
		         client->send_queue.count - MAX_QUEUED_SENDS + 1);

		while (client->send_queue.count >= MAX_QUEUED_SENDS) {
			queue_pop(&client->send_queue, NULL);
		}
	}

	queued_response = queue_push(&client->send_queue);

	if (queued_response == NULL) {
		log_error("Could not push response (%s) to send queue of client ("CLIENT_INFO_FORMAT"), discarding response: %s (%d)",
		          packet_get_request_signature(packet_signature, response),
		          client_expand_info(client),
		          get_errno_name(errno), errno);

		return -1;
	}

	memcpy(queued_response, response, response->header.length);

	if (client->send_queue.count == 1) {
		// first queued response, register for write events
		if (event_add_source(client->socket->handle, EVENT_SOURCE_TYPE_GENERIC,
		                     EVENT_WRITE, client_handle_send, client) < 0) {
			// FIXME: how to handle this error?
			return -1;
		}
	}

	return 0;
}

int client_create(Client *client, Socket *socket,
                  struct sockaddr *address, socklen_t length,
                  uint32_t authentication_nonce) {
	int phase = 0;

	log_debug("Creating client from socket (handle: %d)", socket->handle);

	client->socket = socket;
	client->disconnected = 0;
	client->request_used = 0;
	client->request_header_checked = 0;
	client->authentication_state = CLIENT_AUTHENTICATION_STATE_DISABLED;
	client->authentication_nonce = authentication_nonce;

	if (config_get_authentication_secret() != NULL) {
		client->authentication_state = CLIENT_AUTHENTICATION_STATE_ENABLED;
	}

	// create pending request array
	if (array_create(&client->pending_requests, 32, sizeof(PendingRequest), 1) < 0) {
		log_error("Could not create pending request array: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 1;

	// create send queue
	if (queue_create(&client->send_queue, sizeof(Packet)) < 0) {
		log_error("Could not create send queue: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 2;

	// get peer name
	client->peer = socket_address_to_hostname(address, length);

	if (client->peer == NULL) {
		log_warn("Could not get peer name of client (socket: %d): %s (%d)",
		         socket->handle, get_errno_name(errno), errno);

		client->peer = (char *)_unknown_peer_name;

		goto cleanup;
	}

	phase = 3;

	// add socket as event source
	if (event_add_source(client->socket->handle, EVENT_SOURCE_TYPE_GENERIC,
	                     EVENT_READ, client_handle_receive, client) < 0) {
		goto cleanup;
	}

	phase = 4;

cleanup:
	switch (phase) { // no breaks, all cases fall through intentionally
	case 3:
		if (client->peer != _unknown_peer_name) {
			free(client->peer);
		}

	case 2:
		queue_destroy(&client->send_queue, NULL);

	case 1:
		array_destroy(&client->pending_requests, NULL);

	default:
		break;
	}

	return phase == 4 ? 0 : -1;
}

void client_destroy(Client *client) {
	if (client->pending_requests.count > 0) {
		log_warn("Destroying client ("CLIENT_INFO_FORMAT") while %d request(s) are still pending",
		         client_expand_info(client), client->pending_requests.count);
	}

	if (client->send_queue.count > 0) {
		log_warn("Destroying client ("CLIENT_INFO_FORMAT") while %d response(s) have not beed send",
		         client_expand_info(client), client->send_queue.count);
	}

	event_remove_source(client->socket->handle, EVENT_SOURCE_TYPE_GENERIC, -1);
	socket_destroy(client->socket);
	free(client->socket);

	if (client->peer != _unknown_peer_name) {
		free(client->peer);
	}

	array_destroy(&client->pending_requests, NULL);
	queue_destroy(&client->send_queue, NULL);
}

// returns -1 on error, 0 if the response was not dispatched and 1 if it was dispatch
int client_dispatch_response(Client *client, Packet *response, int force,
                             int ignore_authentication) {
	int i;
	PendingRequest *pending_request = NULL;
	int found = -1;
	int enqueued = 0;
	char packet_signature[PACKET_MAX_SIGNATURE_LENGTH];
	int rc = -1;
#ifdef BRICKD_WITH_PROFILING
	uint64_t elapsed;
#endif

	if (client->disconnected) {
		log_debug("Ignoring disconnected client ("CLIENT_INFO_FORMAT")",
		          client_expand_info(client));

		return 0;
	}

	if (!ignore_authentication &&
	    client->authentication_state != CLIENT_AUTHENTICATION_STATE_DISABLED &&
	    client->authentication_state != CLIENT_AUTHENTICATION_STATE_DONE) {
		log_debug("Ignoring unauthenticated client ("CLIENT_INFO_FORMAT")",
		          client_expand_info(client));

		return 0;
	}

	if (!force) {
		for (i = 0; i < client->pending_requests.count; ++i) {
			pending_request = array_get(&client->pending_requests, i);

			if (packet_is_matching_response(response, &pending_request->header)) {
				found = i;

				break;
			}
		}
	}

	if (force || found >= 0) {
		if (client->send_queue.count > 0) {
			if (client_push_response_to_send_queue(client, response) < 0) {
				goto cleanup;
			}

			enqueued = 1;
		} else {
			if (socket_send(client->socket, response, response->header.length) < 0) {
				if (!errno_would_block()) {
					log_error("Could not send response (%s) to client ("CLIENT_INFO_FORMAT"), disconnecting client: %s (%d)",
					          packet_get_request_signature(packet_signature, response),
					          client_expand_info(client), get_errno_name(errno), errno);

					client->disconnected = 1;

					goto cleanup;
				}

				if (client_push_response_to_send_queue(client, response) < 0) {
					goto cleanup;
				}

				enqueued = 1;
			}
		}

		if (force) {
			log_debug("Forced to %s response to client ("CLIENT_INFO_FORMAT")",
			          enqueued ? "enqueue" : "send", client_expand_info(client));
		} else {
#ifdef BRICKD_WITH_PROFILING
			elapsed = microseconds() - pending_request->arrival_time;

			log_debug("%s response to client ("CLIENT_INFO_FORMAT"), was requested %u.%03u msec ago, %d request(s) still pending",
			          enqueued ? "Enqueue" : "Sent", client_expand_info(client),
			          (unsigned int)(elapsed / 1000), (unsigned int)(elapsed % 1000),
			          client->pending_requests.count - 1);
#else
			log_debug("%s response to client ("CLIENT_INFO_FORMAT"), %d request(s) still pending",
			          enqueued ? "Enqueue" : "Sent", client_expand_info(client),
			          client->pending_requests.count - 1);
#endif
		}
	}

	rc = 0;

cleanup:
	if (found >= 0) {
		array_remove(&client->pending_requests, found, NULL);

		if (rc == 0) {
			rc = 1;
		}
	}

	return rc;
}
