/*
 * brickd
 * Copyright (C) 2012-2014 Matthias Bolte <matthias@tinkerforge.com>
 *
 * packet.c: Packet definiton for protocol version 2
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

/*
 * functions for validating, packing, unpacking and comparing packets.
 */

#include <stdio.h>

#include "packet.h"

#include "macros.h"
#include "utils.h"

STATIC_ASSERT(sizeof(PacketHeader) == 8, "PacketHeader has invalid size");
STATIC_ASSERT(sizeof(Packet) == 80, "Packet has invalid size");
STATIC_ASSERT(sizeof(EnumerateCallback) == 34, "EnumerateCallback has invalid size");

int packet_header_is_valid_request(PacketHeader *header, const char **message) {
	if (header->length < (int)sizeof(PacketHeader)) {
		*message = "Length is too small";

		return 0;
	}

	if (header->length > (int)sizeof(Packet)) {
		*message = "Length is too big";

		return 0;
	}

	if (header->function_id == 0) {
		*message = "Invalid function ID";

		return 0;
	}

	if (packet_header_get_sequence_number(header) == 0) {
		*message = "Invalid sequence number";

		return 0;
	}

	return 1;
}

int packet_header_is_valid_response(PacketHeader *header, const char **message) {
	if (header->length < (int)sizeof(PacketHeader)) {
		*message = "Length is too small";

		return 0;
	}

	if (header->length > (int)sizeof(Packet)) {
		*message = "Length is too big";

		return 0;
	}

	if (uint32_from_le(header->uid) == 0) {
		*message = "Invalid UID";

		return 0;
	}

	if (header->function_id == 0) {
		*message = "Invalid function ID";

		return 0;
	}

	if (!packet_header_get_response_expected(header)) {
		*message = "Invalid response expected bit";

		return 0;
	}

	return 1;
}

uint8_t packet_header_get_sequence_number(PacketHeader *header) {
	return (header->sequence_number_and_options >> 4) & 0x0F;
}

void packet_header_set_sequence_number(PacketHeader *header, uint8_t sequence_number) {
	header->sequence_number_and_options |= (sequence_number << 4) & 0xF0;
}

uint8_t packet_header_get_response_expected(PacketHeader *header) {
	return (header->sequence_number_and_options >> 3) & 0x01;
}

uint8_t packet_header_get_error_code(PacketHeader *header) {
	return (header->error_code_and_future_use >> 6) & 0x03;
}

const char *packet_get_callback_type(Packet *packet) {
	if (packet->header.function_id != CALLBACK_ENUMERATE) {
		return "";
	}

	switch (((EnumerateCallback *)packet)->enumeration_type) {
	case ENUMERATION_TYPE_AVAILABLE:
		return "enumerate-available ";

	case ENUMERATION_TYPE_CONNECTED:
		return "enumerate-connected ";

	case ENUMERATION_TYPE_DISCONNECTED:
		return "enumerate-disconnected ";

	default:
		return "enumerate-<unknown> ";
	}
}

char *packet_get_request_signature(char *signature, Packet *packet) {
	char base58[BASE58_MAX_LENGTH];

	snprintf(signature, PACKET_MAX_SIGNATURE_LENGTH,
	         "U: %s, L: %u, F: %u, S: %u, R: %u",
	         base58_encode(base58, uint32_from_le(packet->header.uid)),
	         packet->header.length,
	         packet->header.function_id,
	         packet_header_get_sequence_number(&packet->header),
	         packet_header_get_response_expected(&packet->header));

	return signature;
}

char *packet_get_response_signature(char *signature, Packet *packet) {
	char base58[BASE58_MAX_LENGTH];

	snprintf(signature, PACKET_MAX_SIGNATURE_LENGTH,
	         "U: %s, L: %u, F: %u, S: %u, E: %u",
	         base58_encode(base58, uint32_from_le(packet->header.uid)),
	         packet->header.length,
	         packet->header.function_id,
	         packet_header_get_sequence_number(&packet->header),
	         packet_header_get_error_code(&packet->header));

	return signature;
}

char *packet_get_callback_signature(char *signature, Packet *packet) {
	char base58[BASE58_MAX_LENGTH];

	snprintf(signature, PACKET_MAX_SIGNATURE_LENGTH, "U: %s, L: %u, F: %u",
	         base58_encode(base58, uint32_from_le(packet->header.uid)),
	         packet->header.length,
	         packet->header.function_id);

	return signature;
}

int packet_is_matching_response(Packet *packet, PacketHeader *pending_request) {
	if (packet->header.uid != pending_request->uid) {
		return 0;
	}

	if (packet->header.function_id != pending_request->function_id) {
		return 0;
	}

	if (packet_header_get_sequence_number(&packet->header) !=
	    packet_header_get_sequence_number(pending_request)) {
		return 0;
	}

	return 1;
}
