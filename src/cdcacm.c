// vim: tabstop=8 softtabstop=8 shiftwidth=8 noexpandtab
/**
 * @file cdcacm.c
 * @brief USB CDC ACM (Communications Device Class - Abstract Control Model) implementation
 *
 * This module implements a virtual serial port over USB using the CDC ACM
 * class specification. It provides the command-line interface for controlling
 * the Pill Duck device, including:
 *
 * - Receiving and processing serial commands
 * - Writing payload data to flash memory
 * - Reading stored data
 * - Controlling script execution
 *
 * ## Serial Protocol
 *
 * The device presents a simple command-line interface with the prompt "duck> ".
 * Commands are single characters followed by optional hex-encoded data:
 *
 * | Command | Description                                      |
 * |---------|--------------------------------------------------|
 * | v       | Display firmware version                         |
 * | ?       | Show help                                        |
 * | w<hex>  | Write raw hex data to flash                      |
 * | d<hex>  | Write compiled DuckyScript to flash              |
 * | j       | Write mouse jiggler pattern to flash             |
 * | r       | Read first 16 bytes from flash (hex-encoded)     |
 * | @       | Show current report execution index              |
 * | p       | Pause/resume script execution                    |
 * | s       | Single-step execution                            |
 * | z       | Reset report index to zero                       |
 *
 * ## USB Architecture
 *
 * CDC ACM requires two USB interfaces:
 * 1. **Communication Interface** (Interface 1): Handles CDC control requests
 * 2. **Data Interface** (Interface 2): Carries actual serial data
 *
 * An Interface Association Descriptor (IAD) groups these interfaces.
 *
 * ## Endpoints
 *
 * | Endpoint | Direction | Type      | Purpose                    |
 * |----------|-----------|-----------|----------------------------|
 * | 0x03     | OUT       | Bulk      | Receive data from host     |
 * | 0x83     | IN        | Bulk      | Send data to host          |
 * | 0x84     | IN        | Interrupt | Send notifications to host |
 *
 * @note Uses 128-byte packet size for efficient data transfer.
 *
 * @see cdcacm.h for interface declarations
 * @see process_serial_command() in main.c for command handling
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
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/cm3/systick.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/usb/cdc.h>
#include <string.h>

#include "cdcacm.h"
#include "version.h"

/*============================================================================
 * Constants and Macros
 *===========================================================================*/

/**
 * @brief Maximum USB packet size for CDC data transfers
 *
 * This is the maximum amount of data that can be sent or received
 * in a single USB transaction on the bulk endpoints. Larger transfers
 * are automatically split into multiple packets.
 */
#define CDCACM_PACKET_SIZE 	128

/**
 * @brief Endpoint address for serial data transfer
 *
 * This is a bulk endpoint used for both directions:
 * - 0x03: OUT (host to device)
 * - 0x83: IN (device to host, with direction bit set)
 */
#define CDCACM_UART_ENDPOINT	0x03

/**
 * @brief Endpoint address for CDC notifications
 *
 * Interrupt IN endpoint for sending serial state notifications
 * (like DCD/DSR changes) to the host.
 */
#define CDCACM_INTR_ENDPOINT	0x84

/*============================================================================
 * USB Endpoint Descriptors
 *===========================================================================*/

/**
 * @brief Communication interface endpoint descriptor
 *
 * Defines the interrupt IN endpoint (0x84) for CDC notifications.
 * This endpoint is used to send modem status changes (DCD, DSR, etc.)
 * to the host using USB_CDC_NOTIFY_SERIAL_STATE notifications.
 *
 * Configuration:
 * - **Address**: 0x84 (Endpoint 4, IN)
 * - **Type**: Interrupt
 * - **Max Packet**: 16 bytes
 * - **Interval**: 255ms (maximum, low priority)
 */
static const struct usb_endpoint_descriptor uart_comm_endp[] = {{
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = CDCACM_INTR_ENDPOINT, /* 0x84 - Interrupt IN */
	.bmAttributes = USB_ENDPOINT_ATTR_INTERRUPT,
	.wMaxPacketSize = 16,                     /* Notification packet size */
	.bInterval = 255,                         /* Maximum polling interval */
}};

