// vim: tabstop=8 softtabstop=8 shiftwidth=8 noexpandtab
/**
 * @file flash.c
 * @brief STM32F103 internal flash memory read/write implementation
 *
 * This module provides functionality to read and write the STM32F103's
 * internal flash memory. It's used to persistently store HID report
 * payloads that are executed when the device starts up.
 *
 * ## Flash Programming Model
 *
 * The STM32F103 flash has specific requirements:
 *
 * 1. **Erase before write**: Flash bits can only be changed from 1 to 0.
 *    Erasing sets all bits in a page to 1, allowing new data to be written.
 *
 * 2. **Page erase granularity**: The smallest erasable unit is a 1KB page.
 *    You cannot erase individual bytes.
 *
 * 3. **Word writes**: Programming is done in 16-bit half-words, but this
 *    implementation uses 32-bit word writes for efficiency.
 *
 * 4. **Unlock required**: Flash is locked by default. Writing to the
 *    FLASH_KEYR register with specific values unlocks it.
 *
 * ## Implementation Notes
 *
 * - Based on libopencm3 flash example code
 * - Performs verification after each word write
 * - Automatically handles page alignment
 * - Does not re-lock flash after operations (could be improved)
 *
 * @note Flash operations should not be interrupted. Consider disabling
 *       interrupts during critical flash operations in production code.
 *
 * @see flash.h for interface documentation
 * @see https://github.com/libopencm3/libopencm3-examples
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


#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/usart.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/flash.h>

#include "flash.h"
#include "hid.h"

/*============================================================================
 * Constants
 *===========================================================================*/

/**
 * @brief Maximum page number in flash memory
 *
 * STM32F103C8T6 has 64KB flash = 64 pages of 1KB each (pages 0-63).
 * STM32F103CBT6 has 128KB flash = 128 pages (pages 0-127).
 *
 * @note This value (127) assumes the larger 128KB variant.
 */
#define FLASH_PAGE_NUM_MAX 127

/**
 * @brief Size of one flash page in bytes
 *
 * STM32F103 medium-density devices have 1KB (0x400) pages.
 * This is the minimum erasable unit.
 */
#define FLASH_PAGE_SIZE 0x400

/*============================================================================
 * Public Functions
 *===========================================================================*/

/**
 * @brief Program data to internal flash memory
 *
 * Erases the flash page containing the start address, then programs
 * the provided data. Each word is verified after programming.
 *
 * ## Algorithm
 *
 * 1. Calculate page-aligned start address
 * 2. Unlock flash (write key sequence to FLASH_KEYR)
 * 3. Erase the page (sets all bits to 1)
 * 4. For each 32-bit word:
 *    a. Program the word
 *    b. Check status flags
 *    c. Verify written value
 * 5. Return success or error code
 *
 * ## Error Handling
 *
 * The function checks for errors at two points:
 * - After page erase: Returns flash status if not FLASH_SR_EOP
 * - After each word write: Returns status or FLASH_WRONG_DATA_WRITTEN
 *
 * @param start_address Destination address in flash (e.g., 0x08002000)
 * @param input_data    Source data buffer
 * @param num_elements  Number of bytes to program
 *
 * @return RESULT_OK (0) on success
 * @return FLASH_WRONG_DATA_WRITTEN (0x80) on verification failure
 * @return Flash status flags on other errors
 *
 * @warning Erases entire page - existing data will be lost!
 * @warning num_elements should be multiple of 4 for word alignment
 *
 * @note Based on libopencm3 flash_rw_example
 *
 * @see https://github.com/libopencm3/libopencm3-examples/blob/master/examples/stm32/f1/stm32-h107/flash_rw_example/flash_rw_example.c
 */
uint32_t flash_program_data(uint32_t start_address, uint8_t *input_data, uint16_t num_elements)
{
	uint16_t iter;
	uint32_t current_address = start_address;
	uint32_t page_address = start_address;
	uint32_t flash_status = 0;

	/*
	 * Address range check (commented out - relies on linker script)
	 * Uncomment for additional safety if needed:
	 */
	/* if((start_address - FLASH_BASE) >= (FLASH_PAGE_SIZE * (FLASH_PAGE_NUM_MAX+1)))
		return 1; */

	/* Calculate page-aligned address (round down to page boundary) */
	if(start_address % FLASH_PAGE_SIZE)
		page_address -= (start_address % FLASH_PAGE_SIZE);

	/* Unlock flash for write operations */
	flash_unlock();

	/* Erase the target page (required before programming) */
	flash_erase_page(page_address);
	flash_status = flash_get_status_flags();
	if(flash_status != FLASH_SR_EOP)  /* EOP = End Of Program (success) */
		return flash_status;

	/* Program data in 32-bit words */
	for(iter=0; iter<num_elements; iter += 4)
	{
		/* Write one 32-bit word to flash */
		flash_program_word(current_address+iter, *((uint32_t*)(input_data + iter)));

		/* Check for programming errors */
		flash_status = flash_get_status_flags();
		if(flash_status != FLASH_SR_EOP)
			return flash_status;

		/* Verify the written data by reading it back */
		if(*((uint32_t*)(current_address+iter)) != *((uint32_t*)(input_data + iter)))
			return FLASH_WRONG_DATA_WRITTEN;
	}

	return RESULT_OK;
}

/**
 * @brief Read data from internal flash memory
 *
 * Copies data from flash to the provided output buffer. Flash memory
 * is memory-mapped on STM32, so this is essentially a memory copy
 * operation.
 *
 * Reading is done in 32-bit word increments for efficiency, matching
 * the bus width of the ARM Cortex-M3.
 *
 * @param start_address Source address in flash (e.g., 0x08002000)
 * @param num_elements  Number of bytes to read (should be multiple of 4)
 * @param output_data   Destination buffer (must have enough space)
 *
 * @note No error checking - ensure addresses are valid
 * @note Reads floor(num_elements/4) words, so round down behavior
 *       for non-multiples of 4
 *
 * @code
 * // Read first report from flash
 * struct composite_report report;
 * flash_read_data((uint32_t)&user_data, sizeof(report), (uint8_t*)&report);
 * @endcode
 */
void flash_read_data(uint32_t start_address, uint16_t num_elements, uint8_t *output_data)
{
	uint16_t iter;
	uint32_t *memory_ptr= (uint32_t*)start_address;

	/* Read data in 32-bit word increments */
	for(iter=0; iter<num_elements/4; iter++)
	{
		*(uint32_t*)output_data = *(memory_ptr + iter);
		output_data += 4;
	}
}



