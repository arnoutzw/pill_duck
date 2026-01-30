// vim: tabstop=8 softtabstop=8 shiftwidth=8 noexpandtab
/**
 * @file main.c
 * @brief Pill Duck firmware - Main entry point and core functionality
 *
 * This is the main firmware file for the Pill Duck USB HID device, a
 * scriptable USB keyboard/mouse emulator for the STM32F103 "Blue Pill"
 * microcontroller board. The device is inspired by the USB Rubber Ducky.
 *
 * ## Features
 *
 * - **USB Composite Device**: Presents as both HID (keyboard/mouse) and
 *   CDC ACM (serial port) to the host
 * - **Script Storage**: Stores HID report sequences in internal flash
 * - **DuckyScript Support**: Converts compiled DuckyScript binary format
 * - **Mouse Jiggler**: Built-in pattern to prevent screen lock
 * - **Interactive Control**: Pause, resume, and single-step execution
 *
 * ## Architecture Overview
 *
 * ```
 * +------------------+      +------------------+
 * |   USB Host PC    |      |   Pill Duck      |
 * +------------------+      +------------------+
 * |                  |      |                  |
 * | HID Driver  <----+------+-> HID Interface  | (keyboard/mouse input)
 * |                  |      |   (Endpoint 0x81)|
 * |                  |      |                  |
 * | Serial Driver<---+------+-> CDC ACM        | (commands/responses)
 * |                  |      |   (Endpoint 0x03)|
 * |                  |      |                  |
 * +------------------+      +------------------+
 *                           |                  |
 *                           | Flash Memory     | (payload storage)
 *                           | (user_data)      |
 *                           +------------------+
 * ```
 *
 * ## Execution Flow
 *
 * 1. System initialization (clock, GPIO, USB)
 * 2. Check if payload exists in flash (not REPORT_ID_END)
 * 3. If payload exists, start execution (paused=false)
 * 4. Main loop: Poll USB stack
 * 5. SysTick interrupt (1ms): Execute next HID report
 *
 * ## Memory Map
 *
 * | Region      | Address          | Size   | Purpose            |
 * |-------------|------------------|--------|--------------------|
 * | Flash Code  | 0x08000000       | 8 KB   | Firmware           |
 * | Flash Data  | 0x08002000       | 120 KB | user_data (payload)|
 * | SRAM        | 0x20000000       | 20 KB  | Variables, stack   |
 *
 * @note Built using libopencm3 hardware abstraction library
 *
 * @see hid.h for HID report structures
 * @see cdcacm.h for serial interface
 * @see flash.h for persistent storage
 *
 * @copyright (C) 2010 Gareth McMullin <gareth@blacksphere.co.nz>
 * @license LGPL-3.0-or-later
 */

/*
 * This file is part of the libopencm3 project.
 *
 * Copyright (C) 2010 Gareth McMullin <gareth@blacksphere.co.nz>
 *
 * This library is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <string.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/cm3/systick.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/usb/hid.h>
#include <libopencm3/usb/cdc.h>

#include "cdcacm.h"
#include "hid.h"
#include "hex_utils.h"
#include "version.h"
#include "flash.h"

/*============================================================================
 * Global Variables
 *===========================================================================*/

/**
 * @brief USB device instance pointer
 *
 * Handle to the USB device state, returned by usbd_init() and used
 * for all subsequent USB operations. This is the central state object
 * for the libopencm3 USB stack.
 */
static usbd_device *usbd_dev;

/*============================================================================
 * USB Descriptors
 *===========================================================================*/

/**
 * @brief USB Device Descriptor
 *
 * Identifies the device to the USB host. This descriptor is read first
 * during enumeration and tells the host about device capabilities.
 *
 * Configuration:
 * - **USB Version**: 2.0
 * - **Class/SubClass/Protocol**: 0 (defined at interface level)
 * - **Vendor ID**: 0x05AC (Apple Inc.) - for keyboard compatibility
 * - **Product ID**: 0x2227 (Aluminum Keyboard)
 * - **Device Version**: 2.00
 *
 * @note Using Apple VID/PID provides better keyboard compatibility
 *       on some systems. For production, obtain proper VID/PID.
 */