/**
 * @brief Data interface endpoint descriptors (OUT and IN)
 *
 * Defines the bulk endpoints for bidirectional serial data transfer:
 * - First descriptor: Bulk OUT (0x03) for host-to-device data
 * - Second descriptor: Bulk IN (0x83) for device-to-host data
 *
 * Configuration:
 * - **OUT Address**: 0x03 (Endpoint 3, OUT)
 * - **IN Address**: 0x83 (Endpoint 3, IN)
 * - **Type**: Bulk (reliable, non-time-critical transfer)
 * - **Max Packet**: 128 bytes
 *
 * @note Bulk transfers have no guaranteed bandwidth but provide
 *       reliable data delivery with error checking and retry.
 */
static const struct usb_endpoint_descriptor uart_data_endp[] = {{
	/* Bulk OUT endpoint - receive data from host */
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = CDCACM_UART_ENDPOINT,      /* 0x03 - Bulk OUT */
	.bmAttributes = USB_ENDPOINT_ATTR_BULK,
	.wMaxPacketSize = CDCACM_PACKET_SIZE,          /* 128 bytes */
	.bInterval = 1,                                /* Ignored for bulk */
}, {
	/* Bulk IN endpoint - send data to host */
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = 0x80 | CDCACM_UART_ENDPOINT, /* 0x83 - Bulk IN */
	.bmAttributes = USB_ENDPOINT_ATTR_BULK,
	.wMaxPacketSize = CDCACM_PACKET_SIZE,          /* 128 bytes */
	.bInterval = 1,                                /* Ignored for bulk */
}};

/*============================================================================
 * CDC Functional Descriptors
 *===========================================================================*/

/**
 * @brief CDC ACM functional descriptors
 *
 * These class-specific descriptors define the CDC device capabilities
 * and the relationship between the Communication and Data interfaces.
 *
 * Included descriptors:
 * 1. **Header**: CDC specification version (1.10)
 * 2. **Call Management**: Call handling capabilities (none)
 * 3. **ACM**: Abstract Control Model capabilities (SET_LINE_CODING)
 * 4. **Union**: Associates Communication (1) and Data (2) interfaces
 *
 * @note These descriptors are sent as "extra" data with the
 *       Communication interface descriptor during enumeration.
 *
 * @see USB CDC Specification 1.2
 */
static const struct {
	struct usb_cdc_header_descriptor header;          /**< CDC version info */
	struct usb_cdc_call_management_descriptor call_mgmt; /**< Call handling */
	struct usb_cdc_acm_descriptor acm;                /**< ACM capabilities */
	struct usb_cdc_union_descriptor cdc_union;        /**< Interface grouping */
} __attribute__((packed)) uart_cdcacm_functional_descriptors = {
	/* CDC Header Descriptor - required, specifies CDC version */
	.header = {
		.bFunctionLength = sizeof(struct usb_cdc_header_descriptor),
		.bDescriptorType = CS_INTERFACE,           /* Class-specific interface */
		.bDescriptorSubtype = USB_CDC_TYPE_HEADER,
		.bcdCDC = 0x0110,                          /* CDC 1.10 */
	},
	/* Call Management Descriptor - required for ACM */
	.call_mgmt = {
		.bFunctionLength =
			sizeof(struct usb_cdc_call_management_descriptor),
		.bDescriptorType = CS_INTERFACE,
		.bDescriptorSubtype = USB_CDC_TYPE_CALL_MANAGEMENT,
		.bmCapabilities = 0,                       /* No call management */
		.bDataInterface = 2,                       /* Data interface number */
	},
	/* Abstract Control Model Descriptor - defines ACM capabilities */
	.acm = {
		.bFunctionLength = sizeof(struct usb_cdc_acm_descriptor),
		.bDescriptorType = CS_INTERFACE,
		.bDescriptorSubtype = USB_CDC_TYPE_ACM,
		.bmCapabilities = 2,                       /* Supports SET_LINE_CODING */
	},
	/* Union Descriptor - groups Communication and Data interfaces */
	.cdc_union = {
		.bFunctionLength = sizeof(struct usb_cdc_union_descriptor),
		.bDescriptorType = CS_INTERFACE,
		.bDescriptorSubtype = USB_CDC_TYPE_UNION,
		.bControlInterface = 1,                    /* Communication interface */
		.bSubordinateInterface0 = 2,               /* Data interface */
	 }
};

