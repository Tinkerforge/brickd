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
#include <sys/ioctl.h>
#include <linux/types.h>
#include <sys/eventfd.h>
#include <time.h>
#include <sys/timerfd.h>
#include <termios.h>
#include <linux/serial.h>
//#include <stropts.h>
//#include <asm/termios.h>

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
#define RECEIVE_BUFFER_SIZE                                             524288 //0.5MB, in bytes
#define RS485_EXTENSION_SERIAL_DEVICE                                   "/dev/ttyS0"
#define RS485_EXTENSION_SERIAL_PARITY_NONE                              110
#define RS485_EXTENSION_SERIAL_PARITY_EVEN                              101
#define RS485_EXTENSION_SERIAL_PARITY_ODD                               111

// Time related constants
#define MASTER_POLL_SLAVE_TIMEOUT                                       8000000 // 8ms, in nano seconds
#define MASTER_RETRY_TIMEOUT                                            8000000 // 8ms, in nano seconds
#define PARTIAL_RECEIVE_TIMEOUT                                         MASTER_POLL_SLAVE_TIMEOUT/2
#define SEND_VERIFY_TIMEOUT                                             MASTER_POLL_SLAVE_TIMEOUT/2
#define MASTER_RETRIES                                                  4       // Times master retries a request
#define TIME_UNIT_SEC                                                   0
#define TIME_UNIT_NSEC                                                  1

// Packet check codes
#define PACKET_EMPTY_OK                  1
#define PACKET_DATA_OK                   2
#define PACKET_SEND_VERIFY_OK            3
#define PACKET_ERROR_ADDRESS            -1
#define PACKET_ERROR_FUNCTION_CODE      -2
#define PACKET_ERROR_SEQUENCE_NUMBER    -3
#define PACKET_ERROR_LENGTH             -4
#define PACKET_ERROR_LENGTH_PARTIAL     -5
#define PACKET_ERROR_CRC16              -6
#define PACKET_ERROR_SEND_VERIFY        -7

// Packet related constants
#define MODBUS_PACKET_HEADER_LENGTH      3
#define MODBUS_PACKET_FOOTER_LENGTH      2
#define MODBUS_PACKET_OVERHEAD           MODBUS_PACKET_HEADER_LENGTH+MODBUS_PACKET_FOOTER_LENGTH
#define TINKERFORGE_HEADER_LENGTH        8
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

// Variables tracking current stuffs
static Packet current_request;
static char current_request_as_byte_array[sizeof(Packet) + MODBUS_PACKET_OVERHEAD];
static uint8_t current_sequence_number = 0; // Current session sequence number
//static uint8_t previous_sequence_number = 0; // Used by slave
static int master_current_slave_to_process = 0; // Only used used by master
static unsigned int master_current_retry = 0; // For counting retries

// Saved configs from EEPROM
static uint32_t _modbus_serial_config_type;
static uint32_t _modbus_serial_config_address;
static uint32_t _modbus_serial_config_baudrate;
static uint8_t _modbus_serial_config_parity;
static uint8_t _modbus_serial_config_stopbits;
//static uint32_t _modbus_serial_config_slave_addresses[RS485_EXTENSION_MODBUS_MAX_SLAVES];

// Receive buffer
static uint8_t receive_buffer[RECEIVE_BUFFER_SIZE] = {0};
static int partial_receive_merge_index = 0;

// Events
static int _master_poll_slave_event;
static int _partial_receive_timeout_event;
static int _master_retry_event;
static int _send_verify_event;

// Timers
static struct itimerspec master_poll_slave_timer;
static struct itimerspec partial_receive_timer;
static struct itimerspec master_retry_timer;
static struct itimerspec send_verify_timer;

// Used as boolean
static uint8_t master_current_request_processed = 1;
static uint8_t sent_current_request_from_queue = 0;
static uint8_t partial_receive_flag = 0;
//static uint8_t previous_request_had_payload = 0;
static uint8_t send_verify_flag = 0;
static uint8_t sent_ack_of_data_packet = 0;

// TX/RX GPIO pin definitions
//static GPIOPin _tx_pin; // Active high
static GPIOPin _rx_pin; // Active low

