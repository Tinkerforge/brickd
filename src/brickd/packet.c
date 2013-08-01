/*
 * brickd
 * Copyright (C) 2012-2013 Matthias Bolte <matthias@tinkerforge.com>
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

#include "packet.h"

#include "log.h"

int packet_header_is_valid_request(PacketHeader *header, const char **message) {
	if (header->length < (int)sizeof(PacketHeader)) {
		*message = "Length is too small";

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
	if (packet->header.function_id == CALLBACK_ENUMERATE) {
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
	} else {
		return "";
	}
}

int packet_is_matching_response(Packet *packet, Packet *pending_request) {
	if (packet->header.uid != pending_request->header.uid) {
		return 0;
	}

	if (packet->header.function_id != pending_request->header.function_id) {
		return 0;
	}

	if (packet_header_get_sequence_number(&packet->header) !=
	    packet_header_get_sequence_number(&pending_request->header)) {
		return 0;
	}

	return 1;
}
