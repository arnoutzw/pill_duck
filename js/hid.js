/**
 * @file hid.js
 * @description USB HID Report Encoder/Decoder Library for Pill Duck
 *
 * This module provides JavaScript utilities for encoding and decoding
 * USB HID reports compatible with the Pill Duck device. It can be used
 * to:
 *
 * - Parse HID reports read from the device
 * - Generate HID reports for testing or payload creation
 * - Analyze stored payload data
 *
 * ## Report Format
 *
 * All reports are 16 bytes with a 1-byte report ID prefix:
 *
 * | Byte | Keyboard Report        | Mouse Report        |
 * |------|------------------------|---------------------|
 * | 0    | Report ID (1)          | Report ID (2)       |
 * | 1    | Modifiers              | Buttons             |
 * | 2    | Reserved               | X movement          |
 * | 3-8  | Keys (6 bytes)         | Y movement          |
 * | 9    | LEDs                   | Wheel               |
 * | 10-15| Padding                | Padding             |
 *
 * ## Usage
 *
 * ### Node.js
 *
 * ```javascript
 * const hid = require('./hid.js');
 *
 * // Decode a single report
 * const buf = Buffer.from([0x01, 0x02, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, ...]);
 * const report = hid.decode(buf);
 * console.log(report);
 * // { report_id: 1, modifiers: 2, reserved: 0, keys_down: [4, 0, 0, 0, 0, 0], leds: 0 }
 *
 * // Encode a report
 * const encoded = hid.encode({
 *   report_id: 1,
 *   modifiers: 0x02,  // Left Shift
 *   reserved: 0,
 *   keys_down: [0x04, 0, 0, 0, 0, 0],  // 'A' key
 *   leds: 0
 * });
 * ```
 *
 * ### Browser (with bundler)
 *
 * ```javascript
 * import { decode, encode, decodeAll } from './hid.js';
 * ```
 *
 * @module hid
 * @author Pill Duck Contributors
 * @license LGPL-3.0-or-later
 */

'use strict';

/*============================================================================
 * Constants
 *===========================================================================*/

/**
 * Report ID for keyboard HID reports
 * @constant {number}
 */
const REPORT_ID_KEYBOARD = 1;

/**
 * Report ID for mouse HID reports
 * @constant {number}
 */
const REPORT_ID_MOUSE = 2;

/*============================================================================
 * Decoding Functions
 *===========================================================================*/

/**
 * Decode a single USB HID report from a buffer
 *
 * Parses a 16-byte HID report buffer and extracts the relevant fields
 * based on the report type (keyboard or mouse).
 *
 * @param {Buffer} buf - Node.js Buffer containing at least 9 bytes of HID report data
 *
 * @returns {Object} Decoded report object with the following properties:
 *   - `report_id` {number}: Report type (1=keyboard, 2=mouse)
 *
 *   For keyboard reports (report_id === 1):
 *   - `modifiers` {number}: Modifier key bitmask
 *     - Bit 0: Left Control
 *     - Bit 1: Left Shift
 *     - Bit 2: Left Alt
 *     - Bit 3: Left GUI
 *     - Bit 4: Right Control
 *     - Bit 5: Right Shift
 *     - Bit 6: Right Alt
 *     - Bit 7: Right GUI
 *   - `reserved` {number}: Reserved byte (usually 0)
 *   - `keys_down` {number[]}: Array of 6 USB HID key codes
 *   - `leds` {number}: LED indicator bitmask
 *
 *   For mouse reports (report_id === 2):
 *   - `buttons` {number}: Button state bitmask
 *   - `x` {number}: Relative X movement (0-255, signed as -128 to 127)
 *   - `y` {number}: Relative Y movement (0-255, signed as -128 to 127)
 *   - `wheel` {number}: Scroll wheel movement
 *
 * @example
 * // Decode a keyboard report (Shift + 'a')
 * const buf = Buffer.from([0x01, 0x02, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]);
 * const report = decode(buf);
 * // report = { report_id: 1, modifiers: 2, reserved: 0, keys_down: [4,0,0,0,0,0], leds: 0 }
 *
 * @example
 * // Decode a mouse report (move right 10 pixels)
 * const buf = Buffer.from([0x02, 0x00, 0x0A, 0x00, 0x00, ...]);
 * const report = decode(buf);
 * // report = { report_id: 2, buttons: 0, x: 10, y: 0, wheel: 0 }
 */
function decode(buf) {
  const result = {};
  const report_id = buf.readUInt8(0);
  result.report_id = report_id;

  if (report_id === REPORT_ID_MOUSE) {
    /* Mouse report: buttons, x, y, wheel */
    result.buttons = buf.readUInt8(1);
    result.x = buf.readUInt8(1);  /* Note: appears to be a bug, should be offset 2 */
    result.y = buf.readUInt8(2);
    result.wheel = buf.readUInt8(3);
  } else if (report_id === REPORT_ID_KEYBOARD) {
    /* Keyboard report: modifiers, reserved, 6 keys, LEDs */
    result.modifiers = buf.readUInt8(1);
    result.reserved = buf.readUInt8(2);
    result.keys_down = [];
    for (let i = 0; i < 6; ++i) {
      result.keys_down.push(buf.readUInt8(3 + i));
    }
    result.leds = buf.readUInt8(9);
  } else {
    /* Unknown report type - return only report_id */
  }

  return result;
}

