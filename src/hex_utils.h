/**
 * @file hex_utils.h
 * @brief Hexadecimal string encoding/decoding utilities
 *
 * This header provides functions for converting between binary data
 * and ASCII hexadecimal string representations. These utilities are
 * essential for the serial command protocol, which uses hex encoding
 * for binary payload data.
 *
 * ## Usage in Pill Duck
 *
 * The serial interface accepts hex-encoded binary data for the `w`
 * (write) and `d` (DuckyScript) commands. For example:
 *
 * ```
 * duck> w0102030405060708    # Writes bytes 0x01, 0x02, ... to flash
 * duck> r                     # Reads and returns hex-encoded data
 * ```
 *
 * ## Encoding Format
 *
 * - Each byte is encoded as two lowercase hex characters
 * - No separators or prefixes (no "0x" or spaces)
 * - Uppercase input is accepted during decoding
 *
 * Examples:
 * - Binary `{0x00, 0xFF, 0x42}` encodes to `"00ff42"`
 * - String `"DeadBeef"` decodes to `{0xDE, 0xAD, 0xBE, 0xEF}`
 *
 * @note Originally from the Black Magic Debug project.
 *
 * @see hex_utils.c for implementation
 *
 * @copyright (C) 2011 Black Sphere Technologies Ltd.
 * @author Gareth McMullin <gareth@blacksphere.co.nz>
 * @license GPL-3.0-or-later
 */

/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2011  Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __HEX_UTILS_H
#define __HEX_UTILS_H

#include <stddef.h>

/**
 * @brief Convert binary data to hexadecimal string
 *
 * Encodes each byte of the input buffer as two lowercase hexadecimal
 * ASCII characters. The output is null-terminated.
 *
 * @param hex   Output buffer for hex string.
 *              Must have space for (size * 2 + 1) bytes.
 * @param buf   Input binary data buffer
 * @param size  Number of bytes to encode
 *
 * @return Pointer to the hex output buffer (same as `hex` parameter)
 *
 * @note Output uses lowercase hex digits (0-9, a-f)
 * @note Output is null-terminated
 *
 * @code
 * uint8_t data[] = {0xDE, 0xAD, 0xBE, 0xEF};
 * char hex[9];  // 4 bytes * 2 + null terminator
 * hexify(hex, data, sizeof(data));
 * // hex now contains "deadbeef\0"
 * @endcode
 */
char * hexify(char *hex, const void *buf, size_t size);

/**
 * @brief Convert hexadecimal string to binary data
 *
 * Decodes pairs of hexadecimal ASCII characters into bytes.
 * Accepts both uppercase and lowercase hex digits.
 *
 * @param buf   Output buffer for binary data.
 *              Must have space for `size` bytes.
 * @param hex   Input hexadecimal string (must contain at least size*2 chars)
 * @param size  Number of bytes to decode (reads size*2 hex characters)
 *
 * @return Pointer to the binary output buffer (same as `buf` parameter)
 *
 * @warning No input validation - invalid hex characters produce undefined output
 * @warning Reads exactly size*2 characters - ensure input is long enough
 *
 * @code
 * const char *hex = "DEADBEEF";
 * uint8_t data[4];
 * unhexify(data, hex, sizeof(data));
 * // data now contains {0xDE, 0xAD, 0xBE, 0xEF}
 * @endcode
 */
char * unhexify(void *buf, const char *hex, size_t size);

#endif /* __HEX_UTILS_H */

