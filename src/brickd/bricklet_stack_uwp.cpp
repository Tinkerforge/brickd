/*
 * brickd
 * Copyright (C) 2019 Matthias Bolte <matthias@tinkerforge.com>
 *
 * bricklet_stack_uwp.cpp: Universal Windows Platform specific parts of SPI
 *                         Tinkerforge Protocol (SPITFP) implementation for
 *                         direct communication between brickd and Bricklet
 *                         with co-processor
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
#include <collection.h>
#include <ppltasks.h>

extern "C" {

#include <daemonlib/log.h>
#include <daemonlib/utils.h>

}

#include "bricklet_stack.h"

extern "C" {

#include "bricklet.h"

}

using namespace concurrency;
using namespace Platform;
using namespace Windows::Devices::Enumeration;
using namespace Windows::Devices::Gpio;
using namespace Windows::Devices::Spi;

#define BRICKLET_STACK_SPI_CONFIG_MAX_SPEED_HZ   1400000
#define BRICKLET_STACK_SPI_CONFIG_MODE           SpiMode::Mode3
#define BRICKLET_STACK_SPI_CONFIG_BITS_PER_WORD  8

struct _BrickletStackPlatform {
	GpioPin ^chip_select;
	SpiDevice ^spi_device;
};

static LogSource _log_source = LOG_SOURCE_INITIALIZER;
static BrickletStackPlatform _platform[BRICKLET_SPI_MAX_NUM * BRICKLET_CS_MAX_NUM];

extern "C" int bricklet_stack_create_platform(BrickletStack *bricklet_stack) {
	BrickletStackPlatform *platform = &_platform[bricklet_stack->config.index];

	memset(platform, 0, sizeof(BrickletStackPlatform));

	bricklet_stack->platform = platform;

	platform->chip_select = nullptr;

	// configure GPIO chip select
	if (bricklet_stack->config.chip_select_driver == BRICKLET_CHIP_SELECT_DRIVER_GPIO) {
		GpioController ^controller = GpioController::GetDefault();

		if (controller == nullptr) {
			log_error("GPIO not available");
			return -1;
		}

		try {
			platform->chip_select = controller->OpenPin(bricklet_stack->config.chip_select_gpio_num);
		} catch (...) { // FIXME: too generic
			log_error("Could not open GPIO pin %d: <exception>", bricklet_stack->config.chip_select_gpio_num); // FIXME
			return -1;
		}

		try {
			platform->chip_select->Write(GpioPinValue::High);
		} catch (...) { // FIXME: too generic
			log_error("Could not set GPIO pin %d to high: <exception>", bricklet_stack->config.chip_select_gpio_num); // FIXME

			delete platform->chip_select;
			platform->chip_select = nullptr;

			return -1;
		}

		try {
			platform->chip_select->SetDriveMode(GpioPinDriveMode::Output);
		} catch (...) { // FIXME: too generic
			log_error("Could not set GPIO pin %d to output: <exception>", bricklet_stack->config.chip_select_gpio_num); // FIXME

			delete platform->chip_select;
			platform->chip_select = nullptr;

			return -1;
		}
	}

	String ^selector = SpiDevice::GetDeviceSelector("SPI0");

	create_task(DeviceInformation::FindAllAsync(selector))
	.then([bricklet_stack](DeviceInformationCollection^ devices)
	{
		SpiConnectionSettings^ settings = ref new SpiConnectionSettings(bricklet_stack->config.chip_select_driver == BRICKLET_CHIP_SELECT_DRIVER_GPIO ? 0 : 1);

		settings->ClockFrequency = BRICKLET_STACK_SPI_CONFIG_MAX_SPEED_HZ;
		settings->Mode = BRICKLET_STACK_SPI_CONFIG_MODE;
		settings->DataBitLength = BRICKLET_STACK_SPI_CONFIG_BITS_PER_WORD;
		settings->SharingMode = SpiSharingMode::Shared;

		bricklet_stack->platform->spi_device = create_task(SpiDevice::FromIdAsync(devices->GetAt(0)->Id, settings)).get();
	}).wait();

	return 0;
}

extern "C" void bricklet_stack_destroy_platform(BrickletStack *bricklet_stack) {
	delete bricklet_stack->platform->spi_device;
	delete bricklet_stack->platform->chip_select;
}

extern "C" int bricklet_stack_chip_select_gpio(BrickletStack *bricklet_stack, bool enable) {
	try {
		bricklet_stack->platform->chip_select->Write(enable ? GpioPinValue::Low : GpioPinValue::High);
	} catch (...) { // FIXME: too generic
		log_error("Could not set GPIO pin %d to %s: <exception>", 
		          bricklet_stack->config.chip_select_gpio_num, enable ? "low" : "high"); // FIXME

		return -1;
	}

	return 0;
}

extern "C" int bricklet_stack_notify(BrickletStack *bricklet_stack) {
	uint8_t byte = 0;

	if (pipe_write(&bricklet_stack->notification_pipe, &byte, 1) < 0) {
		log_error("Could not write to Bricklet stack SPI notification pipe: %s (%d)",
		          get_errno_name(errno), errno);

		return -1;
	}

	return 0;
}

extern "C" int bricklet_stack_wait(BrickletStack *bricklet_stack) {
	uint8_t byte;

	if (pipe_read(&bricklet_stack->notification_pipe, &byte, 1) < 0) {
		if (errno_would_block()) {
			return -1; // no queue responses left
		}

		log_error("Could not read from SPI notification pipe: %s (%d)",
		          get_errno_name(errno), errno);

		return -1;
	}

	return 0;
}

extern "C" int bricklet_stack_spi_transceive(BrickletStack *bricklet_stack, uint8_t *write_buffer,
                                             uint8_t *read_buffer, int length) {
	Platform::Array<unsigned char>^ write_array = ref new Platform::Array<unsigned char>(length);
	Platform::Array<unsigned char>^ read_array = ref new Platform::Array<unsigned char>(length);

	memcpy(write_array->Data, write_buffer, length);
	
	bricklet_stack->platform->spi_device->TransferFullDuplex(write_array, read_array);

	memcpy(read_buffer, read_array->Data, length);

	delete write_array;
	delete read_array;
	
	return length;
}
