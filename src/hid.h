// vim: tabstop=8 softtabstop=8 shiftwidth=8 noexpandtab
/**
 * @file hid.h
 * @brief USB Human Interface Device (HID) definitions and data structures
 *
 * This header file defines the data structures and constants for USB HID
 * composite device functionality, supporting both keyboard and mouse input.
 * The Pill Duck device presents itself as a USB HID device capable of
 * emulating keyboard keystrokes and mouse movements.
 *
 * @note The composite_report structure must match the HID report descriptor
 *       defined in hid.c for proper USB communication.
 *
 * @see hid.c for implementation details
 * @see https://www.usb.org/hid for USB HID specification
 */

#ifndef __HID_H
#define __HID_H

#include <libopencm3/usb/usbd.h>
#include <libopencm3/usb/hid.h>

/*============================================================================
 * External Declarations
 *===========================================================================*/

/**
 * @brief USB HID interface descriptor
 *
 * Defines the HID interface configuration including endpoint setup,
 * interface class (USB_CLASS_HID), and boot protocol settings.
 * This is interface 0 in the composite USB device.
 */
extern struct usb_interface_descriptor hid_iface;

/**
 * @brief Configure the HID interface after USB enumeration
 *
 * This function is called when the USB host sets the device configuration.
 * It sets up the HID endpoint (0x81) and registers the control request
 * callback for handling HID-specific USB requests (e.g., GET_DESCRIPTOR
 * for the HID report descriptor).
 *
 * @param dev    Pointer to the USB device instance
 * @param wValue Configuration value selected by the host (unused)
 *
 * @note This function should be called from the USB set_config callback
 *       along with cdcacm_set_config() for full composite device setup.
 */
extern void hid_set_config(usbd_device *dev, uint16_t wValue);

/*============================================================================
 * Report ID Constants
 *===========================================================================*/

/**
 * @defgroup ReportIDs USB HID Report IDs
 * @brief Report IDs for composite HID device
 *
 * These report IDs prefix each HID report to distinguish between
 * keyboard and mouse data. The host uses these IDs to route data
 * to the appropriate input device driver.
 * @{
 */

/**
 * @brief Report ID for keyboard HID reports
 *
 * Keyboard reports contain modifier keys (Ctrl, Shift, Alt, GUI),
 * and up to 6 simultaneously pressed keys using USB HID key codes.
 */
#define REPORT_ID_KEYBOARD	1

/**
 * @brief Report ID for mouse HID reports
 *
 * Mouse reports contain button states (3 buttons), relative X/Y
 * movement (-127 to +127), and scroll wheel movement.
 */
#define REPORT_ID_MOUSE		2

/** @} */ /* end of ReportIDs group */

/*============================================================================
 * Pseudo Report IDs (Internal Use)
 *===========================================================================*/

/**
 * @defgroup PseudoReportIDs Internal Pseudo Report IDs
 * @brief Internal command identifiers for script execution engine
 *
 * These are not actual USB HID report IDs but rather internal markers
 * used by the script execution engine to handle timing and control flow.
 * @{
 */

/**
 * @brief No operation report ID
 *
 * When encountered during execution, this report is skipped without
 * sending any USB data. Used for empty or padding entries.
 */
#define REPORT_ID_NOP		0

/**
 * @brief Delay command pseudo-report ID
 *
 * Indicates a timing delay in the script execution. The delay duration
 * in milliseconds is stored in the first padding byte (padding[0]).
 * Execution pauses for the specified number of SysTick timer intervals.
 */
#define REPORT_ID_DELAY		254

/**
 * @brief End of script marker pseudo-report ID
 *
 * Signals the end of the stored payload script. When the execution
 * engine encounters this ID, it resets the report index to 0 and
 * optionally loops or stops execution.
 */
#define REPORT_ID_END		255

/** @} */ /* end of PseudoReportIDs group */

/*============================================================================
 * Data Structures
 *===========================================================================*/

/**
 * @brief USB HID composite report structure
 *
 * This structure represents a single HID report packet that can contain
 * either keyboard or mouse data. The 16-byte fixed size ensures alignment
 * with flash page boundaries and simplifies storage management.
 *
 * Memory Layout (16 bytes total):
 * @code
 * Offset  Size  Field
 * ------  ----  -----
 * 0       1     report_id
 * 1-15    15    payload (keyboard, mouse, or padding)
 * @endcode
 *
 * @note Structure is packed to ensure no padding is inserted by the compiler,
 *       maintaining exact USB report format compatibility.
 *
 * @see REPORT_ID_KEYBOARD
 * @see REPORT_ID_MOUSE
 */