// Function prototypes
uint16_t crc16(uint8_t*, uint16_t);
int rs485_extension_serial_init(char*);
int is_valid_packet(uint8_t*, int);
int send_modbus_packet(uint8_t, uint8_t, Packet*);
void init_tx_rx_state(void);
void update_sequence_number(void);
void update_slave_to_process(void);
void disable_all_timers(void);
void partial_receive_timeout_handler(void*);
void rs485_serial_data_available_handler(void*);
void master_poll_slave_timeout_handler(void*);
void master_retry_timeout_handler(void*);
void send_verify_timeout_handler(void*);
void setup_timer(struct itimerspec*, uint8_t, long);
int rs485_extension_dispatch_to_modbus(Stack*, Packet*, Recipient*);
void abort_current_request(void);
void handle_partial_receive(void);
int rs485_extension_init(void);
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
    serial_interface_config.c_cflag |= (CREAD | CLOCAL | CRTSCTS);
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
	serial_config.custom_divisor =
                                   (serial_config.baud_base + (_modbus_serial_config_baudrate / 2)) /
                                   _modbus_serial_config_baudrate;
    if (serial_config.custom_divisor < 1) {
        serial_config.custom_divisor = 1;
    }
    if (ioctl(_rs485_serial_fd, TIOCSSERIAL, &serial_config) < 0) {
        log_error("RS485: Error setting serial baudrate");
        return -1;
    }
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
    
    log_debug("RS485: Serial interface initialized");
    
    return 0;
}
uint64_t start, end;

// Verify received buffer
int is_valid_packet(uint8_t* buffer, int size) {
    if(send_verify_flag) {
        if(size < (buffer[7] + MODBUS_PACKET_OVERHEAD)) {
            //log_info("SEND VERIFY PARTIAL DETECTED, SIZE = %d, PKT_LENGTH = %d", size, buffer[7]);
            return PACKET_ERROR_LENGTH_PARTIAL;
        }
        if(size > (buffer[7] + MODBUS_PACKET_OVERHEAD)) {
            log_info("SEND VERIFY LENGTH ERROR DETECTED, SIZE = %d, PKT_LENGTH = %d, FID = %d", size, buffer[7], buffer[8]);
            
            {
                int i;
                for (i = 0; i < size; ++i) {
                    printf("%d %d\n", i, buffer[i]);
                }
            }
            
            return PACKET_ERROR_SEND_VERIFY;
        }

        int i = 0;
        for(i = 0; i < size; i++) {
            if(buffer[i] != current_request_as_byte_array[i]) {
                //log_info("SEND VERIFY ERROR DETECTED, SIZE = %d, PKT_LENGTH = %d", size, buffer[7]);
                return PACKET_ERROR_SEND_VERIFY;
            }
        }
        //log_info("SEND VERIFY OK DETECTED, SIZE = %d, PKT_LENGTH = %d", size, buffer[7]);
        return PACKET_SEND_VERIFY_OK;
    }
    
    // Checking Modbus device address
    if(
        (_modbus_serial_config_address == 0 &&
         buffer[0] == _rs485_extension.slaves[master_current_slave_to_process])
        ||
        (_modbus_serial_config_address > 0 &&
        buffer[0] ==_modbus_serial_config_address)
    ) {
        // Checking Modbus function code
        if(buffer[1] != RS485_EXTENSION_MODBUS_FUNCTION_CODE) {
            return PACKET_ERROR_FUNCTION_CODE;
        }
        // Checking current sequence number
        if(buffer[2] != current_sequence_number) {
            return PACKET_ERROR_SEQUENCE_NUMBER;
        }
        // Checking if size exceeds empty packet length
        if(size == 13) {
            // Checking length of payload packet
            if(buffer[7] != TINKERFORGE_HEADER_LENGTH) {
                return PACKET_ERROR_LENGTH;
            }
            // Checking the CRC16 checksum
            uint16_t crc16_calculated = crc16(buffer, size-MODBUS_PACKET_FOOTER_LENGTH);
            uint16_t crc16_on_packet = ((buffer[size-MODBUS_PACKET_FOOTER_LENGTH] << 8) |
                                        (buffer[size-(MODBUS_PACKET_FOOTER_LENGTH-1)]));
            if (crc16_calculated != crc16_on_packet) {
                return PACKET_ERROR_CRC16;
                
            }
            return PACKET_EMPTY_OK;
        }
        else {
            // Checking length of payload packet
            if(buffer[LENGTH_INDEX_IN_MODBUS_PACKET]+MODBUS_PACKET_OVERHEAD == size) {
                // Checking the CRC16 checksum
                uint16_t crc16_calculated = crc16(buffer, size-MODBUS_PACKET_FOOTER_LENGTH);
                uint16_t crc16_on_packet = ((buffer[size-MODBUS_PACKET_FOOTER_LENGTH] << 8) |
                                            (buffer[size-(MODBUS_PACKET_FOOTER_LENGTH-1)]));
                if (crc16_calculated != crc16_on_packet) {
                    return PACKET_ERROR_CRC16;
                    
                }
                return PACKET_DATA_OK;
            }
            else if(buffer[LENGTH_INDEX_IN_MODBUS_PACKET]+MODBUS_PACKET_OVERHEAD < size) {
                // Partial receive of data packet
                return PACKET_ERROR_LENGTH_PARTIAL;
            }
            else {
                return PACKET_ERROR_LENGTH;
            }
        }
    }
    else {
        
        
        log_info("RS485: %u usec, WHAT?! %d", (uint32_t)(end - start), buffer[0]);
        
                    {
                int i;
                for (i = 0; i < size; ++i) {
                    printf("xx %d %d\n", i, buffer[i]);
                }
            }
        //exit(0);
        return PACKET_ERROR_ADDRESS;
    }
}