/*============================================================================
 * USB Interface Descriptors
 *===========================================================================*/

/**
 * @brief CDC Communication Class interface descriptor
 *
 * Defines Interface 1, which handles CDC control functions including:
 * - SET_LINE_CODING (baud rate, parity, etc. - ignored by firmware)
 * - SET_CONTROL_LINE_STATE (DTR, RTS signals)
 * - Serial state notifications (DCD, DSR)
 *
 * Configuration:
 * - **Interface Number**: 1
 * - **Class**: CDC (Communications Device Class)
 * - **SubClass**: ACM (Abstract Control Model)
 * - **Protocol**: AT commands (V.250)
 * - **String**: "Pill Duck UART Port" (index 4)
 *
 * @note The functional descriptors (header, ACM, union) are attached
 *       as extra data to this interface.
 */
const struct usb_interface_descriptor uart_comm_iface[] = {{
	.bLength = USB_DT_INTERFACE_SIZE,
	.bDescriptorType = USB_DT_INTERFACE,
	.bInterfaceNumber = 1,                    /* Second interface (after HID) */
	.bAlternateSetting = 0,
	.bNumEndpoints = 1,                       /* One interrupt endpoint */
	.bInterfaceClass = USB_CLASS_CDC,
	.bInterfaceSubClass = USB_CDC_SUBCLASS_ACM,
	.bInterfaceProtocol = USB_CDC_PROTOCOL_AT, /* AT command protocol */
	.iInterface = 4,                          /* String descriptor index */

	.endpoint = uart_comm_endp,

	.extra = &uart_cdcacm_functional_descriptors,
	.extralen = sizeof(uart_cdcacm_functional_descriptors)
}};

/**
 * @brief CDC Data Class interface descriptor
 *
 * Defines Interface 2, which carries the actual serial data.
 * This interface has two bulk endpoints for bidirectional
 * communication.
 *
 * Configuration:
 * - **Interface Number**: 2
 * - **Class**: CDC Data
 * - **Endpoints**: 2 (bulk IN and OUT)
 */
const struct usb_interface_descriptor uart_data_iface[] = {{
	.bLength = USB_DT_INTERFACE_SIZE,
	.bDescriptorType = USB_DT_INTERFACE,
	.bInterfaceNumber = 2,                    /* Third interface */
	.bAlternateSetting = 0,
	.bNumEndpoints = 2,                       /* Bulk IN and OUT */
	.bInterfaceClass = USB_CLASS_DATA,        /* Data class */
	.bInterfaceSubClass = 0,
	.bInterfaceProtocol = 0,
	.iInterface = 0,                          /* No string descriptor */

	.endpoint = uart_data_endp,
}};

/**
 * @brief Interface Association Descriptor for CDC function
 *
 * Groups the Communication (1) and Data (2) interfaces into a single
 * CDC ACM function. This is required for composite USB devices so that
 * the host OS can properly associate related interfaces.
 *
 * Without the IAD, some operating systems may not correctly identify
 * the CDC function as a single serial port.
 *
 * @note IADs are part of USB 2.0 ECN (Engineering Change Notice)
 *       and are required for multi-function devices.
 */
const struct usb_iface_assoc_descriptor uart_assoc = {
	.bLength = USB_DT_INTERFACE_ASSOCIATION_SIZE,
	.bDescriptorType = USB_DT_INTERFACE_ASSOCIATION,
	.bFirstInterface = 1,                     /* Start at interface 1 */
	.bInterfaceCount = 2,                     /* Includes interfaces 1 and 2 */
	.bFunctionClass = USB_CLASS_CDC,
	.bFunctionSubClass = USB_CDC_SUBCLASS_ACM,
	.bFunctionProtocol = USB_CDC_PROTOCOL_AT,
	.iFunction = 0,                           /* No string descriptor */
};

/*============================================================================
 * Internal Functions
 *===========================================================================*/