struct composite_report {
	/**
	 * @brief Report type identifier
	 *
	 * Identifies the type of report contained in this structure.
	 * Valid values:
	 * - REPORT_ID_KEYBOARD (1): Keyboard data in keyboard union member
	 * - REPORT_ID_MOUSE (2): Mouse data in mouse union member
	 * - REPORT_ID_NOP (0): No operation, skip this report
	 * - REPORT_ID_DELAY (254): Delay command, duration in padding[0]
	 * - REPORT_ID_END (255): End of script marker
	 */
	uint8_t report_id;

	/**
	 * @brief Report payload union
	 *
	 * Contains the actual HID report data. The active member is
	 * determined by the report_id field.
	 */
	union {
		/**
		 * @brief Mouse HID report data
		 *
		 * Standard USB HID mouse report format.
		 * Total size: 4 bytes (excluding report_id)
		 */
		struct {
			/**
			 * @brief Mouse button states
			 *
			 * Bit field for button states:
			 * - Bit 0: Left button (1 = pressed)
			 * - Bit 1: Right button (1 = pressed)
			 * - Bit 2: Middle button (1 = pressed)
			 * - Bits 3-7: Reserved
			 */
			uint8_t buttons;

			/**
			 * @brief Relative X-axis movement
			 *
			 * Signed 8-bit value (-127 to +127).
			 * Positive values move cursor right, negative left.
			 */
			uint8_t x;

			/**
			 * @brief Relative Y-axis movement
			 *
			 * Signed 8-bit value (-127 to +127).
			 * Positive values move cursor down, negative up.
			 */
			uint8_t y;

			/**
			 * @brief Scroll wheel movement
			 *
			 * Signed 8-bit value (-127 to +127).
			 * Positive values scroll up, negative scroll down.
			 */
			uint8_t wheel;
		} __attribute__((packed)) mouse;

		/**
		 * @brief Keyboard HID report data
		 *
		 * Standard USB HID boot keyboard report format.
		 * Total size: 9 bytes (excluding report_id)
		 */
		struct {
			/**
			 * @brief Modifier key states
			 *
			 * Bit field for modifier keys:
			 * - Bit 0: Left Control
			 * - Bit 1: Left Shift
			 * - Bit 2: Left Alt
			 * - Bit 3: Left GUI (Windows/Command key)
			 * - Bit 4: Right Control
			 * - Bit 5: Right Shift
			 * - Bit 6: Right Alt
			 * - Bit 7: Right GUI
			 */
			uint8_t modifiers;

			/**
			 * @brief Reserved byte
			 *
			 * Reserved for OEM use, typically set to 0 or 1.
			 * In DuckyScript conversion, set to 1 as a marker.
			 */
			uint8_t reserved;

			/**
			 * @brief Array of pressed key codes
			 *
			 * Up to 6 simultaneously pressed keys using USB HID
			 * key codes (not ASCII). Key code 0 indicates no key.
			 *
			 * Common key codes:
			 * - 0x04-0x1D: Letters a-z
			 * - 0x1E-0x27: Numbers 1-0
			 * - 0x28: Enter
			 * - 0x29: Escape
			 * - 0x2A: Backspace
			 * - 0x2C: Space
			 *
			 * @see USB HID Usage Tables for complete key code list
			 */
			uint8_t keys_down[6];

			/**
			 * @brief LED indicator states (output report)
			 *
			 * Bit field for keyboard LEDs:
			 * - Bit 0: Num Lock
			 * - Bit 1: Caps Lock
			 * - Bit 2: Scroll Lock
			 * - Bit 3: Compose
			 * - Bit 4: Kana
			 *
			 * @note This is typically set by the host, not the device.
			 */
			uint8_t leds;
		} __attribute__((packed)) keyboard;

		/**
		 * @brief Raw padding bytes
		 *
		 * Used for delay commands (duration in padding[0]) or
		 * to ensure the structure is always 16 bytes total.
		 */
		uint8_t padding[15];
	};
} __attribute__((packed));


/*============================================================================
 * HID Report Descriptor
 *===========================================================================*/

#ifdef INCLUDE_PACKET_DESCRIPTOR /* Included only in hid.c to avoid duplicate symbols */

/**
 * @brief USB HID Report Descriptor
 *
 * This descriptor tells the USB host the format and capabilities of the
 * HID reports this device sends. It defines a composite device with:
 *
 * 1. **Keyboard (Report ID 1)**: Boot-compatible keyboard
 *    - 8-bit modifier byte (Ctrl, Shift, Alt, GUI keys)
 *    - 1 reserved byte
 *    - 6-byte key array (6-key rollover)
 *    - 1-byte LED output for indicators
 *
 * 2. **Mouse (Report ID 2)**: 3-button mouse with scroll wheel
 *    - 3 button bits + 5 padding bits
 *    - 8-bit relative X movement (-127 to +127)
 *    - 8-bit relative Y movement (-127 to +127)
 *    - 8-bit scroll wheel
 *    - Motion wakeup feature for power management
 *
 * @note This descriptor MUST match the composite_report structure layout
 *       for correct operation.
 *
 * @see http://eleccelerator.com/tutorial-about-usb-hid-report-descriptors/
 * @see http://eleccelerator.com/usbdescreqparser/
 * @see USB HID Specification 1.11
 */