const struct usb_device_descriptor dev_descr = {
	.bLength = USB_DT_DEVICE_SIZE,
	.bDescriptorType = USB_DT_DEVICE,
	.bcdUSB = 0x0200,          /* USB 2.0 */
	.bDeviceClass = 0,         /* Class defined at interface level */
	.bDeviceSubClass = 0,
	.bDeviceProtocol = 0,
	.bMaxPacketSize0 = 64,     /* Max packet size for EP0 */
	.idVendor = 0x05ac,        /* Apple Inc. */
	.idProduct = 0x2227,       /* Aluminum Keyboard (ISO) */
	.bcdDevice = 0x0200,       /* Device version 2.0 */
	.iManufacturer = 1,        /* String index: "satoshinm" */
	.iProduct = 2,             /* String index: "Pill Duck" */
	.iSerialNumber = 3,        /* String index: "ABC" */
	.bNumConfigurations = 1,   /* Single configuration */
};

/**
 * @brief USB Interface array
 *
 * Defines all USB interfaces in the composite device:
 * - Interface 0: HID (keyboard/mouse)
 * - Interface 1: CDC ACM Communication (with IAD)
 * - Interface 2: CDC ACM Data
 *
 * The Interface Association Descriptor (uart_assoc) groups interfaces
 * 1 and 2 as a single CDC ACM function.
 */
const struct usb_interface ifaces[] = {{
	/* Interface 0: HID (keyboard/mouse) */
	.num_altsetting = 1,
	.altsetting = &hid_iface,
}, {
	/* Interface 1: CDC Communication (with IAD for composite device) */
	.num_altsetting = 1,
	.iface_assoc = &uart_assoc,
	.altsetting = uart_comm_iface,
}, {
	/* Interface 2: CDC Data */
	.num_altsetting = 1,
	.altsetting = uart_data_iface,
}};

/**
 * @brief USB Configuration Descriptor
 *
 * Describes the device's single configuration, including all interfaces
 * and endpoints. The total length is calculated automatically by the
 * USB stack based on the interface descriptors.
 *
 * Configuration:
 * - **Self-powered**: Yes (bmAttributes bit 6)
 * - **Remote wakeup**: No
 * - **Max Power**: 100mA (0x32 * 2)
 */
const struct usb_config_descriptor config = {
	.bLength = USB_DT_CONFIGURATION_SIZE,
	.bDescriptorType = USB_DT_CONFIGURATION,
	.wTotalLength = 0,                              /* Calculated by stack */
	.bNumInterfaces = sizeof(ifaces)/sizeof(ifaces[0]), /* 3 interfaces */
	.bConfigurationValue = 1,                       /* Configuration 1 */
	.iConfiguration = 0,                            /* No string descriptor */
	.bmAttributes = 0xC0,                           /* Self-powered */
	.bMaxPower = 0x32,                              /* 100mA */

	.interface = ifaces,
};

/**
 * @brief USB String Descriptors
 *
 * Human-readable strings for device identification:
 * - Index 1: Manufacturer name
 * - Index 2: Product name
 * - Index 3: Serial number
 * - Index 4: CDC interface name
 *
 * These strings appear in device manager and system information tools.
 */
static const char *usb_strings[] = {
	"satoshinm",           /* iManufacturer (index 1) */
	"Pill Duck",           /* iProduct (index 2) */
	"ABC",                 /* iSerialNumber (index 3) */
	"Pill Duck UART Port", /* CDC interface (index 4) */
};

/*============================================================================
 * Execution State Variables
 *===========================================================================*/

/**
 * @brief Current position in the HID report sequence
 *
 * Index into the user_data array, indicating which report will be
 * sent next by sys_tick_handler(). Incremented after each report
 * is transmitted, reset to 0 when REPORT_ID_END is encountered.
 *
 * This can be read via the '@' serial command and reset via 'z'.
 */
static uint32_t report_index = 0;

/*============================================================================
 * Flash Storage
 *===========================================================================*/