/**
 * @brief Send modem state notification to the host
 *
 * Sends a USB_CDC_NOTIFY_SERIAL_STATE notification to inform the host
 * about the current state of virtual modem control signals. This is
 * important for proper detection on some operating systems (especially
 * BSD and macOS) which wait for DCD before allowing the serial port
 * to be opened.
 *
 * Notification format (10 bytes):
 * @code
 * Offset  Size  Field           Value
 * ------  ----  -----           -----
 * 0       1     bmRequestType   0xA1 (device-to-host, class, interface)
 * 1       1     bNotification   0x20 (SERIAL_STATE)
 * 2       2     wValue          0
 * 4       2     wIndex          Interface number
 * 6       2     wLength         2
 * 8       2     Data            Modem state bits
 * @endcode
 *
 * Modem state bits:
 * - Bit 0: DCD (Data Carrier Detect)
 * - Bit 1: DSR (Data Set Ready)
 *
 * @param dev  USB device instance
 * @param iface Interface number (used to calculate endpoint address)
 * @param dsr  DSR signal state (true = asserted)
 * @param dcd  DCD signal state (true = asserted)
 *
 * @note This function sends to endpoint 0x82 + iface (e.g., 0x84 for iface 2)
 */
static void cdcacm_set_modem_state(usbd_device *dev, int iface, bool dsr, bool dcd)
{
	char buf[10];
	struct usb_cdc_notification *notif = (void*)buf;

	/* Build SERIAL_STATE notification packet */
	notif->bmRequestType = 0xA1;                     /* Class, interface, device-to-host */
	notif->bNotification = USB_CDC_NOTIFY_SERIAL_STATE;
	notif->wValue = 0;
	notif->wIndex = iface;
	notif->wLength = 2;                              /* 2 bytes of data follow */
	buf[8] = (dsr ? 2 : 0) | (dcd ? 1 : 0);          /* Modem state bits */
	buf[9] = 0;

	/* Send notification on interrupt endpoint */
	usbd_ep_write_packet(dev, 0x82 + iface, buf, 10);
}

/**
 * @brief Handle CDC ACM class-specific control requests
 *
 * This callback handles CDC class requests from the host, including:
 *
 * 1. **SET_CONTROL_LINE_STATE** (0x22): Host is asserting DTR/RTS
 *    - Responds by asserting DCD/DSR back to host
 *    - Required for proper serial port detection
 *
 * 2. **SET_LINE_CODING** (0x20): Host is setting baud rate, parity, etc.
 *    - Accepted but ignored (virtual serial port has no physical UART)
 *    - Host sends a 7-byte usb_cdc_line_coding structure
 *
 * @param dev      USB device instance
 * @param req      USB setup packet containing:
 *                 - bRequest: CDC request code
 *                 - wIndex: Interface number
 *                 - wValue: Request-specific value
 *                 - wLength: Data stage length
 * @param buf      [out] Response data buffer (unused for these requests)
 * @param len      [in/out] Length of data (used to validate SET_LINE_CODING)
 * @param complete Completion callback (unused)
 *
 * @return 1 if request was handled, 0 if not (passed to next handler)
 *
 * @note Line coding parameters (baud, parity, stop bits) are ignored
 *       since this is a virtual serial port with no physical UART.
 */
static int cdcacm_control_request(usbd_device *dev,
		struct usb_setup_data *req, uint8_t **buf, uint16_t *len,
		void (**complete)(usbd_device *dev, struct usb_setup_data *req))
{
	(void)dev;
	(void)complete;
	(void)buf;
	(void)len;

	switch(req->bRequest) {
	case USB_CDC_REQ_SET_CONTROL_LINE_STATE:
		/* Host is setting DTR/RTS - respond with DCD/DSR asserted */
		cdcacm_set_modem_state(dev, req->wIndex, true, true);
		return 1;

	case USB_CDC_REQ_SET_LINE_CODING:
		/* Host is setting baud rate, parity, etc. - validate length only */
		if(*len < sizeof(struct usb_cdc_line_coding))
			return 0;
		/* Accept but ignore - no physical UART to configure */
		return 1;
	}
	return 0;
}