// Send Modbus packet
int send_modbus_packet(uint8_t device_address, uint8_t sequence_number, Packet* packet_to_send) {
    int bytes_written = 0;
    uint16_t packet_crc16 = 0;
    uint8_t crc16_first_byte_index = 0;
    int packet_size = packet_to_send->header.length + MODBUS_PACKET_OVERHEAD;
    uint8_t modbus_packet[packet_size];
    
    //printf(">>>>>>>>>>>>>>>>>>>>> SEN %llu\n", microseconds());
    
    // Assemble Modbus packet header    
    modbus_packet[0] = device_address;
    modbus_packet[1] = RS485_EXTENSION_MODBUS_FUNCTION_CODE;
    modbus_packet[2] = sequence_number;
    
    // Assemble Tinkerforge packet header
    memcpy(&modbus_packet[3], &packet_to_send->header, sizeof(PacketHeader));
    
    // Assemble Tinkerforge packet payload (if any)
    memcpy(&modbus_packet[3+sizeof(PacketHeader)], &packet_to_send->payload,
           packet_to_send->header.length - TINKERFORGE_HEADER_LENGTH);

    // Calculating CRC16
    packet_crc16 = crc16(modbus_packet, packet_to_send->header.length + MODBUS_PACKET_HEADER_LENGTH);
    
    // Assemble the calculated CRC16 to the Modbus packet
    crc16_first_byte_index = packet_to_send->header.length +
                             MODBUS_PACKET_HEADER_LENGTH;
    modbus_packet[crc16_first_byte_index] = packet_crc16 >> 8;
    modbus_packet[++crc16_first_byte_index] = packet_crc16 & 0x00FF;

    // Enabling TX
    start = microseconds();
    //gpio_output_set(_tx_pin);
    
    // Sending packet
    bytes_written = write(_rs485_serial_fd, modbus_packet, sizeof(modbus_packet));
    
    if (bytes_written <= 0) {
        // Disabling TX
        //gpio_output_clear(_tx_pin);
        end = microseconds();
        send_verify_flag = 0;
        log_error("RS485: Error sending packet through serial interface");
        return -1;
    }

    // Save the packet as byte array
    memcpy(current_request_as_byte_array, modbus_packet, packet_size);

    // Start the send verify timer
    setup_timer(&send_verify_timer, TIME_UNIT_NSEC, SEND_VERIFY_TIMEOUT);
    timerfd_settime(_send_verify_event, 0, &send_verify_timer, NULL);
    
    // Set send verify flag
    send_verify_flag = 1;
    
    log_debug("RS485: Packet sent through serial interface");
    
    return bytes_written;
}

