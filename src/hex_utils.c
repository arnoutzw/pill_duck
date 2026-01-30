// vim: tabstop=8 softtabstop=8 shiftwidth=8 noexpandtab
/**
 * @file hex_utils.c
 * @brief Hexadecimal string encoding/decoding implementation
 *
 * This module provides bidirectional conversion between binary data
 * and ASCII hexadecimal string representations. It's used extensively
 * in the serial command protocol for encoding payload data.
 *
 * ## Algorithm Details
 *
 * ### Encoding (hexify)
 *
 * Each byte is split into two 4-bit nibbles, which are then used as
 * indices into a lookup table of hex characters:
 *
 * ```
 * Byte 0xAB -> High nibble: A (0x0A) -> 'a'
 *           -> Low nibble:  B (0x0B) -> 'b'
 * Result: "ab"
 * ```
 *
 * ### Decoding (unhexify)
 *
 * Each pair of hex characters is converted to their numeric values
 * and combined into a single byte:
 *
 * ```
 * "ab" -> High char 'a' -> 0x0A, shifted left 4 -> 0xA0
 *      -> Low char  'b' -> 0x0B
 *      -> Combined: 0xA0 | 0x0B = 0xAB
 * ```
 *
 * ## Character Handling
 *
 * The decoding algorithm handles case-insensitive input through
 * a clever subtraction technique:
 * - '0'-'9' map directly to 0-9
 * - 'A'-'F' subtract extra offset to get 10-15
 * - 'a'-'f' subtract additional offset to match uppercase
 *
 * @note Originally from the Black Magic Debug project
 *
 * @see hex_utils.h for function documentation
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

#include <stdlib.h>
#include <stdint.h>
#include "hex_utils.h"

/*============================================================================
 * Private Constants
 *===========================================================================*/

/**
 * @brief Lookup table for hex digit characters
 *
 * Maps nibble values (0-15) to their ASCII hex character representation.
 * Uses lowercase letters for a-f.
 */
static const char hexdigits[] = "0123456789abcdef";

/*============================================================================
 * Public Functions
 *===========================================================================*/

/**
 * @brief Convert binary data to hexadecimal string
 *
 * Iterates through each byte of the input buffer, extracting the high
 * and low nibbles and looking up the corresponding hex character.
 *
 * @param hex   Output buffer (must hold size*2+1 bytes)
 * @param buf   Input binary buffer
 * @param size  Number of bytes to convert
 *
 * @return Pointer to hex buffer
 *
 * @note Output is null-terminated
 * @note Uses lowercase hex digits
 */
char * hexify(char *hex, const void *buf, size_t size)
{
	char *tmp = hex;
	const uint8_t *b = buf;

	while (size--) {
		/* High nibble: shift right 4 bits, lookup in table */
		*tmp++ = hexdigits[*b >> 4];
		/* Low nibble: mask with 0x0F, lookup in table */
		*tmp++ = hexdigits[*b++ & 0xF];
	}
	*tmp++ = 0;  /* Null terminate */

	return hex;
}

/*============================================================================
 * Private Functions
 *===========================================================================*/

/**
 * @brief Convert single hex character to numeric value
 *
 * Uses arithmetic character manipulation for efficient conversion:
 * 1. Subtract '0' to handle digits 0-9
 * 2. If result > 9, adjust for uppercase letters A-F
 * 3. If result > 16, adjust for lowercase letters a-f
 *
 * @param hex Single hex character ('0'-'9', 'A'-'F', or 'a'-'f')
 *
 * @return Numeric value 0-15
 *
 * @warning No validation - invalid input produces undefined results
 *
 * @note The subtraction constants work as follows:
 *       - '0' = 48, 'A' = 65, 'a' = 97
 *       - 'A' - '0' - 10 = 65 - 48 - 10 = 7
 *       - 'a' - 'A' = 97 - 65 = 32
 */
static uint8_t unhex_digit(char hex)
{
	uint8_t tmp = hex - '0';      /* Works for '0'-'9' -> 0-9 */
	if(tmp > 9)
		tmp -= 'A' - '0' - 10;    /* Adjust for 'A'-'F' -> 10-15 */
	if(tmp > 16)
		tmp -= 'a' - 'A';         /* Adjust for 'a'-'f' -> 10-15 */
	return tmp;
}

/*============================================================================
 * Public Functions (continued)
 *===========================================================================*/

/**
 * @brief Convert hexadecimal string to binary data
 *
 * Reads pairs of hex characters and combines them into bytes.
 * Each pair consists of a high nibble (shifted left 4) and a low nibble.
 *
 * @param buf   Output buffer (must hold `size` bytes)
 * @param hex   Input hex string (must have at least size*2 characters)
 * @param size  Number of bytes to produce
 *
 * @return Pointer to output buffer
 *
 * @warning Reads exactly size*2 characters - no bounds checking
 * @warning Invalid hex characters produce undefined results
 */
char * unhexify(void *buf, const char *hex, size_t size)
{
	uint8_t *b = buf;
	while (size--) {
		/* High nibble: first char shifted left 4 bits */
		*b = unhex_digit(*hex++) << 4;
		/* Low nibble: second char OR'd into low bits */
		*b++ |= unhex_digit(*hex++);
	}
	return buf;
}

