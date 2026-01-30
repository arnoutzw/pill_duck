// vim: tabstop=8 softtabstop=8 shiftwidth=8 noexpandtab
/**
 * @file hid.c
 * @brief USB HID (Human Interface Device) interface implementation
 *
 * This module implements the USB HID interface for the Pill Duck device,
 * providing keyboard and mouse emulation capabilities. It handles:
 *
 * - USB HID descriptor configuration
 * - HID endpoint setup (Endpoint 0x81, Interrupt IN)
 * - HID control requests (GET_DESCRIPTOR for report descriptor)
 *
 * The HID interface is part of a USB composite device that also includes
 * a CDC ACM (serial) interface for command and control.
 *
 * ## USB Configuration
 *
 * - **Interface Number**: 0
 * - **Endpoint**: 0x81 (Interrupt IN)
 * - **Max Packet Size**: 9 bytes (report ID + 8 bytes data)
 * - **Polling Interval**: 32ms (0x20)
 * - **Boot Protocol**: Mouse (allows BIOS compatibility)
 *
 * ## HID Reports
 *
 * The device sends composite reports with a report ID prefix:
 * - Report ID 1: Keyboard (9 bytes)
 * - Report ID 2: Mouse (5 bytes)
 *
 * @note This implementation uses libopencm3 USB stack.
 *
 * @see hid.h for data structures and constants
 * @see main.c for the execution engine that sends HID reports
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
#include <libopencm3/usb/hid.h>

#define INCLUDE_PACKET_DESCRIPTOR  /**< Enable HID report descriptor definition in hid.h */
#include "hid.h"


/*============================================================================
 * USB HID Function Descriptor
 *===========================================================================*/

/**
 * @brief Combined HID descriptor structure
 *
 * This structure contains the HID class descriptor and subordinate
 * report descriptor reference. It is included as "extra" data in
 * the interface descriptor and is sent to the host during enumeration.
 *
 * Structure layout (9 bytes total):
 * @code
 * Offset  Size  Field
 * ------  ----  -----
 * 0       1     bLength (9)
 * 1       1     bDescriptorType (0x21 = HID)
 * 2       2     bcdHID (0x0100 = HID 1.0)
 * 4       1     bCountryCode (0 = not localized)
 * 5       1     bNumDescriptors (1)
 * 6       1     bDescriptorType (0x22 = Report)
 * 7       2     wDescriptorLength (size of report descriptor)
 * @endcode
 *
 * @see USB HID Specification 1.11, Section 6.2.1
 */
static const struct {
	struct usb_hid_descriptor hid_descriptor;   /**< Standard HID descriptor */
	struct {
		uint8_t bReportDescriptorType;      /**< Report descriptor type (0x22) */
		uint16_t wDescriptorLength;         /**< Length of report descriptor */
	} __attribute__((packed)) hid_report;       /**< Report descriptor reference */
} __attribute__((packed)) hid_function = {
	.hid_descriptor = {
		.bLength = sizeof(hid_function),
		.bDescriptorType = USB_DT_HID,
		.bcdHID = 0x0100,    /* HID Class Specification 1.0 */
		.bCountryCode = 0,   /* Not localized */
		.bNumDescriptors = 1,
	},
	.hid_report = {
		.bReportDescriptorType = USB_DT_REPORT,
		.wDescriptorLength = sizeof(hid_report_descriptor),
	}
};


/*============================================================================
 * USB Endpoint and Interface Descriptors
 *===========================================================================*/

/**
 * @brief HID endpoint descriptor for keyboard/mouse reports
 *
 * Defines the interrupt IN endpoint used to send HID reports to the host.
 *
 * Configuration:
 * - **Endpoint Address**: 0x81 (Endpoint 1, IN direction)
 * - **Type**: Interrupt (for HID devices)
 * - **Max Packet Size**: 9 bytes (1 report ID + 8 bytes max data)
 * - **Polling Interval**: 32ms (0x20) - how often host polls for data
 *
 * @note Interrupt endpoints guarantee bounded latency for input devices.
 *       The host will poll this endpoint every 32ms for new data.
 */
const struct usb_endpoint_descriptor hid_endpoint = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = 0x81,                    /* EP1 IN */
	.bmAttributes = USB_ENDPOINT_ATTR_INTERRUPT, /* Interrupt transfer type */
	.wMaxPacketSize = 9,                         /* Report ID + 8 bytes */
	.bInterval = 0x20,                           /* Poll every 32ms */
};

/**
 * @brief HID interface descriptor for composite keyboard/mouse device
 *
 * Defines the HID interface (Interface 0) in the USB configuration.
 * This interface presents as a boot-compatible mouse for BIOS support,
 * but the report descriptor also includes keyboard functionality.
 *
 * Configuration:
 * - **Interface Number**: 0 (first interface in composite device)
 * - **Class**: HID (Human Interface Device)
 * - **SubClass**: 1 (Boot Interface)
 * - **Protocol**: 2 (Mouse) - for BIOS/boot compatibility
 *
 * @note Boot protocol support allows the device to work in BIOS setup
 *       menus before OS drivers are loaded.
 *
 * @see hid_endpoint for the associated endpoint
 * @see hid_function for the HID descriptor (extra data)
 */