/**
 * @brief Persistent storage for HID report payload in flash memory
 *
 * This array is placed in a dedicated flash section (.user_data) by
 * the linker script. It stores the sequence of HID reports that are
 * executed automatically on device startup.
 *
 * Memory characteristics:
 * - Located at 0x08002000 (after 8KB firmware area)
 * - Size: ~120KB (calculated from linker script)
 * - Persists across power cycles
 * - Modified via 'w' or 'd' serial commands
 *
 * @note The section attribute ensures this is placed in flash, not RAM.
 *       Reading is direct (memory-mapped), writing requires flash_program_data().
 *
 * @see flash_program_data() for writing
 * @see bluepill.ld for memory layout
 */
__attribute__((__section__(".user_data"))) const struct composite_report
	user_data[sizeof(struct composite_report) / (128 - 8) * 1024];


/**
 * @brief Temporary RAM buffer for report conversion
 *
 * Used to build HID reports in RAM before writing to flash.
 * Sized to fit one flash page (1KB) worth of reports.
 *
 * Used by:
 * - convert_ducky_binary(): Converting DuckyScript to reports
 * - add_mouse_jiggler(): Generating mouse movement pattern
 *
 * @note Must be in RAM since we can't write directly to flash.
 */
static struct composite_report packet_buffer[1024 / sizeof(struct composite_report)] = {0};

/*============================================================================
 * DuckyScript Conversion
 *===========================================================================*/

/**
 * @brief Convert compiled DuckyScript binary to USB HID reports
 *
 * Parses the binary format produced by the DuckyScript encoder and
 * generates corresponding USB HID keyboard reports. Each DuckyScript
 * instruction becomes one or two HID reports.
 *
 * ## DuckyScript Binary Format
 *
 * The compiled format consists of 16-bit little-endian words:
 *
 * | Byte 0 (Low)    | Byte 1 (High) | Meaning                    |
 * |-----------------|---------------|----------------------------|
 * | 0x00            | delay_ms      | Delay for delay_ms ticks   |
 * | keycode         | modifiers     | Press key with modifiers   |
 *
 * ## Output Format
 *
 * For each keypress instruction, two reports are generated:
 * 1. Key press report (key down + modifiers)
 * 2. Key release report (all keys up)
 *
 * This ensures proper key event generation for the host OS.
 *
 * ## Modifier Byte Format
 *
 * | Bit | Modifier       |
 * |-----|----------------|
 * | 0   | Left Control   |
 * | 1   | Left Shift     |
 * | 2   | Left Alt       |
 * | 3   | Left GUI       |
 * | 4   | Right Control  |
 * | 5   | Right Shift    |
 * | 6   | Right Alt      |
 * | 7   | Right GUI      |
 *
 * @param buf Input buffer containing compiled DuckyScript binary
 * @param len Length of input buffer in bytes
 * @param out Output buffer for composite_report structures
 *            Must have space for approximately len reports
 *
 * @return Number of composite_report records written to out
 *
 * @note Input length is rounded down to even (16-bit boundary)
 * @note Output always ends with REPORT_ID_END marker
 *
 * @see https://github.com/hak5darren/USB-Rubber-Ducky for DuckyScript
 */