// Initialize TX/RX state
void init_tx_rx_state(void) {
    //_tx_pin.port_index = GPIO_PORT_C;
    //_tx_pin.pin_index = GPIO_PIN_19;
    _rx_pin.port_index = GPIO_PORT_B;
    _rx_pin.pin_index = GPIO_PIN_13;
    
    //gpio_mux_configure(_tx_pin, GPIO_MUX_OUTPUT);
    gpio_mux_configure(_rx_pin, GPIO_MUX_OUTPUT);

    // By default, RX = always on and TX = enabled on demand
    //gpio_output_clear(_tx_pin);
    gpio_output_clear(_rx_pin);
    
    log_debug("RS485: Initialized RS485 TX/RX state");
}

// Update current sequence number
void update_sequence_number(void) {
    if(++current_sequence_number >= 129) {
        current_sequence_number = 1;
    }
    log_debug("RS485: Updated current Modbus sequence number");
}

// Update current slave to process
void update_slave_to_process(void) {
    if (++master_current_slave_to_process >= _rs485_extension.slave_num) {
        master_current_slave_to_process = 0;
    }
    log_debug("RS485: Updated current Modbus slave's index");
}

// Setup a specific timer
void setup_timer(struct itimerspec* target_timer, uint8_t time_unit, long time_amount) {
    if(time_unit == TIME_UNIT_SEC) {
        target_timer->it_interval.tv_sec = 0;
        target_timer->it_interval.tv_nsec = 0;
        target_timer->it_value.tv_sec = (time_t)time_amount;
        target_timer->it_value.tv_nsec = 0;
    }
    if(time_unit == TIME_UNIT_NSEC) {
        target_timer->it_interval.tv_sec = 0;
        target_timer->it_interval.tv_nsec = 0;
        target_timer->it_value.tv_sec = 0;
        target_timer->it_value.tv_nsec = time_amount;
    }
    log_debug("RS485: Setted up a timer");
}

// Function for disabling all timers
void disable_all_timers(void) {
    // Buffer for the dummy read
    uint64_t dummy_read_buffer = 0;

    // Make the event fd unreadable
    if(read(_master_retry_event, &dummy_read_buffer, sizeof(uint64_t))) {}
    if(read(_master_poll_slave_event, &dummy_read_buffer, sizeof(uint64_t))) {}
    if(read(_partial_receive_timeout_event, &dummy_read_buffer, sizeof(uint64_t))) {}
    if(read(_send_verify_event, &dummy_read_buffer, sizeof(uint64_t))) {}
    
    setup_timer(&master_retry_timer, TIME_UNIT_NSEC, 0);
    timerfd_settime(_master_retry_event, 0, &master_retry_timer, NULL);
    setup_timer(&master_poll_slave_timer, TIME_UNIT_NSEC, 0);
    timerfd_settime(_master_poll_slave_event, 0, &master_poll_slave_timer, NULL);
    setup_timer(&partial_receive_timer, TIME_UNIT_NSEC, 0);
    timerfd_settime(_partial_receive_timeout_event, 0, &partial_receive_timer, NULL);
    setup_timer(&send_verify_timer, TIME_UNIT_NSEC, 0);
    timerfd_settime(_send_verify_event, 0, &send_verify_timer, NULL);
    log_debug("RS485: Disabled all timers");
}

// Handle partial receive timeout
void partial_receive_timeout_handler(void* opaque) {
	(void)opaque;

    //log_info("RS485_EXTENSION : IN partial_receive_timeout_handler()");
    disable_all_timers();
    master_current_retry = MASTER_RETRIES;
    // Retry the current request
    master_retry_timeout_handler(NULL);
    log_debug("RS485: Handled partial data arrival");
}

