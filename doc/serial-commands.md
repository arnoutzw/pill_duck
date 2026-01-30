# Serial Command Reference

Pill Duck provides a command-line interface over USB serial for controlling the device, uploading payloads, and debugging execution.

## Table of Contents

- [Connecting](#connecting)
- [Command Summary](#command-summary)
- [Command Reference](#command-reference)
  - [v - Version](#v---version)
  - [? - Help](#---help)
  - [w - Write Raw Data](#w---write-raw-data)
  - [d - Write DuckyScript](#d---write-duckyscript)
  - [j - Mouse Jiggler](#j---mouse-jiggler)
  - [r - Read Flash](#r---read-flash)
  - [@ - Show Index](#---show-index)
  - [p - Pause/Resume](#p---pauseresume)
  - [s - Single Step](#s---single-step)
  - [z - Reset Index](#z---reset-index)
- [Data Format](#data-format)
- [Examples](#examples)

---

## Connecting

The device appears as a USB serial port:

| Platform | Device Path |
|----------|-------------|
| Linux | `/dev/ttyACM0` or `/dev/ttyACM1` |
| macOS | `/dev/cu.usbmodem*` |
| Windows | `COM3` (or other COM port) |

### Terminal Examples

```bash
# Linux/macOS with screen
screen /dev/ttyACM0 115200

# Linux/macOS with minicom
minicom -D /dev/ttyACM0

# Linux with picocom
picocom /dev/ttyACM0

# Windows with PuTTY
# Select Serial, COM3, 115200 baud
```

**Note**: Baud rate doesn't matter (it's a virtual serial port), but most terminals require a value.

### Prompt

After connecting, you'll see the prompt:

```
duck>
```

Commands are single characters, optionally followed by data. Press Enter to execute.

---

## Command Summary

| Command | Arguments | Description |
|---------|-----------|-------------|
| `v` | (none) | Display firmware version |
| `?` | (none) | Show help |
| `w` | `<hex_data>` | Write raw hex data to flash |
| `d` | `<hex_data>` | Write compiled DuckyScript to flash |
| `j` | (none) | Write mouse jiggler to flash |
| `r` | (none) | Read first 16 bytes from flash |
| `@` | (none) | Show current report index |
| `p` | (none) | Toggle pause/resume |
| `s` | (none) | Execute single report |
| `z` | (none) | Reset index to zero |

---

## Command Reference

### v - Version

Displays the firmware version string.

**Syntax**: `v`

**Example**:
```
duck> v
Pill Duck version 1.0
```

---

### ? - Help

Shows a brief help message.

**Syntax**: `?`

**Example**:
```
duck> ?
see source code for help
```

---

### w - Write Raw Data

Writes raw hexadecimal data directly to flash storage. Use this to upload pre-formatted HID report data.

**Syntax**: `w<hex_data>`

**Parameters**:
- `hex_data`: Hexadecimal string (no spaces, no 0x prefix)

**Response**:
- `wrote flash` - Success
- `wrong data written` - Verification failed
- `error writing flash` - Flash error

**Example**:
```
duck> w0102030405060708090a0b0c0d0e0f10
wrote flash
```

**Notes**:
- Data is written starting at the `user_data` address
- Previous data in the flash page is erased
- Maximum single write is ~1KB (limited by input buffer)

---

### d - Write DuckyScript

Writes compiled DuckyScript binary to flash. The binary is converted to HID reports before storage.

**Syntax**: `d<hex_data>`

**Parameters**:
- `hex_data`: Compiled DuckyScript in hexadecimal format

**Response**: Same as `w` command

**DuckyScript Binary Format**:

Each instruction is 2 bytes (little-endian):

| Byte 0 (Low) | Byte 1 (High) | Meaning |
|--------------|---------------|---------|
| `0x00` | `delay_ms` | Delay for N milliseconds |
| `keycode` | `modifiers` | Press key with modifiers |

**Example**:
```
duck> d0700020700
wrote flash
```

This writes:
- `0x07 0x02` = 'd' key (0x07) with Shift (0x02) = 'D'
- `0x07 0x00` = 'd' key without modifier = 'd'

**Conversion Process**:

For each keystroke, two HID reports are generated:
1. Key press (key down + modifiers)
2. Key release (all keys up)

---

### j - Mouse Jiggler

Writes a mouse jiggler pattern to flash. The pattern moves the mouse cursor back and forth to prevent screen lock.

**Syntax**: `j`

**Response**: Same as `w` command

**Pattern Generated**:
1. Move right 1 pixel (30 times)
2. Move left 1 pixel (30 times)
3. End marker

**Example**:
```
duck> j
wrote flash
duck> p
resumed
```

The mouse cursor will oscillate slightly, keeping the system awake.

---

### r - Read Flash

Reads and displays the first 16 bytes of flash storage as hexadecimal.

**Syntax**: `r`

**Response**: 32-character hex string (16 bytes)

**Example**:
```
duck> r
01020000040000000000000000000000
```

This shows the first `composite_report` structure:
- `01` = `REPORT_ID_KEYBOARD`
- `02` = modifiers (Left Shift)
- `00` = reserved
- `04` = first key ('a')
- ... etc

---

### @ - Show Index

Displays the current execution index (which report will be sent next).

**Syntax**: `@`

**Response**: Index as hexadecimal (little-endian)

**Example**:
```
duck> @
05000000
```

This means `report_index = 5` (the 6th report, 0-indexed).

**Note**: The output is in little-endian format. `05000000` = 0x00000005 = 5.

---

### p - Pause/Resume

Toggles execution pause state.

**Syntax**: `p`

**Response**:
- `paused` - Execution stopped
- `resumed` - Execution started/continued

**Example**:
```
duck> p
resumed
duck> p
paused
```

**Behavior**:
- When paused: SysTick handler skips report execution
- When resumed: Reports are sent at 1ms intervals

---

### s - Single Step

Executes exactly one report, then pauses.

**Syntax**: `s`

**Response**: `step`

**Example**:
```
duck> s
step
duck> @
01000000
duck> s
step
duck> @
02000000
```

**Use Case**: Debugging payload execution by stepping through reports one at a time.

---

### z - Reset Index

Resets the execution index to zero (beginning of payload).

**Syntax**: `z`

**Response**: (empty)

**Example**:
```
duck> @
0a000000
duck> z
duck> @
00000000
```

---

## Data Format

### Hexadecimal Encoding

All binary data is transmitted as ASCII hexadecimal:
- Each byte becomes two hex characters
- Lowercase or uppercase accepted
- No separators (spaces, colons, etc.)
- No prefix (`0x` is not used)

**Examples**:

| Binary | Hex String |
|--------|------------|
| `{0x01, 0x02, 0x03}` | `010203` |
| `{0xFF, 0x00, 0xAB}` | `ff00ab` |
| `{0xDE, 0xAD, 0xBE, 0xEF}` | `deadbeef` |

### Report Structure

Each HID report is 16 bytes:

**Keyboard Report**:
```
Offset: 00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F
Data:   ID MD RS K1 K2 K3 K4 K5 K6 LD 00 00 00 00 00 00
```

| Field | Offset | Description |
|-------|--------|-------------|
| ID | 0 | Report ID (0x01 for keyboard) |
| MD | 1 | Modifier bitmask |
| RS | 2 | Reserved |
| K1-K6 | 3-8 | Key codes (up to 6) |
| LD | 9 | LED bitmask |

**Mouse Report**:
```
Offset: 00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F
Data:   ID BT XX YY WH 00 00 00 00 00 00 00 00 00 00 00
```

| Field | Offset | Description |
|-------|--------|-------------|
| ID | 0 | Report ID (0x02 for mouse) |
| BT | 1 | Button bitmask |
| XX | 2 | X movement |
| YY | 3 | Y movement |
| WH | 4 | Wheel movement |

---

## Examples

### Upload a Simple Keystroke

Type the letter 'a':

```
duck> w0100000400000000000000000000000001000000000000000000000000000000ff00000000000000000000000000000000
wrote flash
duck> p
resumed
```

This uploads:
1. Keyboard report: press 'a' (keycode 0x04)
2. Keyboard report: release all keys
3. End marker (0xFF)

### Type "Hi" Using DuckyScript Format

```
duck> d0b0212000000
wrote flash
```

Breakdown:
- `0b02` = 'h' (0x0B) + Shift (0x02) = 'H'
- `0c00` = 'i' (0x0C) + no modifier = 'i'
- `0000` = padding (optional)

### Debug Execution

```
duck> z           # Reset to beginning
duck> @           # Check index
00000000
duck> s           # Execute one report
step
duck> @           # Check index
01000000
duck> s           # Execute next report
step
duck> @
02000000
duck> p           # Resume normal execution
resumed
```

### Check Payload Contents

```
duck> r
0102000400000000000000000000000
```

Interpretation:
- `01` = REPORT_ID_KEYBOARD
- `02` = Modifiers (Left Shift)
- `00` = Reserved
- `04` = Key 'a'
- Rest = zeros (no other keys, padding)

---

## Error Handling

| Response | Meaning | Solution |
|----------|---------|----------|
| `wrote flash` | Success | - |
| `wrong data written` | Verification failed | Retry, check hardware |
| `error writing flash` | Flash error | Check address, retry |
| `invalid command, try ? for help` | Unknown command | Check spelling |

---

## Tips

1. **Pause before uploading**: Use `p` to pause execution before uploading new payloads
2. **Reset after upload**: Use `z` to reset index to start of new payload
3. **Test with single-step**: Use `s` to debug payloads step-by-step
4. **Check with read**: Use `r` to verify payload was written correctly