int convert_ducky_binary(uint8_t *buf, int len, struct composite_report *out)
{
	int j = 0;

	/* DuckyScript uses 16-bit words, ensure even length */
	if ((len % 2) != 0) len -= 1;

	for (int i = 0; i < len; i += 2) {
		/* Read 16-bit word (little-endian) */
		uint16_t word = buf[i] | (buf[i + 1] << 8);

		if ((word & 0xff) == 0) {
			/* Special case: delay command (low byte = 0) */
			/* High byte contains delay duration in ms */
			out[j].report_id = REPORT_ID_DELAY;
			out[j].padding[0] = word >> 8;
			++j;
			continue;
		}

		/* Key press report: press key with modifiers */
		out[j].report_id = REPORT_ID_KEYBOARD;
		out[j].keyboard.modifiers = word >> 8;    /* High byte: modifiers */
		out[j].keyboard.reserved = 1;             /* Mark as converted */
		out[j].keyboard.keys_down[0] = word & 0xff; /* Low byte: keycode */
		out[j].keyboard.keys_down[1] = 0;
		out[j].keyboard.keys_down[2] = 0;
		out[j].keyboard.keys_down[3] = 0;
		out[j].keyboard.keys_down[4] = 0;
		out[j].keyboard.keys_down[5] = 0;
		out[j].keyboard.leds = 0;
		++j;

		/* Key release report: all keys up */
		out[j].report_id = REPORT_ID_KEYBOARD;
		out[j].keyboard.modifiers = 0;
		out[j].keyboard.reserved = 1;
		out[j].keyboard.keys_down[0] = 0;
		out[j].keyboard.keys_down[1] = 0;
		out[j].keyboard.keys_down[2] = 0;
		out[j].keyboard.keys_down[3] = 0;
		out[j].keyboard.keys_down[4] = 0;
		out[j].keyboard.keys_down[5] = 0;
		out[j].keyboard.leds = 0;
		++j;
	}

	/* Add end marker */
	out[j].report_id = REPORT_ID_END;
	++j;

	return j;
}

/*============================================================================
 * Execution Control Variables
 *===========================================================================*/

/**
 * @brief Execution paused flag
 *
 * When true, the sys_tick_handler() will not advance through reports.
 * Toggled via the 'p' serial command.
 *
 * Default: true (paused) unless user_data contains a payload
 */
static bool paused = true;

/**
 * @brief Single-step mode flag
 *
 * When true, execute exactly one report then pause.
 * Set via the 's' serial command, cleared after execution.
 */
static bool single_step = false;

/**
 * @brief Currently processing a delay
 *
 * When true, we're counting down delay_ticks_remaining.
 * No HID reports are sent during delays.
 */
static bool delaying = false;

/**
 * @brief Remaining SysTick counts for current delay
 *
 * Decremented each SysTick interrupt. When reaches 0,
 * delay is complete and execution continues.
 */
static int delay_ticks_remaining = 0;

/*============================================================================
 * Interrupt Handlers
 *===========================================================================*/

/**
 * @brief SysTick interrupt handler - HID report execution engine
 *
 * Called every 1ms by the SysTick timer. This is the core execution
 * engine that sends HID reports from flash memory to the USB host.
 *
 * ## State Machine
 *
 * ```
 * +--------+     start      +----------+    delay      +---------+
 * | PAUSED |--------------->| RUNNING  |-------------->| DELAY   |
 * +--------+                +----------+               +---------+
 *     ^                          |                          |
 *     |       'p' or 's'         v                          |
 *     +<--------------------send report                     |
 *     |                          |                          |
 *     |                    REPORT_ID_END                    |
 *     +<-----------------------reset                        |
 *                                ^                          |
 *                                |      count==0            |
 *                                +--------------------------+
 * ```
 *
 * ## Report Processing
 *
 * | Report ID          | Action                              |
 * |--------------------|-------------------------------------|
 * | REPORT_ID_NOP (0)  | Skip, don't advance index           |
 * | REPORT_ID_DELAY    | Start/continue delay countdown      |
 * | REPORT_ID_KEYBOARD | Send 9-byte keyboard report         |
 * | REPORT_ID_MOUSE    | Send 5-byte mouse report            |
 * | REPORT_ID_END      | Reset index to 0 (loop or stop)     |
 *
 * ## Timing
 *
 * - SysTick fires every 1ms (configured in setup_clock)
 * - Each keystroke takes 2 ticks (press + release reports)
 * - Delays are in milliseconds (1 tick = 1ms)
 *
 * @note This function runs in interrupt context. Keep it fast!
 * @note The LED (PC13) toggles on each HID report sent.
 *
 * @see setup_clock() for SysTick configuration
 * @see user_data for the report source
 */