/**
 * @brief Send data to host in chunks, blocking until complete
 *
 * Sends a buffer to the host by breaking it into USB packet-sized
 * chunks. This function blocks until all data has been transmitted,
 * retrying each chunk until the endpoint buffer accepts it.
 *
 * This is necessary because:
 * 1. USB endpoints have limited buffer sizes (CDCACM_PACKET_SIZE)
 * 2. usbd_ep_write_packet() may return 0 if the buffer is full
 *
 * @param buf              Pointer to data buffer to send
 * @param len              Total number of bytes to send
 * @param dev              USB device instance
 * @param endpoint         Endpoint address to write to (e.g., 0x83)
 * @param max_packet_length Maximum bytes per USB packet (CDCACM_PACKET_SIZE)
 *
 * @note This function is blocking and will spin-wait if the endpoint
 *       buffer is full. Use with caution in interrupt contexts.
 *
 * @warning Large transfers may block for extended periods if the host
 *          is not reading data from the endpoint.
 */
static void send_chunked_blocking(char *buf, int len, usbd_device *dev, int endpoint, int max_packet_length) {
	uint16_t bytes_written = 0;
	uint16_t total_bytes_written = 0;
	uint16_t bytes_remaining = len;

	do {
		/* Determine chunk size (limited by max packet) */
		uint16_t this_length = bytes_remaining;
		if (this_length > max_packet_length) this_length = max_packet_length;

		/* Try to send chunk, may return 0 if buffer full */
		bytes_written = usbd_ep_write_packet(dev, endpoint, buf + total_bytes_written, this_length);
		bytes_remaining -= bytes_written;
		total_bytes_written += bytes_written;
	} while (bytes_remaining > 0);
}

/**
 * @brief External reference to command processor in main.c
 *
 * @param buf Null-terminated command string
 * @param len Length of command string
 * @return Response string to send back to host
 *
 * @see process_serial_command() in main.c
 */
extern char *process_serial_command(char *buf, int len);

/**
 * @brief USB callback for received serial data (OUT endpoint)
 *
 * Called by the USB stack when data is received on the bulk OUT
 * endpoint (0x03). This function implements the serial console:
 *
 * 1. Reads incoming data from USB endpoint
 * 2. Echoes characters back to the host (for terminal display)
 * 3. Accumulates characters until CR or LF (Enter key)
 * 4. Processes complete commands via process_serial_command()
 * 5. Sends command response and new prompt to host
 *
 * ## Input Buffer
 *
 * Characters are accumulated in a static 2048-byte buffer until
 * a newline is received. This allows for long commands (hex data).
 *
 * ## Echo Behavior
 *
 * - All received characters are echoed back immediately
 * - CR (\\r) is converted to CR+LF for proper line advancement
 * - After command execution, response + "duck> " prompt is sent
 *
 * @param dev USB device instance
 * @param ep  Endpoint that received data (always CDCACM_UART_ENDPOINT)
 *
 * @note Uses static variables, so this function is not reentrant.
 *       The typing buffer supports commands up to 2048 characters.
 *
 * @see process_serial_command() for command handling
 * @see send_chunked_blocking() for response transmission
 */
static void usbuart_usb_out_cb(usbd_device *dev, uint8_t ep)
{
	(void)ep;

	char buf[CDCACM_PACKET_SIZE];     /* Receive buffer for USB packet */
	char reply_buf[256];              /* Response buffer for echo + response */

	static char typing_buf[2048] = {0}; /* Accumulated command line */
	static int typing_index = 0;        /* Current position in typing_buf */

	/* Read data from USB endpoint */
	int len = usbd_ep_read_packet(dev, CDCACM_UART_ENDPOINT,
					buf, CDCACM_PACKET_SIZE);


	int j = 0;  /* Reply buffer write index */
	for(int i = 0; i < len; i++) {
		gpio_toggle(GPIOC, GPIO13);  /* Toggle LED on activity */

		/* Echo character back to host */
		/* CR needs LF added for proper terminal line advancement */
		if (buf[i] == '\r') reply_buf[j++] = '\n';
		reply_buf[j++] = buf[i];

		/* Accumulate character in typing buffer */
		typing_buf[typing_index++] = buf[i];

		/* Check for end of line (command complete) */
		if (buf[i] == '\r' || buf[i] == '\n') {
			/* Process the complete command */
			char *response = process_serial_command(typing_buf, typing_index);
			typing_index = 0;  /* Reset for next command */

			/* Append command response to reply */
			for (size_t k = 0; k < strlen(response); ++k) {
				reply_buf[j++] = response[k];
			}

			/* Append prompt for next command */
			reply_buf[j++] = '\r';
			reply_buf[j++] = '\n';
			reply_buf[j++] = 'd';
			reply_buf[j++] = 'u';
			reply_buf[j++] = 'c';
			reply_buf[j++] = 'k';
			reply_buf[j++] = '>';
			reply_buf[j++] = ' ';
		}
	}

	/* Send accumulated reply (echo + response + prompt) */
	send_chunked_blocking(reply_buf, j, dev, CDCACM_UART_ENDPOINT, CDCACM_PACKET_SIZE);
}