// New data available event handler
void rs485_serial_data_available_handler(void* opaque) {
	(void)opaque;

    if(!send_verify_flag) {
        disable_all_timers();
    }

    int bytes_received = 0;
    uint32_t uid_from_packet = 0;
    int add_recipient_opaque;
    Packet empty_packet;
    Packet data_packet;
    uint64_t dummy_read_buffer;
    
    // Merge or simply save the received bytes
    if(partial_receive_flag) {
        bytes_received = read(_rs485_serial_fd,
                              &receive_buffer[partial_receive_merge_index],
                              (RECEIVE_BUFFER_SIZE - partial_receive_merge_index));

        
        printf("bytes_received %d\n", bytes_received);

        partial_receive_merge_index = partial_receive_merge_index + bytes_received;
    }
    else {
        partial_receive_merge_index = read(_rs485_serial_fd, receive_buffer, RECEIVE_BUFFER_SIZE);

    if (partial_receive_merge_index != 13)
        printf("partial_receive_merge_index %d\n", partial_receive_merge_index);
    }
    //log_info("PARTIAL_RECEIVE_MERGE_INDEX = %d", partial_receive_merge_index);
    // Checks at least 13 bytes is available in the receive buffer
    if(partial_receive_merge_index >= 13) {
        int packet_validation_code =
        is_valid_packet(receive_buffer, partial_receive_merge_index);
        
        switch(packet_validation_code) {
            case PACKET_SEND_VERIFY_OK:
                // Stop send verify timer
                if (read(_send_verify_event, &dummy_read_buffer, sizeof(uint64_t))) {}
                setup_timer(&send_verify_timer, TIME_UNIT_NSEC, 0);
                timerfd_settime(_send_verify_event, 0, &send_verify_timer, NULL);
                // Disabling TX
                //gpio_output_clear(_tx_pin);
                end = microseconds();
                // Clearing send verify flag
                send_verify_flag = 0;
                // Clearing partial receive flag
                partial_receive_flag = 0;
                
                if(sent_ack_of_data_packet) {
                    sent_ack_of_data_packet = 0;
                    master_current_request_processed = 1;
                    master_poll_slave_timeout_handler(NULL);
                }
                log_debug("RS485: Send verified");
                return;
            
            case PACKET_EMPTY_OK:
                // Proper empty packet
                log_debug("RS485: Empty packet received");
                
                if(sent_current_request_from_queue){
                    queue_pop(&_rs485_extension.packet_to_modbus_queue, NULL);
                    sent_current_request_from_queue = 0;
                }
                // Updating recipient in the routing table
                memcpy(&uid_from_packet, &receive_buffer[3], sizeof(uint32_t));
                stack_add_recipient(&_rs485_extension.base, uid_from_packet, receive_buffer[0]);
                
                partial_receive_flag = 0;
                master_current_request_processed = 1;
                if(sent_ack_of_data_packet) {
                    sent_ack_of_data_packet = 0;
                }
                master_poll_slave_timeout_handler(NULL);
                return;
            
            case PACKET_DATA_OK:
                // Proper data packet
                log_info("RS485: Data packet received");
                
                // Send message into brickd dispatcher
                memset(&data_packet, 0, sizeof(Packet));
                memcpy(&data_packet, &receive_buffer[3], partial_receive_merge_index - MODBUS_PACKET_OVERHEAD);
                network_dispatch_response(&data_packet);
                log_debug("RS485: Dispatched packet to network subsystem");
                
                if(sent_current_request_from_queue){
                    queue_pop(&_rs485_extension.packet_to_modbus_queue, NULL);
                    sent_current_request_from_queue = 0;
                }
                
                // Updating recipient in the routing table
                memcpy(&uid_from_packet, &receive_buffer[3], sizeof(uint32_t));
                add_recipient_opaque = receive_buffer[0];
                stack_add_recipient(&_rs485_extension.base, uid_from_packet, add_recipient_opaque);
                
                // Send ACK to the slave
                memset(&empty_packet, 0, sizeof(PacketHeader));
                empty_packet.header.length = 8;
                send_modbus_packet(_rs485_extension.slaves[master_current_slave_to_process],
                                   current_sequence_number,
                                   &empty_packet);
                sent_ack_of_data_packet = 1;
                return;
            
            case PACKET_ERROR_SEND_VERIFY:
                // Retry the current request
                log_error("RS485: Send verify failed");
                partial_receive_flag = 0;
                master_current_retry = MASTER_RETRIES;
                master_retry_timeout_handler(NULL);
                return;
                
            case PACKET_ERROR_ADDRESS:
                // Retry the current request
                log_error("RS485: Wrong address in packet");
                partial_receive_flag = 0;
                master_current_retry = MASTER_RETRIES;
                master_retry_timeout_handler(NULL);
                return;
                
            case PACKET_ERROR_FUNCTION_CODE:
                log_error("RS485: Wrong function code in packet");
                partial_receive_flag = 0;
                master_current_retry = MASTER_RETRIES;
                master_retry_timeout_handler(NULL);
                return;
                
            case PACKET_ERROR_SEQUENCE_NUMBER:
                // Retry the current request
                log_info("RS485: Wrong sequence number in packet");
                partial_receive_flag = 0;
                master_current_retry = MASTER_RETRIES;
                master_retry_timeout_handler(NULL);
                return;
                
            case PACKET_ERROR_LENGTH:
                // Retry the current request
                log_error("RS485: Wrong length in packet");
                partial_receive_flag = 0;
                master_current_retry = MASTER_RETRIES;
                master_retry_timeout_handler(NULL);
                return;
            
            case PACKET_ERROR_LENGTH_PARTIAL:
                log_debug("RS485: Partial data packet recieved");
                handle_partial_receive();
                return;
            
            case PACKET_ERROR_CRC16:
                // Retry the current request
                log_error("RS485: Wrong CRC16 in packet");
                partial_receive_flag = 0;
                master_current_retry = MASTER_RETRIES;
                master_retry_timeout_handler(NULL);
                return;
            
            default:
                return;
        }
    }
    else {
        log_debug("RS485: Partial packet recieved");
        handle_partial_receive();
        return;
    }
    if(send_verify_flag) {
        // Disabling TX
        //gpio_output_clear(_tx_pin);
        end = microseconds();
        // Stop send verify timer
        if (read(_send_verify_event, &dummy_read_buffer, sizeof(uint64_t))) {}
        setup_timer(&send_verify_timer, TIME_UNIT_NSEC, 0);
        timerfd_settime(_send_verify_event, 0, &send_verify_timer, NULL);
        // Clearing send verify flag
        send_verify_flag = 0;
    }
    abort_current_request();
    return;
}