void sys_tick_handler(void)
{
	/* Check if execution is paused (and not single-stepping) */
	if (paused && !single_step) return;

	/* Read current report from flash */
	struct composite_report report = user_data[report_index];
	uint16_t len = 0;
	uint8_t id = report.report_id;

	/* Process based on report type */
	if (id == REPORT_ID_NOP) {
		/* No operation - skip without advancing index */
		return;
	} else if (id == REPORT_ID_DELAY) {
		/* Handle delay command */
		if (!delaying) {
			/* Start new delay: load tick count from report */
			delay_ticks_remaining = report.padding[0];
			delaying = true;
			return;
		} else {
			/* Continue existing delay: decrement counter */
			--delay_ticks_remaining;
			if (delay_ticks_remaining <= 0) {
				/* Delay complete: advance to next report */
				delaying = false;
				++report_index;
			}
		}
		return;
	} else if (id == REPORT_ID_KEYBOARD) {
		/* Keyboard report: 1 byte ID + 8 bytes data */
		len = 9;
	} else if (id == REPORT_ID_MOUSE) {
		/* Mouse report: 1 byte ID + 4 bytes data */
		len = 5;
	} else {
		/* Unknown ID (including REPORT_ID_END): reset to start */
		report_index = 0;
		return;
	}

	/* Send HID report to host via endpoint 0x81 */
	/* Retry until endpoint buffer accepts the data */
	uint16_t bytes_written = 0;
	do {
		bytes_written = usbd_ep_write_packet(usbd_dev, 0x81, &report, len);
	} while (bytes_written == 0);

	/* Toggle LED to indicate activity */
	gpio_toggle(GPIOC, GPIO13);

	/* Handle single-step mode: pause after one report */
	if (single_step) {
		single_step = false;
		paused = true;
	}

	/* Advance to next report */
	++report_index;
}

/*============================================================================
 * USB Callbacks
 *===========================================================================*/

/**
 * @brief USB set configuration callback
 *
 * Called by the USB stack when the host sends SET_CONFIGURATION.
 * Initializes both the HID and CDC ACM interfaces.
 *
 * @param dev    USB device instance
 * @param wValue Configuration number (always 1 for this device)
 *
 * @see hid_set_config() for HID interface setup
 * @see cdcacm_set_config() for serial interface setup
 */
static void usb_set_config(usbd_device *dev, uint16_t wValue)
{
	hid_set_config(dev, wValue);
	cdcacm_set_config(dev, wValue);
}

/*============================================================================
 * Payload Generators
 *===========================================================================*/

/**
 * @brief Generate mouse jiggler pattern
 *
 * Creates a sequence of mouse movement reports that move the cursor
 * back and forth horizontally. This prevents screen savers and
 * auto-lock from activating while maintaining cursor position.
 *
 * Pattern generated:
 * 1. Move right by 1 pixel, `width` times
 * 2. Move left by 1 pixel, `width` times
 * 3. End marker
 *
 * Net movement is zero, so cursor returns to original position.
 *
 * @param width Number of 1-pixel movements in each direction
 *              Total reports generated: (width * 2) + 1
 *
 * @return Number of composite_report records written to packet_buffer
 *
 * @note Output is written to the global packet_buffer array
 * @note The 'j' serial command uses width=30
 *
 * @code
 * // Generate jiggler and write to flash
 * int records = add_mouse_jiggler(30);
 * flash_program_data(&user_data, packet_buffer, records * sizeof(*packet_buffer));
 * @endcode
 */
int add_mouse_jiggler(int width)
{
	int j = 0;

	/* Generate rightward movements */
	for (int i = 0; i < width; ++i) {
		packet_buffer[j].report_id = REPORT_ID_MOUSE;
		packet_buffer[j].mouse.buttons = 0;   /* No buttons pressed */
		packet_buffer[j].mouse.x = 1;         /* Move right 1 pixel */
		packet_buffer[j].mouse.y = 0;
		packet_buffer[j].mouse.wheel = 0;
		++j;
	}

	/* Generate leftward movements (return to start) */
	for (int i = 0; i < width; ++i) {
		packet_buffer[j].report_id = REPORT_ID_MOUSE;
		packet_buffer[j].mouse.buttons = 0;
		packet_buffer[j].mouse.x = -1;        /* Move left 1 pixel */
		packet_buffer[j].mouse.y = 0;
		packet_buffer[j].mouse.wheel = 0;
		++j;
	}

	/* Add end marker */
	packet_buffer[j].report_id = REPORT_ID_END;
	++j;

	return j;
}

