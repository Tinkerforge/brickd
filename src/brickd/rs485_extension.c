/*
 * brickd
 * Copyright (C) 2014 Ishraq Ibne Ashraf <ishraq@tinkerforge.com>
 * Copyright (C) 2014 Matthias Bolte <matthias@tinkerforge.com>
 *
 * rs485_extension.c: RS485 extension support for RED Brick
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
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <linux/types.h>
#include <sys/eventfd.h>
#include <time.h>
#include <sys/timerfd.h>
#include <termios.h>
#include <linux/serial.h>
#include <sys/ioctl.h>

#include <daemonlib/threads.h>
#include <daemonlib/packet.h>
#include <daemonlib/pipe.h>
#include <daemonlib/log.h>
#include <daemonlib/red_gpio.h>
#include <daemonlib/event.h>
#include <daemonlib/i2c_eeprom.h>

#include "rs485_extension.h"
#include "network.h"
#include "stack.h"
#include "hardware.h"

#define LOG_CATEGORY LOG_CATEGORY_RS485

#define RS485_EXTENSION_TYPE                                            2

// Modbus config stuffs
#define RS485_EXTENSION_MODBUS_CONFIG_LOCATION_TYPE                     0
#define RS485_EXTENSION_MODBUS_CONFIG_LOCATION_ADDRESS                  4
#define RS485_EXTENSION_MODBUS_CONFIG_LOCATION_SLAVE_ADDRESSES_START    100
#define RS485_EXTENSION_MODBUS_CONFIG_LOCATION_BAUDRATE                 400
#define RS485_EXTENSION_MODBUS_CONFIG_LOCATION_PARTIY                   404
#define RS485_EXTENSION_MODBUS_CONFIG_LOCATION_STOPBITS                 405
#define RS485_EXTENSION_MODBUS_MAX_SLAVES                               32
#define RS485_EXTENSION_MODBUS_FUNCTION_CODE                            100 // Custom modbus function

// Serial interface config stuffs
#define RECEIVE_BUFFER_SIZE                                             1048576 //1MB, in bytes
#define RS485_EXTENSION_SERIAL_DEVICE                                   "/dev/ttyS0"
#define RS485_EXTENSION_SERIAL_PARITY_NONE                              110
#define RS485_EXTENSION_SERIAL_PARITY_EVEN                              101
#define RS485_EXTENSION_SERIAL_PARITY_ODD                               111

// Time related constants
#define MASTER_TRIES                                                    1       // Times master retries a request
static const uint32_t RETRY_TIMEOUT_PACKETS = 86;

// Packet related constants
#define MODBUS_PACKET_HEADER_LENGTH      3
#define MODBUS_PACKET_FOOTER_LENGTH      2
#define MODBUS_PACKET_OVERHEAD           MODBUS_PACKET_HEADER_LENGTH+MODBUS_PACKET_FOOTER_LENGTH
#define TINKERFORGE_HEADER_LENGTH        8
#define MODBUS_PACKET_MAX_LENGTH         80+MODBUS_PACKET_OVERHEAD
#define LENGTH_INDEX_IN_MODBUS_PACKET    7

// Table of CRC values for high-order byte
static const uint8_t table_crc_hi[] = {
    0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0,
    0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41,
    0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0,
    0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40,
    0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1,
    0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41,
    0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1,
    0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41,
    0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0,
    0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40,
    0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1,
    0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40,
    0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0,
    0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40,
    0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0,
    0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40,
    0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0,
    0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41,
    0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0,
    0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41,
    0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0,
    0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40,
    0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1,
    0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41,
    0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0,
    0x80, 0x41, 0x00, 0xC1, 0x81, 0x40
};

// Table of CRC values for low-order byte
static const uint8_t table_crc_lo[] = {
    0x00, 0xC0, 0xC1, 0x01, 0xC3, 0x03, 0x02, 0xC2, 0xC6, 0x06,
    0x07, 0xC7, 0x05, 0xC5, 0xC4, 0x04, 0xCC, 0x0C, 0x0D, 0xCD,
    0x0F, 0xCF, 0xCE, 0x0E, 0x0A, 0xCA, 0xCB, 0x0B, 0xC9, 0x09,
    0x08, 0xC8, 0xD8, 0x18, 0x19, 0xD9, 0x1B, 0xDB, 0xDA, 0x1A,
    0x1E, 0xDE, 0xDF, 0x1F, 0xDD, 0x1D, 0x1C, 0xDC, 0x14, 0xD4,
    0xD5, 0x15, 0xD7, 0x17, 0x16, 0xD6, 0xD2, 0x12, 0x13, 0xD3,
    0x11, 0xD1, 0xD0, 0x10, 0xF0, 0x30, 0x31, 0xF1, 0x33, 0xF3,
    0xF2, 0x32, 0x36, 0xF6, 0xF7, 0x37, 0xF5, 0x35, 0x34, 0xF4,
    0x3C, 0xFC, 0xFD, 0x3D, 0xFF, 0x3F, 0x3E, 0xFE, 0xFA, 0x3A,
    0x3B, 0xFB, 0x39, 0xF9, 0xF8, 0x38, 0x28, 0xE8, 0xE9, 0x29,
    0xEB, 0x2B, 0x2A, 0xEA, 0xEE, 0x2E, 0x2F, 0xEF, 0x2D, 0xED,
    0xEC, 0x2C, 0xE4, 0x24, 0x25, 0xE5, 0x27, 0xE7, 0xE6, 0x26,
    0x22, 0xE2, 0xE3, 0x23, 0xE1, 0x21, 0x20, 0xE0, 0xA0, 0x60,
    0x61, 0xA1, 0x63, 0xA3, 0xA2, 0x62, 0x66, 0xA6, 0xA7, 0x67,
    0xA5, 0x65, 0x64, 0xA4, 0x6C, 0xAC, 0xAD, 0x6D, 0xAF, 0x6F,
    0x6E, 0xAE, 0xAA, 0x6A, 0x6B, 0xAB, 0x69, 0xA9, 0xA8, 0x68,
    0x78, 0xB8, 0xB9, 0x79, 0xBB, 0x7B, 0x7A, 0xBA, 0xBE, 0x7E,
    0x7F, 0xBF, 0x7D, 0xBD, 0xBC, 0x7C, 0xB4, 0x74, 0x75, 0xB5,
    0x77, 0xB7, 0xB6, 0x76, 0x72, 0xB2, 0xB3, 0x73, 0xB1, 0x71,
    0x70, 0xB0, 0x50, 0x90, 0x91, 0x51, 0x93, 0x53, 0x52, 0x92,
    0x96, 0x56, 0x57, 0x97, 0x55, 0x95, 0x94, 0x54, 0x9C, 0x5C,
    0x5D, 0x9D, 0x5F, 0x9F, 0x9E, 0x5E, 0x5A, 0x9A, 0x9B, 0x5B,
    0x99, 0x59, 0x58, 0x98, 0x88, 0x48, 0x49, 0x89, 0x4B, 0x8B,
    0x8A, 0x4A, 0x4E, 0x8E, 0x8F, 0x4F, 0x8D, 0x4D, 0x4C, 0x8C,
    0x44, 0x84, 0x85, 0x45, 0x87, 0x47, 0x46, 0x86, 0x82, 0x42,
    0x43, 0x83, 0x41, 0x81, 0x80, 0x40
};

// Data structure definitions
typedef struct {
	uint8_t slave_address;
	Packet packet;
} RS485ExtensionPacket;

typedef struct {
	Stack base;
	uint8_t slaves[RS485_EXTENSION_MODBUS_MAX_SLAVES];
	int slave_num;
	Queue packet_to_modbus_queue; // Packets from network subsystem to be sent through Modbus
} RS485Extension;

static bool _initialized = false;
static RS485Extension _rs485_extension;
static char packet_signature[PACKET_MAX_SIGNATURE_LENGTH];
static int _rs485_serial_fd; // Serial interface file descriptor

// Variables tracking current states
static Packet current_request;
static char current_request_as_byte_array[sizeof(Packet) + MODBUS_PACKET_OVERHEAD];
static uint8_t current_sequence_number = 0; // Current session sequence number
static int master_current_slave_to_process = 0; // Only used used by master
static int master_current_tries = 0; // For counting retries

// Saved configs from EEPROM
static uint32_t _modbus_serial_config_type;
static uint32_t _modbus_serial_config_address;
static uint32_t _modbus_serial_config_baudrate;
static uint8_t _modbus_serial_config_parity;
static uint8_t _modbus_serial_config_stopbits;

// Receive buffer
static uint8_t receive_buffer[RECEIVE_BUFFER_SIZE] = {0};
static int current_receive_buffer_index = 0;

// Events
static int _master_retry_event;

// Timers
static double MASTER_RETRY_TIMEOUT = 0;
static struct itimerspec master_retry_timer;

// Used as boolean
static uint8_t sent_current_request_from_queue = 0;
static uint8_t sent_ack_of_data_packet = 0;
static uint8_t send_verify_flag = 0;

// RX GPIO pin definitions
static GPIOPin _rx_pin; // Active low

// Function prototypes
uint16_t crc16(uint8_t*, uint16_t);
int rs485_extension_init(void);
int rs485_extension_serial_init(char*);
void verify_buffer(uint8_t*);
void send_modbus_packet(uint8_t, uint8_t, Packet*);
void init_rx_state(void);
void update_sequence_number(void);
void update_slave_to_process(void);
void rs485_serial_data_available_handler(void*);
void master_poll_slave(void);
void master_retry_timeout_handler(void*);
void rs485_extension_dispatch_to_modbus(Stack*, Packet*, Recipient*);
void end_current_request(void);
void disable_master_retry_timer(void);
void rs485_extension_exit(void);

// CRC16 function
uint16_t crc16(uint8_t *buffer, uint16_t buffer_length)
{
    uint8_t crc_hi = 0xFF; // High CRC byte initialized 
    uint8_t crc_lo = 0xFF; // Low CRC byte initialized 
    unsigned int i; // Will index into CRC lookup 

    // Pass through message buffer
    while (buffer_length--) {
        i = crc_hi ^ *buffer++; // Calculate the CRC 
        crc_hi = crc_lo ^ table_crc_hi[i];
        crc_lo = table_crc_lo[i];
    }
    return (crc_hi << 8 | crc_lo);
}

// Function for initializing the serial interface
int rs485_extension_serial_init(char* serial_interface) {
    // Device file opening flags
    int flags = O_RDWR | O_NOCTTY | O_NDELAY | O_EXCL | ASYNC_SPD_CUST | ASYNC_LOW_LATENCY;
    
    // Opening device file
    if ((_rs485_serial_fd = open(serial_interface, flags)) < 0) {
        log_error("RS485: Serial device open failed");
        return -1;
    }
    
    // Serial interface setup
    
    // Serial interface config struct
    struct termios serial_interface_config;
    struct serial_struct serial_config;
    tcgetattr(_rs485_serial_fd, &(serial_interface_config));
    memset(&serial_interface_config, 0, sizeof(serial_interface_config));
        
    // Control options
    serial_interface_config.c_cflag |= (CREAD | CLOCAL);
    serial_interface_config.c_cflag &= ~CSIZE;
    serial_interface_config.c_cflag |= CS8; // Setting data bits
    
    if(_modbus_serial_config_stopbits == 1) {
        serial_interface_config.c_cflag &=~ CSTOPB; // Setting one stop bits
    }
    else if(_modbus_serial_config_stopbits == 2) {
        serial_interface_config.c_cflag |= CSTOPB; // Setting two stop bits
    }
    else {
        log_error("RS485: Error in serial stop bits config");
        close(_rs485_serial_fd);
        return -1;
    }
    
    if(_modbus_serial_config_parity == RS485_EXTENSION_SERIAL_PARITY_NONE) {
        serial_interface_config.c_cflag &=~ PARENB; // parity disabled
    }
    else if(_modbus_serial_config_parity == RS485_EXTENSION_SERIAL_PARITY_EVEN) {
        /* Even */
        serial_interface_config.c_cflag |= PARENB;
        serial_interface_config.c_cflag &=~ PARODD;
    }
    else if(_modbus_serial_config_parity == RS485_EXTENSION_SERIAL_PARITY_ODD){
        /* Odd */
        serial_interface_config.c_cflag |= PARENB;
        serial_interface_config.c_cflag |= PARODD;
    }
    else {
        log_error("RS485: Error in serial parity config");
        close(_rs485_serial_fd);
        return -1;
    }
    
    // Setting the baudrate
    serial_config.reserved_char[0] = 0;
    if (ioctl(_rs485_serial_fd, TIOCGSERIAL, &serial_config) < 0) {
        log_error("Error setting RS485 serial baudrate");
        return -1;
    }
	serial_config.flags &= ~ASYNC_SPD_MASK;
    serial_config.flags |= ASYNC_SPD_CUST;
    serial_config.custom_divisor = (serial_config.baud_base + (_modbus_serial_config_baudrate / 2)) /
                                   _modbus_serial_config_baudrate;
    if (serial_config.custom_divisor < 1) {
        serial_config.custom_divisor = 1;
    }
    if (ioctl(_rs485_serial_fd, TIOCSSERIAL, &serial_config) < 0) {
        log_error("RS485: Error setting serial baudrate");
        return -1;
    }
    log_debug("\nRS485: Baudrate configured = %d\nBaudbase(BB) = %d\n\
Divisor(DIV) = %d\nActual baudrate(%d / %d) = %f",
             _modbus_serial_config_baudrate,
             serial_config.baud_base,
             serial_config.custom_divisor,
             serial_config.baud_base,
             serial_config.custom_divisor,
             (float)serial_config.baud_base / serial_config.custom_divisor);

    cfsetispeed(&serial_interface_config, B38400);
    cfsetospeed(&serial_interface_config, B38400);
    
    // Line options
    serial_interface_config.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG); // Raw input

    // Input options
    if(_modbus_serial_config_parity == RS485_EXTENSION_SERIAL_PARITY_NONE) {
        serial_interface_config.c_iflag &= ~INPCK; // Input check disabled
    }
    else {
        serial_interface_config.c_iflag |= INPCK; // Input check enabled
    }

    serial_interface_config.c_iflag &= ~(IXON | IXOFF | IXANY); // Software iflow control is disabled

    // Output options
    serial_interface_config.c_oflag &=~ OPOST;

    // Control character options
    serial_interface_config.c_cc[VMIN] = 0;
    serial_interface_config.c_cc[VTIME] = 0;

    tcsetattr(_rs485_serial_fd, TCSANOW, &serial_interface_config);

    // Flushing the buffer
    tcflush(_rs485_serial_fd, TCIOFLUSH);

    log_info("RS485: Serial interface initialized");

    return 0;
}