// Master polling slave event handler
void master_poll_slave_timeout_handler(void* opaque) {
	(void)opaque;

    // Turning off the timers
    disable_all_timers();

    // Basically for situation when just after sending packet from this
    //function nothing comes back and master_poll_slave_timer times out
    if (!master_current_request_processed) {
        master_current_retry = MASTER_RETRIES;
        master_retry_timeout_handler(NULL);
        return;
    }
    
    // Check out going queue if nothing to send from there then
    // send modbus packet without payload to current slave
    RS485ExtensionPacket* packet_to_modbus;
    packet_to_modbus = queue_peek(&_rs485_extension.packet_to_modbus_queue);
    
    if(packet_to_modbus == NULL) {
        // Incase there are no slaves configured for this master
        if(_rs485_extension.slave_num == 0) {
            // Re-enabling the timer
            partial_receive_flag = 0;
            master_current_request_processed = 1;
            //setup_timer(&master_poll_slave_timer, TIME_UNIT_NSEC, MASTER_POLL_SLAVE_TIMEOUT);
            return;
        }
        // Since there are no packets to be sent from the queue
        // we poll current slave with an empty message
        Packet empty_packet;
        memset(&empty_packet, 0, sizeof(PacketHeader));
        empty_packet.header.length = 8;
    
        // Update current request which is being sent
        current_request = empty_packet;
    
        // Update current sequence number
        update_sequence_number();
        
        // Update current slave to process
        update_slave_to_process();
        
        int bytes_sent = send_modbus_packet(_rs485_extension.slaves[master_current_slave_to_process],
                                            current_sequence_number, &empty_packet);
        log_debug("RS485: Sending empty packet to slave ID = %d, Sequence number = %d, Bytes sent = %d", 
                 _rs485_extension.slaves[master_current_slave_to_process],
                 current_sequence_number,
                 bytes_sent);
    }
    else {
        // Update current request which is being sent
        current_request = packet_to_modbus->packet;
        
        // Update current sequence number
        update_sequence_number();
        
        // Update current slave to process
        update_slave_to_process();
        int bytes_sent = send_modbus_packet(packet_to_modbus->slave_address,
                                            current_sequence_number,
                                            &packet_to_modbus->packet);
        log_debug("RS485: Sending packet from queue to slave ID = %d, Sequence number = %d, Bytes sent = %d", 
                 packet_to_modbus->slave_address,
                 current_sequence_number,
                 bytes_sent); 
        sent_current_request_from_queue = 1;
    }
    
    partial_receive_flag = 0;
    master_current_request_processed = 0;
    // Re-enabling the timer
    setup_timer(&master_poll_slave_timer, TIME_UNIT_NSEC, MASTER_POLL_SLAVE_TIMEOUT);
    timerfd_settime(_master_poll_slave_event, 0, &master_poll_slave_timer, NULL);
}

