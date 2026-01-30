// vim: tabstop=8 softtabstop=8 shiftwidth=8 noexpandtab
/**
 * @file cdcacm.h
 * @brief USB CDC ACM (Abstract Control Model) interface declarations
 *
 * This header defines the interface for USB CDC ACM functionality,
 * which provides a virtual serial port (COM port) over USB. The serial
 * interface is used for:
 *
 * - Receiving commands from the host
 * - Writing payload data to flash memory
 * - Reading stored payload data
 * - Controlling script execution (pause/resume/step)
 *
 * ## USB Configuration
 *
 * The CDC ACM implementation uses two USB interfaces:
 * - **Interface 1**: Communication Class Interface (control)
 * - **Interface 2**: Data Class Interface (bulk data transfer)
 *
 * These are associated using an Interface Association Descriptor (IAD)
 * for proper composite device recognition.
 *
 * ## Endpoints
 *
 * - **0x03**: Bulk OUT (host to device data)
 * - **0x83**: Bulk IN (device to host data)
 * - **0x84**: Interrupt IN (notifications)
 *
 * @note The device appears as a standard USB serial port (/dev/ttyACM*
 *       on Linux, /dev/cu.usbmodem* on macOS, COM port on Windows).
 *
 * @see cdcacm.c for implementation
 * @see process_serial_command() in main.c for command processing
 */

#ifndef __CDCACM_H
#define __CDCACM_H

#include <libopencm3/usb/cdc.h>

/*============================================================================
 * External Interface Descriptors
 *===========================================================================*/

/**
 * @brief CDC Communication Class interface descriptor
 *
 * Defines the CDC control interface (Interface 1) including:
 * - Functional descriptors (Header, Call Management, ACM, Union)
 * - Interrupt endpoint for notifications (0x84)
 *
 * This interface handles CDC class-specific control requests like
 * SET_LINE_CODING and SET_CONTROL_LINE_STATE.
 */
extern const struct usb_interface_descriptor uart_comm_iface[];

/**
 * @brief CDC Data Class interface descriptor
 *
 * Defines the CDC data interface (Interface 2) with:
 * - Bulk OUT endpoint (0x03) for receiving data from host
 * - Bulk IN endpoint (0x83) for sending data to host
 *
 * This interface carries the actual serial data (commands and responses).
 */
extern const struct usb_interface_descriptor uart_data_iface[];

/**
 * @brief Interface Association Descriptor for CDC ACM
 *
 * Associates the Communication and Data interfaces together as a single
 * CDC ACM function. Required for composite USB devices to properly
 * identify multi-interface functions.
 *
 * Configuration:
 * - First Interface: 1
 * - Interface Count: 2 (interfaces 1 and 2)
 * - Function Class: CDC
 * - Function SubClass: ACM
 */
extern const struct usb_iface_assoc_descriptor uart_assoc;

/*============================================================================
 * Function Declarations
 *===========================================================================*/

/**
 * @brief Configure the CDC ACM interface after USB enumeration
 *
 * Called when the USB host sets the device configuration. This function:
 * 1. Sets up bulk endpoints for data transfer (0x03 OUT, 0x83 IN)
 * 2. Sets up interrupt endpoint for notifications (0x84)
 * 3. Registers control callback for CDC class requests
 * 4. Asserts DCD/DSR signals for proper serial port detection
 *
 * @param dev    Pointer to the USB device instance
 * @param wValue Configuration value selected by host (unused)
 *
 * @note This should be called from the main usb_set_config callback
 *       along with hid_set_config() for full composite device setup.
 *
 * @code
 * // Example usage in set_config callback:
 * static void usb_set_config(usbd_device *dev, uint16_t wValue) {
 *     hid_set_config(dev, wValue);
 *     cdcacm_set_config(dev, wValue);
 * }
 * @endcode
 *
 * @see hid_set_config() for HID interface setup
 */
void cdcacm_set_config(usbd_device *dev, uint16_t wValue);

#endif /* __CDCACM_H */