/**
 * @brief USB callback for serial data transmission complete (IN endpoint)
 *
 * Called by the USB stack when a packet has been successfully transmitted
 * on the bulk IN endpoint (0x83). Currently this callback is a no-op
 * since data transmission is handled synchronously in send_chunked_blocking().
 *
 * @param dev USB device instance (unused)
 * @param ep  Endpoint that completed transmission (unused)
 *
 * @note This callback could be used to implement asynchronous/buffered
 *       transmission in a future enhancement.
 */
static void usbuart_usb_in_cb(usbd_device *dev, uint8_t ep)
{
	(void) dev;
	(void) ep;
}

/*============================================================================
 * Public Functions
 *===========================================================================*/

/**
 * @brief Configure the CDC ACM interface after USB enumeration
 *
 * This function is called when the USB host sets the device configuration
 * (SET_CONFIGURATION request). It performs the following setup:
 *
 * 1. **Bulk OUT endpoint (0x03)**: Receives serial data from host
 *    - Callback: usbuart_usb_out_cb() for command processing
 *
 * 2. **Bulk IN endpoint (0x83)**: Sends serial data to host
 *    - Callback: usbuart_usb_in_cb() (currently unused)
 *
 * 3. **Interrupt IN endpoint (0x84)**: Sends modem state notifications
 *    - No callback (notification-only)
 *
 * 4. **Control callback**: Handles CDC class requests
 *    - SET_LINE_CODING, SET_CONTROL_LINE_STATE
 *
 * 5. **Modem state notification**: Asserts DCD and DSR
 *    - Required for proper detection on BSD/macOS
 *
 * @param dev    Pointer to the USB device instance
 * @param wValue Configuration value selected by host (unused, always 1)
 *
 * @note Must be called from the USB set_config callback along with
 *       hid_set_config() for complete composite device setup.
 *
 * @code
 * // Typical usage in main.c:
 * static void usb_set_config(usbd_device *dev, uint16_t wValue) {
 *     hid_set_config(dev, wValue);
 *     cdcacm_set_config(dev, wValue);
 * }
 * @endcode
 *
 * @see hid_set_config() for HID interface configuration
 * @see cdcacm_control_request() for the registered control callback
 */
void cdcacm_set_config(usbd_device *dev, uint16_t wValue)
{
	(void) wValue;

	/* Configure bulk endpoints for serial data */
	usbd_ep_setup(dev, CDCACM_UART_ENDPOINT, USB_ENDPOINT_ATTR_BULK,
	              CDCACM_PACKET_SIZE, usbuart_usb_out_cb);      /* OUT: receive */
	usbd_ep_setup(dev, 0x80 | CDCACM_UART_ENDPOINT, USB_ENDPOINT_ATTR_BULK,
	              CDCACM_PACKET_SIZE, usbuart_usb_in_cb);       /* IN: transmit */

	/* Configure interrupt endpoint for notifications */
	usbd_ep_setup(dev, CDCACM_INTR_ENDPOINT, USB_ENDPOINT_ATTR_INTERRUPT, 16, NULL);

	/* Register callback for CDC class control requests */
	usbd_register_control_callback(dev,
			USB_REQ_TYPE_CLASS | USB_REQ_TYPE_INTERFACE,
			USB_REQ_TYPE_TYPE | USB_REQ_TYPE_RECIPIENT,
			cdcacm_control_request);

	/*
	 * Notify the host that DCD (Data Carrier Detect) is asserted.
	 * This is required for proper serial port detection on *BSD and macOS,
	 * which wait for carrier signal before allowing port access.
	 */
	cdcacm_set_modem_state(dev, 2, true, true);
}