/*============================================================================
 * Serial Command Processing
 *===========================================================================*/

/**
 * @brief Process commands received over the serial interface
 *
 * Parses and executes single-character commands from the USB serial
 * console. Commands control payload storage and script execution.
 *
 * ## Command Reference
 *
 * | Cmd | Arguments    | Description                              |
 * |-----|--------------|------------------------------------------|
 * | v   | (none)       | Show firmware version                    |
 * | ?   | (none)       | Show help reference                      |
 * | w   | <hex_data>   | Write raw hex data directly to flash     |
 * | d   | <hex_data>   | Convert DuckyScript binary and store     |
 * | j   | (none)       | Generate and store mouse jiggler pattern |
 * | r   | (none)       | Read first 16 bytes of flash (hex)       |
 * | @   | (none)       | Show current report execution index      |
 * | p   | (none)       | Toggle pause/resume execution            |
 * | s   | (none)       | Single-step one report                   |
 * | z   | (none)       | Reset report index to beginning          |
 *
 * ## Examples
 *
 * ```
 * duck> v
 * Pill Duck version 1.0
 * duck> d0700020700
 * wrote flash
 * duck> p
 * resumed
 * ```
 *
 * @param buf Command string buffer (first char is command)
 * @param len Length of command string (used for hex data length calculation)
 *
 * @return Response string to display to user
 *
 * @warning The 'w' and 'd' commands erase the flash page before writing!
 *
 * @see convert_ducky_binary() for 'd' command processing
 * @see add_mouse_jiggler() for 'j' command
 * @see usbuart_usb_out_cb() in cdcacm.c which calls this function
 */
char *process_serial_command(char *buf, int len) {
	(void) len;

	if (buf[0] == 'v') {
		/* Version command: return firmware version string */
		return "Pill Duck version " FIRMWARE_VERSION;

	} else if (buf[0] == '?') {
		/* Help command: reference to documentation */
		return "see source code for help";
		/*
		 * TODO: Full help text is too large for one USB packet.
		 * Would need chunked transmission support:
		 *
		 * return "help:\r\n"
		 *     "?\tshow this help\r\n"
		 *     "v\tshow firmware version\r\n"
		 *     "w<hex>\twrite flash data\r\n"
		 *     "d<hex>\twrite compiled DuckyScript flash data\r\n"
		 *     "j\twrite mouse jiggler to flash data\r\n"
		 *     "r\tread flash data\r\n"
		 *     "@\tshow current report index\r\n"
		 *     "p\tpause/resume execution\r\n"
		 *     "s\tsingle step execution\r\n"
		 *     "z\treset report index to zero\r\n";
		 */

	} else if (buf[0] == 'w' || buf[0] == 'd') {
		/*
		 * Write commands:
		 * 'w' - Write raw hex data directly to flash
		 * 'd' - Convert DuckyScript binary format, then write
		 */
		char binary[1024] = {0};
		int binary_len = len / 2;  /* 2 hex chars per byte */
		uint8_t *to_write = (uint8_t *)&binary;

		/* Decode hex string to binary */
		unhexify(binary, &buf[1], len);

		if (buf[0] == 'd') {
			/* DuckyScript mode: convert to HID reports first */
			int records = convert_ducky_binary((uint8_t *)binary, binary_len, packet_buffer);
			binary_len = records * sizeof(struct composite_report);
			to_write = (uint8_t *)&packet_buffer;
		}

		/* Write to flash and return status */
		int result = flash_program_data((uint32_t)&user_data, to_write, binary_len);
		if (result == RESULT_OK) {
			return "wrote flash";
		} else if (result == FLASH_WRONG_DATA_WRITTEN) {
			return "wrong data written";
		} else {
			return "error writing flash";
		}

	} else if (buf[0] == 'j') {
		/* Jiggler command: generate and store mouse jiggler pattern */
		int records = add_mouse_jiggler(30);  /* 30 pixels each direction */
		int binary_len = records * sizeof(struct composite_report);

		int result = flash_program_data((uint32_t)&user_data, (uint8_t *)&packet_buffer, binary_len);
		if (result == RESULT_OK) {
			return "wrote flash";
		} else if (result == FLASH_WRONG_DATA_WRITTEN) {
			return "wrong data written";
		} else {
			return "error writing flash";
		}

	} else if (buf[0] == 'r') {
		/* Read command: return first 16 bytes of flash as hex */
		char binary[16] = {0};
		memset(binary, 0, sizeof(binary));
		flash_read_data((uint32_t)&user_data, sizeof(binary), (uint8_t *)&binary);

		static char hex[32] = {0};  /* Static: must outlive function */
		hexify(hex, (const char *)binary, sizeof(binary));
		return hex;

	} else if (buf[0] == '@') {
		/* Index command: show current execution position */
		static char hex[16] = {0};
		/* TODO: Display in decimal with proper endianness */
		hexify(hex, (const char *)&report_index, sizeof(report_index));
		return hex;

	} else if (buf[0] == 'p') {
		/* Pause command: toggle pause state */
		paused = !paused;
		if (paused) return "paused";
		else return "resumed";

	} else if (buf[0] == 's') {
		/* Step command: execute single report */
		single_step = true;
		return "step";

	} else if (buf[0] == 'z') {
		/* Zero command: reset execution index */
		report_index = 0;

	} else {
		/* Unknown command */
		return "invalid command, try ? for help";
	}

	return "";
}