// Master retry event handler
void master_retry_timeout_handler(void* opaque) {
	(void)opaque;

    // Turning off the timers
    disable_all_timers();
    
    if(master_current_retry <= 0) {
        if(send_verify_flag) {
            // Disabling TX
            //gpio_output_clear(_tx_pin);
            end = microseconds();
            // Clearing send verify flag
            send_verify_flag = 0;
        }
        if(sent_current_request_from_queue) {
            queue_pop(&_rs485_extension.packet_to_modbus_queue, NULL);
            sent_current_request_from_queue = 0;
        }
        partial_receive_flag = 0;
        master_current_request_processed = 1;
        master_poll_slave_timeout_handler(NULL);
        return;
    }

    // Resend request    
    partial_receive_flag = 0;
    master_current_request_processed = 0;
    send_modbus_packet(_rs485_extension.slaves[master_current_slave_to_process],
                       current_sequence_number,
                       &current_request);
    
    log_debug("RS485: Retrying to send current request");
    
    // Decrease current retry
    master_current_retry --;
    
    // Start master retry timer
    setup_timer(&master_retry_timer, TIME_UNIT_NSEC, MASTER_RETRY_TIMEOUT);
    timerfd_settime(_master_retry_event, 0, &master_retry_timer, NULL);
}

// Send verify timeout event handler
void send_verify_timeout_handler(void *opaque) {
	(void)opaque;

    // Disabling TX
    //gpio_output_clear(_tx_pin);
    end = microseconds();
    // Disabling timers
    disable_all_timers();
    // Clearing send verify flag
    send_verify_flag = 0;
    sent_ack_of_data_packet = 0;
    // Start slave poll timer in case of master
    if(_modbus_serial_config_address == 0 ) {
        master_current_request_processed = 1;
        master_poll_slave_timeout_handler(NULL);
    }
    log_error("RS485: Error sending packet on serial interface");
}

// New packet from brickd event loop is queued to be sent via Modbus
int rs485_extension_dispatch_to_modbus(Stack *stack, Packet *request, Recipient *recipient) {
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

	return 0;
}

// Used from data available handler to abort the current request
void abort_current_request() {
    if(sent_current_request_from_queue) {
        queue_pop(&_rs485_extension.packet_to_modbus_queue, NULL);
        sent_current_request_from_queue = 0;
    }
    
    sent_ack_of_data_packet = 0;
    partial_receive_flag = 0;
    master_current_request_processed = 1;
    // Flushing serial interface buffer
    tcflush(_rs485_serial_fd, TCIOFLUSH);
    
    master_poll_slave_timeout_handler(NULL);
    log_error("RS485: Aborted current request");
}