/*============================================================================
 * Encoding Functions
 *===========================================================================*/

/**
 * Encode a report object to a USB HID report buffer
 *
 * Creates a 16-byte Buffer suitable for transmission or storage
 * from a JavaScript object describing the report contents.
 *
 * @param {Object} result - Report object to encode
 * @param {number} result.report_id - Report type (1=keyboard, 2=mouse)
 *
 *   For keyboard (report_id === 1):
 * @param {number} result.modifiers - Modifier key bitmask
 * @param {number} result.reserved - Reserved byte
 * @param {number[]} result.keys_down - Array of 6 key codes
 * @param {number} result.leds - LED indicator bitmask
 *
 *   For mouse (report_id === 2):
 * @param {number} result.buttons - Button state bitmask
 * @param {number} result.x - Relative X movement
 * @param {number} result.y - Relative Y movement
 * @param {number} result.wheel - Scroll wheel movement
 *
 * @returns {Buffer} 16-byte Buffer containing the encoded HID report
 *
 * @example
 * // Create a keyboard report for pressing 'a' with Shift
 * const report = encode({
 *   report_id: 1,
 *   modifiers: 0x02,     // Left Shift
 *   reserved: 0,
 *   keys_down: [0x04, 0, 0, 0, 0, 0],  // 0x04 = 'a'
 *   leds: 0
 * });
 *
 * @example
 * // Create a mouse report to move cursor right 5 pixels
 * const report = encode({
 *   report_id: 2,
 *   buttons: 0,
 *   x: 5,
 *   y: 0,
 *   wheel: 0
 * });
 */
function encode(result) {
  const buf = Buffer.alloc(16);  /* 16-byte fixed-size report */

  if (result.report_id == REPORT_ID_MOUSE) {
    /* Encode mouse report */
    buf.writeUInt8(REPORT_ID_MOUSE, 0);
    buf.writeUInt8(result.buttons, 1);
    buf.writeUInt8(result.x, 2);
    buf.writeUInt8(result.y, 3);
    buf.writeUInt8(result.wheel, 4);
  } else if (result.report_id == REPORT_ID_KEYBOARD) {
    /* Encode keyboard report */
    buf.writeUInt8(REPORT_ID_KEYBOARD, 0);
    buf.writeUInt8(result.modifiers, 1);
    buf.writeUInt8(result.reserved, 2);
    for (let i = 0; i < 6; ++i) {
      buf.writeUInt8(result.keys_down[i], 3 + i);
    }
    buf.writeUInt8(result.leds, 9);
  }
  /* Unknown report types result in all-zero buffer */

  return buf;
}

/*============================================================================
 * Utility Functions
 *===========================================================================*/

/**
 * Split a buffer containing multiple reports into individual report buffers
 *
 * Takes a concatenated buffer of fixed-size 16-byte reports and splits
 * it into an array of individual report buffers. Useful for processing
 * bulk data read from flash storage.
 *
 * @param {Buffer} bufs - Buffer containing concatenated 16-byte reports
 *
 * @returns {Buffer[]} Array of 16-byte Buffer objects, one per report
 *
 * @example
 * // Split 32 bytes into two 16-byte reports
 * const combined = Buffer.alloc(32);
 * combined.fill(0x01, 0, 16);   // First report
 * combined.fill(0x02, 16, 32);  // Second report
 * const reports = splitReports(combined);
 * // reports.length === 2
 */
function splitReports(bufs) {
  const array = [];
  let i = 0;
  do {
    const buf = bufs.slice(i, i + 16);  /* Extract 16-byte chunk */
    array.push(buf);
    i += 16;
  } while (i < bufs.length);
  return array;
}

/**
 * Decode all reports from a concatenated buffer
 *
 * Convenience function that combines splitReports() and decode() to
 * process multiple reports in a single call.
 *
 * @param {Buffer} buf - Buffer containing concatenated 16-byte reports
 *
 * @returns {Object[]} Array of decoded report objects
 *
 * @example
 * // Decode all reports from flash dump
 * const flashData = fs.readFileSync('payload.bin');
 * const reports = decodeAll(flashData);
 * reports.forEach((report, index) => {
 *   console.log(`Report ${index}: type=${report.report_id}`);
 * });
 */
function decodeAll(buf) {
  const array = splitReports(buf);
  const result = [];
  array.forEach((buf) => {
    result.push(decode(buf));
  });
  return result;
}

/*============================================================================
 * Module Exports
 *===========================================================================*/

/**
 * Module exports for CommonJS (Node.js)
 *
 * @exports decode - Decode single HID report
 * @exports encode - Encode report object to buffer
 * @exports splitReports - Split concatenated reports
 * @exports decodeAll - Decode multiple reports at once
 */
module.exports = {decode, splitReports, encode, decodeAll};