/*============================================================================
 * System Initialization Functions
 *===========================================================================*/

/**
 * @brief Configure system clock and SysTick timer
 *
 * Sets up the STM32F103 clock system and configures the SysTick timer
 * for 1ms periodic interrupts.
 *
 * ## Clock Configuration
 *
 * Uses the internal 8MHz HSI oscillator with PLL to generate 48MHz
 * system clock (required for USB operation).
 *
 * ## SysTick Timer
 *
 * The SysTick timer drives the HID report execution engine. Configuration:
 * - Clock source: AHB/8 = 48MHz / 8 = 6MHz
 * - Reload value: 5999 = 6000 cycles = 1ms period
 *
 * Alternative reload values (commented out):
 * - 899999: 100ms period
 * - 89999: 10ms period
 * - 8999: 1ms period (current)
 *
 * @note The SysTick interrupt (sys_tick_handler) is the heart of the
 *       execution engine, sending one HID report per tick.
 *
 * @see sys_tick_handler() for the interrupt routine
 */
static void setup_clock(void) {
	/* Configure 48MHz from internal 8MHz HSI */
	rcc_clock_setup_in_hsi_out_48mhz();

	/* Enable GPIO port C clock (for LED) */
	rcc_periph_clock_enable(RCC_GPIOC);

	/*
	 * Configure SysTick timer
	 * Clock source: AHB/8 = 48MHz / 8 = 6MHz
	 * Reload value formula: Period_ms = (reload + 1) / 6000
	 */
	systick_set_clocksource(STK_CSR_CLKSOURCE_AHB_DIV8);

	/* SysTick interrupt every N clock pulses: set reload to N-1
	 * Period: N / (48 MHz / 8) = N / 6MHz
	 */
	/* systick_set_reload(899999); */  /* 100 ms period */
	/* systick_set_reload(89999); */   /* 10 ms period */
	systick_set_reload(8999);          /* 1 ms period (current) */

	/* Enable SysTick interrupt and counter */
	systick_interrupt_enable();
	systick_counter_enable();
}

/**
 * @brief Configure GPIO pins
 *
 * Sets up the LED pin (PC13) on the Blue Pill board as an output.
 * The LED is used for visual feedback during HID report transmission.
 *
 * ## Blue Pill LED
 *
 * - Pin: PC13
 * - Active: LOW (LED on when pin is low)
 * - Initial state: HIGH (LED off)
 *
 * The LED is toggled by sys_tick_handler() each time a HID report
 * is successfully transmitted.
 */
