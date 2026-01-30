# Firmware API Reference

This document describes the C functions, data structures, and constants used in the Pill Duck firmware.

## Table of Contents

- [Data Structures](#data-structures)
  - [composite_report](#composite_report)
- [Constants](#constants)
  - [Report IDs](#report-ids)
  - [Flash Constants](#flash-constants)
- [Functions](#functions)
  - [Main Module](#main-module-mainc)
  - [HID Module](#hid-module-hidc)
  - [CDC ACM Module](#cdc-acm-module-cdcacmc)
  - [Flash Module](#flash-module-flashc)
  - [Hex Utilities](#hex-utilities-hex_utilsc)

---

## Data Structures

### composite_report

The core data structure representing a single HID report or control command.

**Definition** (`src/hid.h`):

```c
struct composite_report {
    uint8_t report_id;
    union {
        struct {
            uint8_t buttons;
            uint8_t x;
            uint8_t y;
            uint8_t wheel;
        } __attribute__((packed)) mouse;

        struct {
            uint8_t modifiers;
            uint8_t reserved;
            uint8_t keys_down[6];
            uint8_t leds;
        } __attribute__((packed)) keyboard;

        uint8_t padding[15];
    };
} __attribute__((packed));
```

**Size**: 16 bytes (fixed)

**Fields**:

| Field | Type | Description |
|-------|------|-------------|
| `report_id` | `uint8_t` | Report type identifier (see [Report IDs](#report-ids)) |
| `mouse.buttons` | `uint8_t` | Mouse button bitmask (bit 0=left, 1=right, 2=middle) |
| `mouse.x` | `uint8_t` | Relative X movement (-127 to +127) |
| `mouse.y` | `uint8_t` | Relative Y movement (-127 to +127) |
| `mouse.wheel` | `uint8_t` | Scroll wheel movement (-127 to +127) |
| `keyboard.modifiers` | `uint8_t` | Modifier key bitmask (see below) |
| `keyboard.reserved` | `uint8_t` | Reserved byte (set to 1 for converted reports) |
| `keyboard.keys_down` | `uint8_t[6]` | Array of up to 6 simultaneous key codes |
| `keyboard.leds` | `uint8_t` | LED indicator bitmask |
| `padding` | `uint8_t[15]` | Raw access / delay duration storage |

**Modifier Key Bitmask**:

| Bit | Modifier |
|-----|----------|
| 0 | Left Control |
| 1 | Left Shift |
| 2 | Left Alt |
| 3 | Left GUI (Windows/Command) |
| 4 | Right Control |
| 5 | Right Shift |
| 6 | Right Alt |
| 7 | Right GUI |

**LED Indicator Bitmask**:

| Bit | LED |
|-----|-----|
| 0 | Num Lock |
| 1 | Caps Lock |
| 2 | Scroll Lock |
| 3 | Compose |
| 4 | Kana |

---

## Constants

### Report IDs

Defined in `src/hid.h`:

| Constant | Value | Description |
|----------|-------|-------------|
| `REPORT_ID_NOP` | 0 | No operation (skip this report) |
| `REPORT_ID_KEYBOARD` | 1 | Keyboard HID report |
| `REPORT_ID_MOUSE` | 2 | Mouse HID report |
| `REPORT_ID_DELAY` | 254 | Delay command (duration in `padding[0]`) |
| `REPORT_ID_END` | 255 | End of script marker |

### Flash Constants

Defined in `src/flash.h`:

| Constant | Value | Description |
|----------|-------|-------------|
| `RESULT_OK` | 0 | Flash operation succeeded |
| `FLASH_WRONG_DATA_WRITTEN` | 0x80 | Verification failed after write |
| `FLASH_PAGE_SIZE` | 0x400 (1024) | Flash page size in bytes |
| `FLASH_PAGE_NUM_MAX` | 127 | Maximum page number |

---

## Functions

### Main Module (`main.c`)

#### convert_ducky_binary

Converts compiled DuckyScript binary format to USB HID reports.

```c
int convert_ducky_binary(uint8_t *buf, int len, struct composite_report *out);
```

**Parameters**:

| Parameter | Type | Description |
|-----------|------|-------------|
| `buf` | `uint8_t *` | Input buffer containing compiled DuckyScript |
| `len` | `int` | Length of input in bytes |
| `out` | `struct composite_report *` | Output buffer for HID reports |

**Returns**: Number of `composite_report` records written

**Notes**:
- Input must be 16-bit word-aligned (rounded down if odd)
- Each keystroke generates 2 reports (press + release)
- Output always ends with `REPORT_ID_END` marker

**DuckyScript Binary Format**:

| Low Byte | High Byte | Meaning |
|----------|-----------|---------|
| 0x00 | delay_ms | Delay for specified milliseconds |
| keycode | modifiers | Press key with modifiers |

---

#### sys_tick_handler

SysTick interrupt handler - executes HID reports from flash.

```c
void sys_tick_handler(void);
```

**Behavior**:
- Called every 1ms by SysTick timer
- Reads current report from `user_data[report_index]`
- Sends keyboard/mouse reports via USB endpoint 0x81
- Handles delay commands by counting down ticks
- Resets index on `REPORT_ID_END`

**Global State Used**:

| Variable | Type | Description |
|----------|------|-------------|
| `paused` | `bool` | If true, execution is paused |
| `single_step` | `bool` | If true, execute one report then pause |
| `report_index` | `uint32_t` | Current position in report array |
| `delaying` | `bool` | Currently processing a delay |
| `delay_ticks_remaining` | `int` | Remaining delay ticks |

---

#### add_mouse_jiggler

Generates a mouse jiggler pattern.

```c
int add_mouse_jiggler(int width);
```

**Parameters**:

| Parameter | Type | Description |
|-----------|------|-------------|
| `width` | `int` | Pixels to move in each direction |

**Returns**: Number of reports generated

**Notes**:
- Writes to global `packet_buffer`
- Generates `width` right moves, then `width` left moves
- Net cursor movement is zero

---

#### process_serial_command

Processes commands from the serial interface.

```c
char *process_serial_command(char *buf, int len);
```

**Parameters**:

| Parameter | Type | Description |
|-----------|------|-------------|
| `buf` | `char *` | Command string (first char is command) |
| `len` | `int` | Length of command string |

**Returns**: Response string to display

See [Serial Commands](serial-commands.md) for command reference.

---

### HID Module (`hid.c`)

#### hid_set_config

Configures HID interface after USB enumeration.

```c
void hid_set_config(usbd_device *dev, uint16_t wValue);
```

**Parameters**:

| Parameter | Type | Description |
|-----------|------|-------------|
| `dev` | `usbd_device *` | USB device instance |
| `wValue` | `uint16_t` | Configuration value (unused) |

**Actions**:
1. Sets up endpoint 0x81 (Interrupt IN)
2. Registers HID control request callback

---

### CDC ACM Module (`cdcacm.c`)

#### cdcacm_set_config

Configures CDC ACM interface after USB enumeration.

```c
void cdcacm_set_config(usbd_device *dev, uint16_t wValue);
```

**Parameters**:

| Parameter | Type | Description |
|-----------|------|-------------|
| `dev` | `usbd_device *` | USB device instance |
| `wValue` | `uint16_t` | Configuration value (unused) |

**Actions**:
1. Sets up bulk endpoints (0x03 OUT, 0x83 IN)
2. Sets up interrupt endpoint (0x84)
3. Registers CDC control callback
4. Asserts DCD/DSR modem signals

---

### Flash Module (`flash.c`)

#### flash_program_data

Writes data to internal flash memory.

```c
uint32_t flash_program_data(uint32_t start_address, uint8_t *input_data, uint16_t num_elements);
```

**Parameters**:

| Parameter | Type | Description |
|-----------|------|-------------|
| `start_address` | `uint32_t` | Destination flash address |
| `input_data` | `uint8_t *` | Source data buffer |
| `num_elements` | `uint16_t` | Number of bytes to write |

**Returns**:

| Value | Meaning |
|-------|---------|
| `RESULT_OK` (0) | Success |
| `FLASH_WRONG_DATA_WRITTEN` (0x80) | Verification failed |
| Other | Flash status flags |

**Notes**:
- Erases the page containing `start_address` before writing
- Writes in 32-bit words; `num_elements` should be multiple of 4
- Verifies each word after writing

---

#### flash_read_data

Reads data from internal flash memory.

```c
void flash_read_data(uint32_t start_address, uint16_t num_elements, uint8_t *output_data);
```

**Parameters**:

| Parameter | Type | Description |
|-----------|------|-------------|
| `start_address` | `uint32_t` | Source flash address |
| `num_elements` | `uint16_t` | Number of bytes to read |
| `output_data` | `uint8_t *` | Destination buffer |

**Notes**:
- Reads in 32-bit words
- `num_elements` should be multiple of 4

---

### Hex Utilities (`hex_utils.c`)

#### hexify

Converts binary data to hexadecimal string.

```c
char * hexify(char *hex, const void *buf, size_t size);
```

**Parameters**:

| Parameter | Type | Description |
|-----------|------|-------------|
| `hex` | `char *` | Output buffer (must hold `size*2+1` bytes) |
| `buf` | `const void *` | Input binary data |
| `size` | `size_t` | Number of bytes to convert |

**Returns**: Pointer to `hex` buffer

**Notes**:
- Output is null-terminated
- Uses lowercase hex digits (0-9, a-f)

**Example**:
```c
uint8_t data[] = {0xDE, 0xAD, 0xBE, 0xEF};
char hex[9];
hexify(hex, data, 4);  // hex = "deadbeef"
```

---

#### unhexify

Converts hexadecimal string to binary data.

```c
char * unhexify(void *buf, const char *hex, size_t size);
```

**Parameters**:

| Parameter | Type | Description |
|-----------|------|-------------|
| `buf` | `void *` | Output buffer (must hold `size` bytes) |
| `hex` | `const char *` | Input hex string |
| `size` | `size_t` | Number of bytes to produce |

**Returns**: Pointer to `buf`

**Notes**:
- Accepts both uppercase and lowercase
- Reads `size*2` characters from input
- No input validation

**Example**:
```c
const char *hex = "DEADBEEF";
uint8_t data[4];
unhexify(data, hex, 4);  // data = {0xDE, 0xAD, 0xBE, 0xEF}
```

---

## Global Variables

### Execution State

| Variable | Type | Location | Description |
|----------|------|----------|-------------|
| `report_index` | `uint32_t` | `main.c` | Current position in report array |
| `paused` | `bool` | `main.c` | Execution paused flag |
| `single_step` | `bool` | `main.c` | Single-step mode flag |
| `delaying` | `bool` | `main.c` | Processing delay flag |
| `delay_ticks_remaining` | `int` | `main.c` | Remaining delay ticks |

### Storage

| Variable | Type | Location | Description |
|----------|------|----------|-------------|
| `user_data` | `struct composite_report[]` | Flash | Persistent payload storage |
| `packet_buffer` | `struct composite_report[]` | RAM | Temporary conversion buffer |

### USB

| Variable | Type | Location | Description |
|----------|------|----------|-------------|
| `usbd_dev` | `usbd_device *` | `main.c` | USB device instance |