static const uint8_t hid_report_descriptor[] = {
	0x05, 0x01,        // Usage Page (Generic Desktop Ctrls)
	0x09, 0x06,        // Usage (Keyboard)
	0xA1, 0x01,        // Collection (Application)
	0x85, 0x01,        //   Report ID (1)
	0x05, 0x07,        //   Usage Page (Kbrd/Keypad)
	0x19, 0xE0,        //   Usage Minimum (0xE0)
	0x29, 0xE7,        //   Usage Maximum (0xE7)
	0x15, 0x00,        //   Logical Minimum (0)
	0x25, 0x01,        //   Logical Maximum (1)
	0x75, 0x01,        //   Report Size (1)
	0x95, 0x08,        //   Report Count (8)
	0x81, 0x02,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
	0x81, 0x01,        //   Input (Const,Array,Abs,No Wrap,Linear,Preferred State,No Null Position)
	0x19, 0x00,        //   Usage Minimum (0x00)
	0x29, 0x65,        //   Usage Maximum (0x65)
	0x15, 0x00,        //   Logical Minimum (0)
	0x25, 0x65,        //   Logical Maximum (101)
	0x75, 0x08,        //   Report Size (8)
	0x95, 0x06,        //   Report Count (6)
	0x81, 0x00,        //   Input (Data,Array,Abs,No Wrap,Linear,Preferred State,No Null Position)
	0x05, 0x08,        //   Usage Page (LEDs)
	0x19, 0x01,        //   Usage Minimum (Num Lock)
	0x29, 0x05,        //   Usage Maximum (Kana)
	0x15, 0x00,        //   Logical Minimum (0)
	0x25, 0x01,        //   Logical Maximum (1)
	0x75, 0x01,        //   Report Size (1)
	0x95, 0x05,        //   Report Count (5)
	0x91, 0x02,        //   Output (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
	0x95, 0x03,        //   Report Count (3)
	0x91, 0x01,        //   Output (Const,Array,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
	0xC0,              // End Collection
	0x05, 0x01,        // Usage Page (Generic Desktop Ctrls)
	0x09, 0x02,        // Usage (Mouse)
	0xA1, 0x01,        // Collection (Application)
	0x85, 0x02,        //   Report ID (2)
	0x09, 0x01,        //   Usage (Pointer)
	0xA1, 0x00,        //   Collection (Physical)
	0x05, 0x09,        //     Usage Page (Button)
	0x19, 0x01,        //     Usage Minimum (0x01)
	0x29, 0x03,        //     Usage Maximum (0x03)
	0x15, 0x00,        //     Logical Minimum (0)
	0x25, 0x01,        //     Logical Maximum (1)
	0x95, 0x03,        //     Report Count (3)
	0x75, 0x01,        //     Report Size (1)
	0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
	0x95, 0x01,        //     Report Count (1)
	0x75, 0x05,        //     Report Size (5)
	0x81, 0x01,        //     Input (Const,Array,Abs,No Wrap,Linear,Preferred State,No Null Position)
	0x05, 0x01,        //     Usage Page (Generic Desktop Ctrls)
	0x09, 0x30,        //     Usage (X)
	0x09, 0x31,        //     Usage (Y)
	0x09, 0x38,        //     Usage (Wheel)
	0x15, 0x81,        //     Logical Minimum (-127)
	0x25, 0x7F,        //     Logical Maximum (127)
	0x75, 0x08,        //     Report Size (8)
	0x95, 0x03,        //     Report Count (3)
	0x81, 0x06,        //     Input (Data,Var,Rel,No Wrap,Linear,Preferred State,No Null Position)
	0xC0,              //   End Collection
	0x09, 0x3C,        //   Usage (Motion Wakeup)
	0x05, 0xFF,        //   Usage Page (Reserved 0xFF)
	0x09, 0x01,        //   Usage (0x01)
	0x15, 0x00,        //   Logical Minimum (0)
	0x25, 0x01,        //   Logical Maximum (1)
	0x75, 0x01,        //   Report Size (1)
	0x95, 0x02,        //   Report Count (2)
	0xB1, 0x22,        //   Feature (Data,Var,Abs,No Wrap,Linear,No Preferred State,No Null Position,Non-volatile)
	0x75, 0x06,        //   Report Size (6)
	0x95, 0x01,        //   Report Count (1)
	0xB1, 0x01,        //   Feature (Const,Array,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
	0xC0,              // End Collection
};

#endif /* INCLUDE_PACKET_DESCRIPTOR */

#endif /* __HID_H */