// Used from data available handler to handle partial receive situation
void handle_partial_receive() {
    // Setting up partial receive timer
    setup_timer(&partial_receive_timer, TIME_UNIT_NSEC, PARTIAL_RECEIVE_TIMEOUT);
    timerfd_settime(_partial_receive_timeout_event, 0, &partial_receive_timer, NULL);
    partial_receive_flag = 1;
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
                        rs485_extension_dispatch_to_modbus) < 0) {
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
        
        // Initial RS485 TX/RX state
        init_tx_rx_state();
        
        phase = 3;

        // Setup partial data receive timer        
        setup_timer(&partial_receive_timer, TIME_UNIT_NSEC, PARTIAL_RECEIVE_TIMEOUT);
        _partial_receive_timeout_event = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
        
        if(!(_partial_receive_timeout_event < 0)) {
            if(event_add_source(_partial_receive_timeout_event, EVENT_SOURCE_TYPE_GENERIC,
                                EVENT_READ, partial_receive_timeout_handler, NULL) < 0) {
                log_error("RS485: Could not add partial receive timeout notification pipe as event source");
                goto cleanup;
            }
        }
        else {
            log_error("RS485: Could not create partial receive timer");
            goto cleanup;
        }

        phase = 4;

        // Adding serial data available event
        if(event_add_source(_rs485_serial_fd, EVENT_SOURCE_TYPE_GENERIC,
                            EVENT_READ, rs485_serial_data_available_handler, NULL) < 0) {
            log_error("RS485: Could not add new serial data event");
            goto cleanup;
        }

        phase = 5;

        // Setup master retry timer        
        setup_timer(&master_retry_timer, TIME_UNIT_NSEC, MASTER_RETRY_TIMEOUT);
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

        phase = 6;

        // Setup send verify timer
        setup_timer(&send_verify_timer, TIME_UNIT_NSEC, SEND_VERIFY_TIMEOUT);
        _send_verify_event = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
        
        if(!(_send_verify_event < 0)) {
            if(event_add_source(_send_verify_event, EVENT_SOURCE_TYPE_GENERIC,
                                EVENT_READ, send_verify_timeout_handler, NULL) < 0) {
                log_error("RS485: Could not add Modbus send verify notification pipe as event source");
                goto cleanup;
            }
        }
        else {
            log_error("RS485: Could not create Modbus send verify timer");
            goto cleanup;
        }

        phase = 7;

        // Get things going in case of a master
        if(_modbus_serial_config_address == 0 && _rs485_extension.slave_num > 0) {
            // Setup master poll slave timer
            setup_timer(&master_poll_slave_timer, TIME_UNIT_NSEC, MASTER_POLL_SLAVE_TIMEOUT);
            _master_poll_slave_event = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
            if(!(_master_poll_slave_event < 0) ) {
                if(event_add_source(_master_poll_slave_event, EVENT_SOURCE_TYPE_GENERIC,
                                    EVENT_READ, master_poll_slave_timeout_handler, NULL) < 0) {
                    log_error("RS485: Could not add Modbus master poll slave notification pipe as event source");
                    goto cleanup;
                }
            }
            else {
                log_error("RS485: Could not create Modbus master poll slave timer");
                goto cleanup;
            }
            master_poll_slave_timeout_handler(NULL);
        }

        phase = 8;
        _initialized = true;
    }
    else {
        log_info("RS485: Extension not present");
        goto cleanup;
    }
    
    cleanup:
        switch (phase) { // no breaks, all cases fall through intentionally
            case 7:
                close(_send_verify_event);
                event_remove_source(_send_verify_event, EVENT_SOURCE_TYPE_GENERIC);
                
            case 6:
                close(_master_retry_event);
                event_remove_source(_master_retry_event, EVENT_SOURCE_TYPE_GENERIC);
            
            case 5:
                close(_rs485_serial_fd);
                event_remove_source(_rs485_serial_fd, EVENT_SOURCE_TYPE_GENERIC);

            case 4:
                close(_partial_receive_timeout_event);
                event_remove_source(_partial_receive_timeout_event, EVENT_SOURCE_TYPE_GENERIC);

            case 3:
                queue_destroy(&_rs485_extension.packet_to_modbus_queue, NULL);
                
            case 2:
                hardware_remove_stack(&_rs485_extension.base);

            case 1:
                stack_destroy(&_rs485_extension.base);

            default:
                break;
        }
    return phase == 8 ? 0 : -1;
}

// Exit function called from central brickd code
void rs485_extension_exit(void) {
	if (!_initialized) {
		return;
	}

	// Remove event as possible poll source
    event_remove_source(_send_verify_event, EVENT_SOURCE_TYPE_GENERIC);
    event_remove_source(_master_poll_slave_event, EVENT_SOURCE_TYPE_GENERIC);
    event_remove_source(_rs485_serial_fd, EVENT_SOURCE_TYPE_GENERIC);
    event_remove_source(_master_retry_event, EVENT_SOURCE_TYPE_GENERIC);
    event_remove_source(_partial_receive_timeout_event, EVENT_SOURCE_TYPE_GENERIC);

	// We can also free the queue and stack now, nobody will use them anymore
	queue_destroy(&_rs485_extension.packet_to_modbus_queue, NULL);
    hardware_remove_stack(&_rs485_extension.base);
    stack_destroy(&_rs485_extension.base);

	// Close file descriptors
    close(_send_verify_event);
    close(_master_poll_slave_event);
    close(_partial_receive_timeout_event);
    close(_master_retry_event);
	close(_rs485_serial_fd);
}