struct usb_interface_descriptor hid_iface = {
	.bLength = USB_DT_INTERFACE_SIZE,
	.bDescriptorType = USB_DT_INTERFACE,
	.bInterfaceNumber = 0,        /* First interface */
	.bAlternateSetting = 0,
	.bNumEndpoints = 1,           /* One interrupt IN endpoint */
	.bInterfaceClass = USB_CLASS_HID,
	.bInterfaceSubClass = 1,      /* Boot interface subclass */
	.bInterfaceProtocol = 2,      /* Mouse protocol (boot compatible) */
	.iInterface = 0,              /* No string descriptor */

	.endpoint = &hid_endpoint,

	.extra = &hid_function,       /* HID descriptor */
	.extralen = sizeof(hid_function),
};

/*============================================================================
 * USB Control Request Handlers
 *===========================================================================*/

/**
 * @brief Handle HID-specific USB control requests
 *
 * This callback handles GET_DESCRIPTOR requests for the HID report
 * descriptor. The host requests this descriptor after enumeration to
 * understand the format of HID reports the device will send.
 *
 * Request handled:
 * - **bmRequestType**: 0x81 (Device-to-host, Standard, Interface)
 * - **bRequest**: GET_DESCRIPTOR (0x06)
 * - **wValue**: 0x2200 (HID Report Descriptor, Index 0)
 *
 * @param dev      USB device instance (unused)
 * @param req      USB setup packet containing the request details:
 *                 - bmRequestType: Request characteristics
 *                 - bRequest: Specific request (GET_DESCRIPTOR)
 *                 - wValue: Descriptor type (high byte) and index (low byte)
 *                 - wIndex: Interface number
 *                 - wLength: Maximum bytes to return
 * @param buf      [out] Pointer to response data buffer
 * @param len      [in/out] On input: max length; On output: actual length
 * @param complete Completion callback (unused)
 *
 * @return 1 if the request was handled, 0 if not handled (passed to next handler)
 *
 * @note This function is registered as a control callback in hid_set_config()
 *       and is called by the USB stack when control requests arrive.
 *
 * @see hid_report_descriptor for the descriptor returned
 * @see USB HID Specification 1.11, Section 7.1
 */
static int hid_control_request(usbd_device *dev, struct usb_setup_data *req, uint8_t **buf, uint16_t *len,
			void (**complete)(usbd_device *, struct usb_setup_data *))
{
	(void)complete;
	(void)dev;

	/* Only handle GET_DESCRIPTOR for HID Report Descriptor */
	if((req->bmRequestType != 0x81) ||
	   (req->bRequest != USB_REQ_GET_DESCRIPTOR) ||
	   (req->wValue != 0x2200))  /* 0x22 = Report descriptor type, 0x00 = index */
		return 0;

	/* Return the HID report descriptor */
	*buf = (uint8_t *)hid_report_descriptor;
	*len = sizeof(hid_report_descriptor);

	return 1;
}

/**
 * @brief Configure the HID interface after USB enumeration
 *
 * Called when the USB host sets the device configuration (SET_CONFIGURATION).
 * This function sets up the HID interrupt endpoint and registers the
 * control request callback for handling HID-specific requests.
 *
 * Setup performed:
 * 1. Configure endpoint 0x81 as an interrupt IN endpoint
 * 2. Register hid_control_request() for handling GET_DESCRIPTOR requests
 *
 * @param dev    Pointer to the USB device instance
 * @param wValue Configuration value selected by host (unused, always 1)
 *
 * @note This should be called from the main usb_set_config callback along
 *       with cdcacm_set_config() to fully configure the composite device.
 *
 * @code
 * // Example usage in set_config callback:
 * static void usb_set_config(usbd_device *dev, uint16_t wValue) {
 *     hid_set_config(dev, wValue);
 *     cdcacm_set_config(dev, wValue);
 * }
 * @endcode
 *
 * @see hid_control_request() for the control callback
 * @see cdcacm_set_config() for the serial interface setup
 */
void hid_set_config(usbd_device *dev, uint16_t wValue)
{
	(void)wValue;
	(void)dev;

	/* Setup endpoint 0x81: Interrupt IN, 4 byte buffer, no callback */
	usbd_ep_setup(dev, 0x81, USB_ENDPOINT_ATTR_INTERRUPT, 4, NULL);

	/* Register control callback for HID class requests
	 * Mask: Standard requests to interface recipient
	 */
	usbd_register_control_callback(
				dev,
				USB_REQ_TYPE_STANDARD | USB_REQ_TYPE_INTERFACE,
				USB_REQ_TYPE_TYPE | USB_REQ_TYPE_RECIPIENT,
				hid_control_request);

}