static void setup_gpio(void) {
	/* Configure PC13 (built-in LED on Blue Pill) as push-pull output */
	gpio_set_mode(GPIOC, GPIO_MODE_OUTPUT_2_MHZ,
		GPIO_CNF_OUTPUT_PUSHPULL, GPIO13);

	/* Set initial state: HIGH (LED off, since it's active-low) */
	gpio_set(GPIOC, GPIO13);
}

/*============================================================================
 * USB Configuration
 *===========================================================================*/

/**
 * @brief USB control transfer buffer
 *
 * Buffer used by the USB stack for control transfer data.
 * Must be large enough for the largest control transfer payload
 * (typically 64-128 bytes for descriptors).
 */
uint8_t usbd_control_buffer[128];

/*============================================================================
 * Main Entry Point
 *===========================================================================*/

/**
 * @brief Firmware entry point
 *
 * Initializes the system and enters the main USB polling loop.
 *
 * ## Initialization Sequence
 *
 * 1. Configure system clock (48MHz for USB)
 * 2. Configure GPIO (LED on PC13)
 * 3. Check for stored payload in flash
 * 4. Initialize USB stack
 * 5. Register USB configuration callback
 * 6. Enter infinite USB polling loop
 *
 * ## Auto-Start Behavior
 *
 * If user_data contains a valid payload (first report is not
 * REPORT_ID_END), execution starts automatically (paused=false).
 * Otherwise, the device waits for commands via serial.
 *
 * ## Development Notes
 *
 * Example payloads (commented out) can be enabled for testing:
 * - add_mouse_jiggler(30): Mouse jiggler pattern
 * - "Ddde": Simple 4-character test
 * - "Hello, world!": Full DuckyScript test
 *
 * @return Never returns (infinite loop)
 */
int main(void)
{
	/* Initialize system peripherals */
	setup_clock();
	setup_gpio();

	/*
	 * Development test payloads (commented out):
	 *
	 * add_mouse_jiggler(30);        // Mouse movement test
	 * add_keyboard_spammer(6);      // Key 'c' repeater
	 *
	 * // "Ddde" test (Shift+D, d, d, e):
	 * add_ducky_binary((uint8_t *)"\x07\x02\x07\x00\x07\x00\x08\x00", 8);
	 *
	 * // "Hello, world!" with delays:
	 * convert_ducky_binary((uint8_t *)
	 *     "\x00\xff\x00\xff\x00\xff\x00\xeb\x0b\x02\x08\x00\x0f\x00\x0f\x00"
	 *     "\x12\x00\x36\x00\x2c\x00\x1a\x00\x12\x00\x15\x00\x0f\x00\x07\x00"
	 *     "\x1e\x02\x00\xff\x00\xf5\x28\x00", 36);
	 */

	/* Check if payload exists in flash; if so, start execution */
	if (user_data[0].report_id != REPORT_ID_END) {
		paused = false;  /* Auto-start execution */
	}

	/* Initialize USB stack as composite HID + CDC ACM device */
	usbd_dev = usbd_init(&st_usbfs_v1_usb_driver,   /* STM32F103 USB driver */
		&dev_descr,                                  /* Device descriptor */
		&config,                                     /* Configuration descriptor */
		usb_strings,                                 /* String descriptors */
		sizeof(usb_strings)/sizeof(char *),         /* Number of strings */
		usbd_control_buffer,                         /* Control transfer buffer */
		sizeof(usbd_control_buffer));               /* Buffer size */

	/* Register callback for SET_CONFIGURATION */
	usbd_register_set_config_callback(usbd_dev, usb_set_config);

	/*
	 * Main loop: poll USB stack forever
	 *
	 * The USB stack handles:
	 * - Enumeration and descriptor requests
	 * - HID report transmission (via sys_tick_handler)
	 * - Serial command reception (via cdcacm callbacks)
	 *
	 * This is a busy-wait loop; all real work happens in
	 * interrupts and USB callbacks.
	 */
	while (1)
		usbd_poll(usbd_dev);
}