// Verify packet
void verify_buffer(uint8_t* receive_buffer) {
    int i = 0;
    uint32_t uid_from_packet;
    uint16_t crc16_calculated;
    uint16_t crc16_on_packet;
    Packet data_packet;

    // Check if partial or full packet
    if(current_receive_buffer_index < 8) {
        log_debug("RS485: Partial packet recieved. Length byte not available");
        return;
    }

    int packet_end_index = 7+((receive_buffer[7] - 5) + MODBUS_PACKET_FOOTER_LENGTH);
    if(current_receive_buffer_index <= packet_end_index) {
        log_debug("RS485: Partial packet recieved");
        return;
    }

    if(send_verify_flag) {
        for(i = 0; i <= packet_end_index; i++) {
            if(receive_buffer[i] != current_request_as_byte_array[i]) {
                // Abort current request and retry
                master_retry_timeout_handler(NULL);
                return;
            }
        }

        send_verify_flag = 0;

        if(sent_ack_of_data_packet) {
            // Request processing done. Move on to next slave.
            sent_ack_of_data_packet = 0;
            master_poll_slave();
            log_debug("RS485: Current request processed");
            return;
        }
        if(current_receive_buffer_index == packet_end_index+1) {
            // No more data in the receive buffer.
            // Set receive buffer index to head to get the response from the other side.
            current_receive_buffer_index = 0;
            log_debug("RS485: Current request processed. Waiting for response.");
            return;
        }
        if(current_receive_buffer_index > packet_end_index+1) {
            // More data in the receive buffer
            memmove(&receive_buffer[0],
                    &receive_buffer[packet_end_index+1],
                    current_receive_buffer_index - (packet_end_index+1));
            current_receive_buffer_index = current_receive_buffer_index - (packet_end_index+1);
            // A recursive call to handle the remaining bytes in the buffer
            verify_buffer(receive_buffer);
            log_debug("RS485: Partial data in the buffer");
            return;
        }
        // Undefined state, abort current request
        // Abort current request and retry
        master_retry_timeout_handler(NULL);
        log_error("RS485: Undefined receive buffer state");
        return;
    }

    memcpy(&uid_from_packet, &receive_buffer[3], sizeof(uint32_t));

    // Received empty packet from the other side
    if(receive_buffer[7] == 8 && receive_buffer[7] == 0 && uid_from_packet == 0) {
        // If configured as master
        if(_modbus_serial_config_address == 0) {
            // Checking Modbus address
            if(receive_buffer[0] != _rs485_extension.slaves[master_current_slave_to_process]){
                // Retry the current request
                master_retry_timeout_handler(NULL);
                log_error("RS485: Wrong address in received empty packet");
                return;
            }
            // Checking Modbus function code
            if(receive_buffer[1] != RS485_EXTENSION_MODBUS_FUNCTION_CODE) {
                // Retry the current request
                master_retry_timeout_handler(NULL);
                log_error("RS485: Wrong function code in received empty packet");
                return;
            }
            // Checking current sequence number
            if(receive_buffer[2] != current_sequence_number) {
                // Retry the current request
                master_retry_timeout_handler(NULL);
                log_error("RS485: Wrong sequence number in received empty packet");
                return;
            }
            // Checking the CRC16 checksum
            crc16_calculated = crc16(&receive_buffer[0], (packet_end_index - MODBUS_PACKET_FOOTER_LENGTH) + 1);
            crc16_on_packet = (receive_buffer[packet_end_index-1] << 8) |
                              receive_buffer[packet_end_index];
            if (crc16_calculated != crc16_on_packet) {
                // Retry the current request
                master_retry_timeout_handler(NULL);
                log_error("RS485: Wrong CRC16 checksum in received empty packet");
                return;
            }

            disable_master_retry_timer();
            log_debug("RS485: Empty packet received");

            if(sent_current_request_from_queue){
                queue_pop(&_rs485_extension.packet_to_modbus_queue, NULL);
            }

            // Updating recipient in the routing table
            memcpy(&uid_from_packet, &receive_buffer[3], sizeof(uint32_t));
            stack_add_recipient(&_rs485_extension.base, uid_from_packet, receive_buffer[0]);
            
            log_debug("RS485: Current request processed");
            
            // Move on to next slave
            master_poll_slave();
            return;
        }
        // If configured as a slave
        if(_modbus_serial_config_address > 0) {

        }
    }
    
    // Received data packet from the other side
    
    // If configured as master
    if(_modbus_serial_config_address == 0) {
        // Checking Modbus address
        if(receive_buffer[0] != _rs485_extension.slaves[master_current_slave_to_process]){
            // Retry the current request
            master_retry_timeout_handler(NULL);
            log_error("RS485: Wrong address in received data packet");
            return;
        }
        // Checking Modbus function code
        if(receive_buffer[1] != RS485_EXTENSION_MODBUS_FUNCTION_CODE) {
            // Retry the current request
            master_retry_timeout_handler(NULL);
            log_error("RS485: Wrong function code in received data packet");
            return;
        }
        // Checking current sequence number
        if(receive_buffer[2] != current_sequence_number) {
            // Retry the current request
            master_retry_timeout_handler(NULL);
            log_error("RS485: Wrong sequence number in received data packet");
            return;
        }
        // Checking the CRC16 checksum
        crc16_calculated = crc16(&receive_buffer[0], (packet_end_index - MODBUS_PACKET_FOOTER_LENGTH) + 1);
        crc16_on_packet = (receive_buffer[packet_end_index-1] << 8) |
                           receive_buffer[packet_end_index];
        if (crc16_calculated != crc16_on_packet) {
            // Retry the current request
            master_retry_timeout_handler(NULL);
            log_error("RS485: Wrong CRC16 checksum in received data packet");
            return;
        }

        disable_master_retry_timer();
        log_debug("RS485: Data packet received");

        // Send message into brickd dispatcher
        memset(&data_packet, 0, sizeof(Packet));
        memcpy(&data_packet, &receive_buffer[3], receive_buffer[7]);
        network_dispatch_response(&data_packet);
        log_debug("RS485: Dispatched packet to network subsystem");

        if(sent_current_request_from_queue){
            queue_pop(&_rs485_extension.packet_to_modbus_queue, NULL);
        }

        // Updating recipient in the routing table
        memcpy(&uid_from_packet, &receive_buffer[3], sizeof(uint32_t));
        stack_add_recipient(&_rs485_extension.base, uid_from_packet, receive_buffer[0]);

        // Send ACK to the slave
        memset(&current_request, 0, sizeof(Packet));
        current_request.header.length = 8;
        send_modbus_packet(_rs485_extension.slaves[master_current_slave_to_process],
                           current_sequence_number,
                           &current_request);

        sent_ack_of_data_packet = 1;
        return;
    }
    // If configured as a slave
    if(_modbus_serial_config_address > 0) {

    }
    // Undefined state
    
    // Retry current request
    master_retry_timeout_handler(NULL);
    log_error("RS485: Undefined packet receive state");
    return;
}

