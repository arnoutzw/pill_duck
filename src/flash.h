/**
 * @file flash.h
 * @brief STM32F103 internal flash memory read/write interface
 *
 * This header provides functions for reading and writing data to the
 * STM32F103's internal flash memory. The flash is used to persistently
 * store HID report payloads (keyboard/mouse scripts) that execute
 * automatically on device startup.
 *
 * ## Flash Memory Layout (STM32F103C8T6 - 64KB)
 *
 * The memory is organized as follows (defined in bluepill.ld):
 *
 * | Region    | Address Range         | Size  | Purpose              |
 * |-----------|-----------------------|-------|----------------------|
 * | Firmware  | 0x08000000-0x08001FFF | 8 KB  | Bootloader/firmware  |
 * | User Data | 0x08002000-0x0801FFFF | 120KB | Payload storage      |
 *
 * ## Flash Characteristics
 *
 * - **Page Size**: 1 KB (1024 bytes)
 * - **Erase Granularity**: Entire page must be erased before writing
 * - **Write Granularity**: 32-bit words (4 bytes at a time)
 * - **Endurance**: ~10,000 erase cycles per page
 *
 * ## Usage Notes
 *
 * - Flash must be unlocked before write operations
 * - The page containing the target address is erased before writing
 * - Data verification is performed after each word write
 * - Functions handle flash unlock/lock internally
 *
 * @note The user_data section address is defined in the linker script
 *       and accessed via the user_data symbol in main.c.
 *
 * @see flash.c for implementation
 * @see bluepill.ld for memory layout definition
 *
 * @copyright (C) 2013 Damian Miller <damian.m.miller@gmail.com>
 * @license LGPL-3.0-or-later
 */

/*
 * This file is part of the libopencm3 project.
 *
 * Copyright (C) 2013 Damian Miller <damian.m.miller@gmail.com>
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

#ifndef __FLASH_H
#define __FLASH_H

#include <stdint.h>

/*============================================================================
 * Constants
 *===========================================================================*/

/**
 * @brief Error code: Written data doesn't match expected value
 *
 * Returned by flash_program_data() when the verification read-back
 * doesn't match the data that was written. This may indicate:
 * - Flash cell wear-out
 * - Electrical noise during programming
 * - Hardware fault
 *
 * @note Value chosen to not conflict with libopencm3 FLASH_SR flags
 */
#define FLASH_WRONG_DATA_WRITTEN 0x80

/**
 * @brief Success code: Operation completed successfully
 *
 * Returned by flash_program_data() when all data was written
 * and verified correctly.
 */
#define RESULT_OK 0

/*============================================================================
 * Function Declarations
 *===========================================================================*/

/**
 * @brief Program data to internal flash memory
 *
 * Erases the flash page containing start_address, then writes the
 * provided data. Each 32-bit word is verified after writing.
 *
 * Operation sequence:
 * 1. Calculate page-aligned address
 * 2. Unlock flash for writing
 * 3. Erase the target page
 * 4. Write data in 32-bit words
 * 5. Verify each word after writing
 * 6. Lock flash
 *
 * @param start_address Flash address to start writing (e.g., &user_data)
 *                      Should be within valid flash range
 * @param input_data    Pointer to data buffer to write
 *                      Must contain at least num_elements bytes
 * @param num_elements  Number of bytes to write
 *                      Should be multiple of 4 for proper alignment
 *
 * @return RESULT_OK (0) on success
 * @return FLASH_WRONG_DATA_WRITTEN (0x80) if verification failed
 * @return Other values from flash_get_status_flags() on flash error
 *
 * @warning This function erases the entire page before writing!
 *          Any existing data in the page will be lost.
 *
 * @note The function handles flash unlock/lock internally.
 * @note num_elements should be a multiple of 4 (word-aligned writes)
 *
 * @code
 * // Example: Write HID report data to user_data section
 * uint8_t payload[] = { ... };
 * uint32_t result = flash_program_data((uint32_t)&user_data, payload, sizeof(payload));
 * if (result != RESULT_OK) {
 *     // Handle error
 * }
 * @endcode
 */
uint32_t flash_program_data(uint32_t start_address, uint8_t *input_data, uint16_t num_elements);

/**
 * @brief Read data from internal flash memory
 *
 * Copies data from flash memory to the provided output buffer.
 * Reads are performed in 32-bit word chunks for efficiency.
 *
 * @param start_address Flash address to start reading from
 *                      Must be within valid flash range
 * @param num_elements  Number of bytes to read
 *                      Should be multiple of 4 for proper operation
 * @param output_data   Pointer to buffer to store read data
 *                      Must have space for at least num_elements bytes
 *
 * @note No bounds checking is performed - caller must ensure addresses
 *       are valid and buffer is large enough.
 * @note Reads num_elements/4 words, so num_elements should be multiple of 4.
 *
 * @code
 * // Example: Read first 16 bytes of stored payload
 * uint8_t buffer[16];
 * flash_read_data((uint32_t)&user_data, sizeof(buffer), buffer);
 * @endcode
 */
void flash_read_data(uint32_t start_address, uint16_t num_elements, uint8_t *output_data);

#endif /* __FLASH_H */