// Send Modbus packet
void send_modbus_packet(uint8_t device_address, uint8_t sequence_number, Packet* packet_to_send) {
    uint16_t packet_crc16 = 0;
    uint8_t crc16_first_byte_index = 0;
    int packet_size = packet_to_send->header.length + MODBUS_PACKET_OVERHEAD;
    uint8_t modbus_packet[packet_size];

    // Assemble Modbus packet header
    modbus_packet[0] = device_address;
    modbus_packet[1] = RS485_EXTENSION_MODBUS_FUNCTION_CODE;
    modbus_packet[2] = sequence_number;

    // Assemble Tinkerforge packet
    memcpy(&modbus_packet[3], packet_to_send, packet_to_send->header.length);

    // Calculating CRC16
    packet_crc16 = crc16(modbus_packet, packet_to_send->header.length + MODBUS_PACKET_HEADER_LENGTH);

    // Assemble the calculated CRC16
    crc16_first_byte_index = packet_to_send->header.length +
                             MODBUS_PACKET_HEADER_LENGTH;
    modbus_packet[crc16_first_byte_index] = packet_crc16 >> 8;
    modbus_packet[++crc16_first_byte_index] = packet_crc16 & 0x00FF;

    // Save the packet as byte array
    memcpy(&current_request_as_byte_array[0], &modbus_packet[0], packet_size);

    // Sending packet
    if ((write(_rs485_serial_fd, modbus_packet, sizeof(modbus_packet))) <= 0) {
        log_error("RS485: Error sending packet on interface");
        master_poll_slave();
        return;
    }

    // Set send verify flag
    send_verify_flag = 1;

    // Start the master retry timer
    master_retry_timer.it_interval.tv_sec = 0;
    master_retry_timer.it_interval.tv_nsec = 0;
    master_retry_timer.it_value.tv_sec = 0;
    master_retry_timer.it_value.tv_nsec = MASTER_RETRY_TIMEOUT;
    timerfd_settime(_master_retry_event, 0, &master_retry_timer, NULL);
    log_debug("RS485: Modbus packet sent");
}

// Initialize RX state
void init_rx_state(void) {
    _rx_pin.port_index = GPIO_PORT_B;
    _rx_pin.pin_index = GPIO_PIN_13;
    gpio_mux_configure(_rx_pin, GPIO_MUX_OUTPUT);
    gpio_output_clear(_rx_pin);
    log_info("RS485: Initialized RS485 RX state");
}

void disable_master_retry_timer() {
    uint64_t dummy_read_buffer = 0;
    read(_master_retry_event, &dummy_read_buffer, sizeof(uint64_t));
    master_retry_timer.it_interval.tv_sec = 0;
    master_retry_timer.it_interval.tv_nsec = 0;
    master_retry_timer.it_value.tv_sec = 0;
    master_retry_timer.it_value.tv_nsec = 0;
    timerfd_settime(_master_retry_event, 0, &master_retry_timer, NULL);
}

// New data available event handler
void rs485_serial_data_available_handler(void* opaque) {
	(void)opaque;

    // Check if there is space in the receive buffer
    if(current_receive_buffer_index >= (RECEIVE_BUFFER_SIZE - MODBUS_PACKET_MAX_LENGTH)) {
        log_warn("RS485: No more space in the receive buffer");
        master_poll_slave();
        return;
    }
    // Put newly received bytes on the specific index in receive buffer
    int bytes_received = read(_rs485_serial_fd,
                              &receive_buffer[current_receive_buffer_index],
                              (RECEIVE_BUFFER_SIZE - current_receive_buffer_index));
    if(bytes_received < 0) {
        return;
    }
    current_receive_buffer_index += bytes_received;
    verify_buffer(receive_buffer);
    return;
}

// Master polling slave event handler
void master_poll_slave() {
    // Disable timer and reset current request states
    disable_master_retry_timer();
    end_current_request();
    // Check out going queue if nothing to send from there then
    // send modbus packet without payload to current slave
    RS485ExtensionPacket* packet_to_modbus;
    packet_to_modbus = queue_peek(&_rs485_extension.packet_to_modbus_queue);
    
    if(packet_to_modbus == NULL) {
        // Since there are no packets to be sent from the queue
        // we poll a slave with an empty message

        // Update current slave to process
        if(master_current_slave_to_process > 0) {
            if (master_current_slave_to_process >= _rs485_extension.slave_num) {
                master_current_slave_to_process = 0;
            }
            else {
                master_current_slave_to_process++;
            }
        }
        log_debug("RS485: Updated current Modbus slave's index");

        // Update current sequence number
        if(++current_sequence_number >= 129) {
            current_sequence_number = 1;
        }
        log_debug("RS485: Updated current Modbus sequence number");

        // Flag indicating the request was not sent from the queue
        sent_current_request_from_queue = 0;

        // Reset flag that indicates ACK of data packet was sent
        sent_ack_of_data_packet = 0;

        // Resetting the number to retry a request
        master_current_tries = MASTER_TRIES - 1;

        // Update current request which is being sent
        memset(&current_request, 0, sizeof(Packet));
        current_request.header.length = 8;

        // The timer will be fired by the send function
        send_modbus_packet(_rs485_extension.slaves[master_current_slave_to_process],
                           current_sequence_number,
                           &current_request);
        log_debug("RS485: Sending empty packet to slave ID = %d, Sequence number = %d", 
                 _rs485_extension.slaves[master_current_slave_to_process],
                 current_sequence_number);
    }
    else {
        // Update current slave to process
        int i;
        for(i = 0; i < _rs485_extension.slave_num; i++){
            if(packet_to_modbus->slave_address == _rs485_extension.slaves[i]) {
                master_current_slave_to_process = i;
                log_debug("RS485: Updated current Modbus slave's index");
                break;
            }
        }

        // Update current sequence number
        if(++current_sequence_number >= 129) {
            current_sequence_number = 1;
        }
        log_debug("RS485: Updated current Modbus sequence number");

        // Flag indicating the request was not sent from the queue
        sent_current_request_from_queue = 1;

        // Reset flag that indicates ACK of data packet was sent
        sent_ack_of_data_packet = 0;

        // Resetting the number to retry a request
        master_current_tries = MASTER_TRIES - 1;

        // Update current request which is being sent
        memset(&current_request, 0, sizeof(Packet));
        current_request = packet_to_modbus->packet;

        // The timer will be fired by the send function
        send_modbus_packet(packet_to_modbus->slave_address,
                           current_sequence_number,
                           &current_request);
        log_debug("RS485: Sending packet from queue to slave ID = %d, Sequence number = %d", 
                  packet_to_modbus->slave_address,
                  current_sequence_number);
    }
}

// Master retry timeout event handler
void master_retry_timeout_handler(void* opaque) {
	(void)opaque;

    // Disable timer and reset current request states
    disable_master_retry_timer();
    end_current_request();

    if(master_current_tries == 0) {
        master_poll_slave();
        return;
    }

    // Resend request
    send_modbus_packet(_rs485_extension.slaves[master_current_slave_to_process],
                       current_sequence_number,
                       &current_request);
    
    master_current_tries --;
    
    log_debug("RS485: Retrying to send current request");
}

// New packet from brickd event loop is queued to be sent via Modbus
void rs485_extension_dispatch_to_modbus(Stack *stack, Packet *request, Recipient *recipient) {
	RS485ExtensionPacket *queued_request;
	(void)stack;

	if(request->header.uid == 0 || recipient == NULL) {
		int is;
        log_debug("RS485: Broadcasting to all available Modbus slaves");
		for(is = 0; is < _rs485_extension.slave_num; is++) {
            queued_request = queue_push(&_rs485_extension.packet_to_modbus_queue);
            queued_request->slave_address = _rs485_extension.slaves[is];
            memcpy(&queued_request->packet, request, request->header.length);
            log_debug("RS485: Packet is queued to be sent to slave %d over Modbus. Function signature = (%s)",
                      _rs485_extension.slaves[is],
                      packet_get_request_signature(packet_signature, request));
		}
	}
    else if (recipient != NULL) {
		queued_request = queue_push(&_rs485_extension.packet_to_modbus_queue);
		queued_request->slave_address = recipient->opaque;
		memcpy(&queued_request->packet, request, request->header.length);
		log_debug("RS485: Packet is queued to be send to slave %d over Modbus. Function signature = (%s)",
		          recipient->opaque,
		          packet_get_request_signature(packet_signature, request));
	}
}

// Used from data available handler to abort the current request
void end_current_request() {
    current_receive_buffer_index = 0;
    if(sent_current_request_from_queue) {
        queue_pop(&_rs485_extension.packet_to_modbus_queue, NULL);
    }
    tcflush(_rs485_serial_fd, TCIOFLUSH);
    log_debug("RS485: Current request ended");
}

// Init function called from central brickd code
int rs485_extension_init(void) {
    uint8_t _tmp_eeprom_read_buf[4];
    int _eeprom_read_status;
    int phase = 0;

    log_info("RS485: Checking presence of extension");

    // Modbus config: TYPE
    _eeprom_read_status =
    i2c_eeprom_read((uint16_t)RS485_EXTENSION_MODBUS_CONFIG_LOCATION_TYPE,
                    _tmp_eeprom_read_buf, 4);
    if (_eeprom_read_status <= 0) {
        log_error("RS485: EEPROM read error. Most probably no RS485 extension present");
		return 0;
    }
    _modbus_serial_config_type = (uint32_t)((_tmp_eeprom_read_buf[0] << 0) |
                                 (_tmp_eeprom_read_buf[1] << 8) |
                                 (_tmp_eeprom_read_buf[2] << 16) |
                                 (_tmp_eeprom_read_buf[3] << 24));

    if (_modbus_serial_config_type == RS485_EXTENSION_TYPE) {

        log_info("RS485: Initializing extension subsystem");
        
        // Create base stack
        if(stack_create(&_rs485_extension.base, "rs485_extension",
                        (StackDispatchRequestFunction)rs485_extension_dispatch_to_modbus) < 0) {
            log_error("RS485: Could not create base stack for extension: %s (%d)",
                      get_errno_name(errno), errno);

            goto cleanup;
        }

        phase = 1;
        
        // Add to stacks array
        if(hardware_add_stack(&_rs485_extension.base) < 0) {
            goto cleanup;
        }

        phase = 2;

        // Initialize modbus packet queue
        if(queue_create(&_rs485_extension.packet_to_modbus_queue, sizeof(RS485ExtensionPacket)) < 0) {
            log_error("RS485: Could not create Modbus queue: %s (%d)",
                    get_errno_name(errno), errno);
            goto cleanup;
        }

        // Reading and storing eeprom config
        
        // Modbus config: ADDRESS
        _eeprom_read_status =
        i2c_eeprom_read((uint16_t)RS485_EXTENSION_MODBUS_CONFIG_LOCATION_ADDRESS,
                        _tmp_eeprom_read_buf, 4);
        if (_eeprom_read_status <= 0) {
            log_error("RS485: Could not read config ADDRESS from EEPROM");
            goto cleanup;
        }
        _modbus_serial_config_address = (uint32_t)((_tmp_eeprom_read_buf[0] << 0) |
                                        (_tmp_eeprom_read_buf[1] << 8) |
                                        (_tmp_eeprom_read_buf[2] << 16) |
                                        (_tmp_eeprom_read_buf[3] << 24));
                                        
        // Modbus config: BAUDRATE
        _eeprom_read_status = i2c_eeprom_read((uint16_t)RS485_EXTENSION_MODBUS_CONFIG_LOCATION_BAUDRATE,
                                              _tmp_eeprom_read_buf, 4);
        if (_eeprom_read_status <= 0) {
            log_error("RS485: Could not read config BAUDRATE from EEPROM");
            goto cleanup;
        }
        _modbus_serial_config_baudrate = (uint32_t)((_tmp_eeprom_read_buf[0] << 0) |
                                         (_tmp_eeprom_read_buf[1] << 8) |
                                         (_tmp_eeprom_read_buf[2] << 16) |
                                         (_tmp_eeprom_read_buf[3] << 24));
        
        if(_modbus_serial_config_baudrate < 8) {
            log_error("RS485: Configured bit rate is too low");
            goto cleanup;
        }
        // Calculate time to send number of bytes of max Modbus packet length and receive same amount
        MASTER_RETRY_TIMEOUT = ((double)(RETRY_TIMEOUT_PACKETS /
                               (double)(_modbus_serial_config_baudrate / 8)) *
                               (double)1000000000) * (double)2;

        // Modbus config: PARITY
        _eeprom_read_status = i2c_eeprom_read((uint16_t)RS485_EXTENSION_MODBUS_CONFIG_LOCATION_PARTIY,
                                              _tmp_eeprom_read_buf, 1);
        if (_eeprom_read_status <= 0) {
            log_error("RS485: Could not read config PARITY from EEPROM");
            goto cleanup;
        }
        if(_tmp_eeprom_read_buf[0] == RS485_EXTENSION_SERIAL_PARITY_NONE) {
            _modbus_serial_config_parity = RS485_EXTENSION_SERIAL_PARITY_NONE;
        }
        else if (_tmp_eeprom_read_buf[0] == RS485_EXTENSION_SERIAL_PARITY_EVEN){
            _modbus_serial_config_parity = RS485_EXTENSION_SERIAL_PARITY_EVEN;
        }
        else {
            _modbus_serial_config_parity = RS485_EXTENSION_SERIAL_PARITY_ODD;
        }
    
        // Modbus config: STOPBITS
        _eeprom_read_status =
        i2c_eeprom_read((uint16_t)RS485_EXTENSION_MODBUS_CONFIG_LOCATION_STOPBITS,
                        _tmp_eeprom_read_buf, 1);
        if (_eeprom_read_status <= 0) {
            log_error("RS485: Could not read config STOPBITS from EEPROM");
            goto cleanup;
        }
        _modbus_serial_config_stopbits = _tmp_eeprom_read_buf[0];

        // Modbus config (if master): SLAVE ADDRESSES
        if(_modbus_serial_config_address == 0) {
            _rs485_extension.slave_num = 0;
            uint16_t _current_eeprom_location =
            RS485_EXTENSION_MODBUS_CONFIG_LOCATION_SLAVE_ADDRESSES_START;
            uint32_t _current_slave_address;

            _rs485_extension.slave_num = 0;

            do {
                _eeprom_read_status = i2c_eeprom_read(_current_eeprom_location, _tmp_eeprom_read_buf, 4);
                if (_eeprom_read_status <= 0) {
                    log_error("RS485: Could not read config SLAVE ADDRESSES from EEPROM");
                    goto cleanup;
                }
                _current_slave_address = (uint32_t)((_tmp_eeprom_read_buf[0] << 0) |
                                         (_tmp_eeprom_read_buf[1] << 8) |
                                         (_tmp_eeprom_read_buf[2] << 16) |
                                         (_tmp_eeprom_read_buf[3] << 24));
        
                if(_current_slave_address != 0) {
                    _rs485_extension.slaves[_rs485_extension.slave_num] = _current_slave_address;
                    _rs485_extension.slave_num ++;
                }
                _current_eeprom_location = _current_eeprom_location + 4;
            }
            while(_current_slave_address != 0 
                  && 
                  _rs485_extension.slave_num < RS485_EXTENSION_MODBUS_MAX_SLAVES);
        }
        
        // Configuring serial interface from the configs
        if(rs485_extension_serial_init(RS485_EXTENSION_SERIAL_DEVICE) < 0) {
            goto cleanup;
        }
        
        // Initial RS485 RX state
        init_rx_state();

        phase = 3;

        // Adding serial data available event
        if(event_add_source(_rs485_serial_fd, EVENT_SOURCE_TYPE_GENERIC,
                            EVENT_READ, rs485_serial_data_available_handler, NULL) < 0) {
            log_error("RS485: Could not add new serial data event");
            goto cleanup;
        }

        phase = 4;

        // Setup master retry timer
        _master_retry_event = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);

        if(!(_master_retry_event < 0)) {
            if(event_add_source(_master_retry_event, EVENT_SOURCE_TYPE_GENERIC,
                                EVENT_READ, master_retry_timeout_handler, NULL) < 0) {
                log_error("RS485: Could not add Modbus master retry notification pipe as event source");
                goto cleanup;
            }
        }
        else {
            log_error("RS485: Could not create Modbus master retry timer");
            goto cleanup;
        }

        phase = 5;

        // Get things going in case of a master with slaves configured
        if(_modbus_serial_config_address == 0 && _rs485_extension.slave_num > 0) {
            master_poll_slave();
        }
        else {
            log_warn("RS485: Master has no slaves configured");
            goto cleanup;
        }

        phase = 6;
        _initialized = true;
    }
    else {
        log_info("RS485: Extension not present");
        goto cleanup;
    }
    
    cleanup:
        switch (phase) { // no breaks, all cases fall through intentionally
            case 5:
                close(_master_retry_event);
                event_remove_source(_master_retry_event, EVENT_SOURCE_TYPE_GENERIC);
                
            case 4:
                close(_rs485_serial_fd);
                event_remove_source(_rs485_serial_fd, EVENT_SOURCE_TYPE_GENERIC);
            
            case 3:
                queue_destroy(&_rs485_extension.packet_to_modbus_queue, NULL);

            case 2:
                hardware_remove_stack(&_rs485_extension.base);

            case 1:
                stack_destroy(&_rs485_extension.base);

            default:
                break;
        }
    return phase == 6 ? 0 : -1;
}

// Exit function called from central brickd code
void rs485_extension_exit(void) {
	if (!_initialized) {
		return;
	}

	// Remove event as possible poll source
    event_remove_source(_rs485_serial_fd, EVENT_SOURCE_TYPE_GENERIC);
    event_remove_source(_master_retry_event, EVENT_SOURCE_TYPE_GENERIC);

	// We can also free the queue and stack now, nobody will use them anymore
	queue_destroy(&_rs485_extension.packet_to_modbus_queue, NULL);
    hardware_remove_stack(&_rs485_extension.base);
    stack_destroy(&_rs485_extension.base);

	// Close file descriptors
	close(_rs485_serial_fd);
    close(_master_retry_event);
}

