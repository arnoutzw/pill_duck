# Pill Duck Documentation

Pill Duck is a scriptable USB HID device for the STM32F103 "Blue Pill" microcontroller board. It emulates a USB keyboard and mouse, allowing automated input sequences to be stored and executed.

## Table of Contents

### Getting Started
- [Architecture Overview](#architecture-overview)
- [Memory Map](#memory-map)

### Reference Documentation
- [Firmware API](firmware-api.md) - C functions and data structures
- [JavaScript API](javascript-api.md) - Node.js HID report library
- [Serial Commands](serial-commands.md) - Command-line interface reference
- [USB Protocol](usb-protocol.md) - USB descriptors and HID reports

---

## Architecture Overview

Pill Duck presents itself to the host computer as a USB composite device with two functions:

```
┌─────────────────────────────────────────────────────────────────┐
│                         USB Host (PC)                           │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│   ┌─────────────────┐              ┌─────────────────────┐      │
│   │   HID Driver    │              │   Serial Driver     │      │
│   │  (Keyboard/     │              │  (/dev/ttyACM0 or   │      │
│   │   Mouse)        │              │   COM port)         │      │
│   └────────┬────────┘              └──────────┬──────────┘      │
│            │                                  │                 │
└────────────┼──────────────────────────────────┼─────────────────┘
             │                                  │
             │ USB                              │ USB
             │                                  │
┌────────────┼──────────────────────────────────┼─────────────────┐
│            │        Pill Duck Device          │                 │
│   ┌────────▼────────┐              ┌──────────▼──────────┐      │
│   │  HID Interface  │              │  CDC ACM Interface  │      │
│   │  (Endpoint 0x81)│              │  (Endpoints 0x03,   │      │
│   │                 │              │   0x83, 0x84)       │      │
│   └────────┬────────┘              └──────────┬──────────┘      │
│            │                                  │                 │
│            │         ┌──────────────┐         │                 │
│            └────────►│  Execution   │◄────────┘                 │
│                      │   Engine     │                           │
│                      │  (SysTick)   │                           │
│                      └──────┬───────┘                           │
│                             │                                   │
│                      ┌──────▼───────┐                           │
│                      │    Flash     │                           │
│                      │   Storage    │                           │
│                      │ (user_data)  │                           │
│                      └──────────────┘                           │
│                                                                 │
│                      STM32F103 "Blue Pill"                      │
└─────────────────────────────────────────────────────────────────┘
```

### Components

| Component | Description |
|-----------|-------------|
| **HID Interface** | Sends keyboard/mouse reports to the host |
| **CDC ACM Interface** | Virtual serial port for commands and data transfer |
| **Execution Engine** | SysTick-driven state machine that sends HID reports |
| **Flash Storage** | Persistent storage for HID report sequences |

---

## Memory Map

### STM32F103C8T6 Flash Layout (64KB)

| Address | Size | Region | Description |
|---------|------|--------|-------------|
| `0x08000000` | 8 KB | Firmware | Application code and read-only data |
| `0x08002000` | 56 KB | User Data | HID report payload storage |

### STM32F103CBT6 Flash Layout (128KB)

| Address | Size | Region | Description |
|---------|------|--------|-------------|
| `0x08000000` | 8 KB | Firmware | Application code and read-only data |
| `0x08002000` | 120 KB | User Data | HID report payload storage |

### SRAM Layout (20KB)

| Address | Size | Region | Description |
|---------|------|--------|-------------|
| `0x20000000` | 20 KB | RAM | Variables, stack, heap |

---

## Quick Start

### Connecting to the Device

1. Connect the Blue Pill via USB
2. The device appears as:
   - **HID device**: Keyboard and mouse (no driver needed)
   - **Serial port**: `/dev/ttyACM0` (Linux), `/dev/cu.usbmodem*` (macOS), `COMx` (Windows)

3. Open a serial terminal at any baud rate:
   ```bash
   screen /dev/ttyACM0 115200
   # or
   minicom -D /dev/ttyACM0
   ```

4. You'll see the prompt:
   ```
   duck>
   ```

### Basic Commands

```
duck> v                    # Show version
Pill Duck version 1.0

duck> j                    # Load mouse jiggler
wrote flash

duck> p                    # Start/pause execution
resumed

duck> z                    # Reset to beginning
```

See [Serial Commands](serial-commands.md) for the complete reference.

---

## Building from Source

### Prerequisites

- ARM GCC toolchain (`arm-none-eabi-gcc`)
- libopencm3 (included as git submodule)
- Make

### Build Steps

```bash
# Clone with submodules
git clone --recursive https://github.com/user/pill_duck.git
cd pill_duck

# Build libopencm3
cd libopencm3 && make && cd ..

# Build firmware
cd src && make
```

### Flashing

Using ST-Link:
```bash
st-flash write pill_duck.bin 0x08000000
```

Using DFU (if bootloader present):
```bash
dfu-util -a 0 -s 0x08000000 -D pill_duck.bin
```

---

## File Structure

```
pill_duck/
├── src/                    # Firmware source code
│   ├── main.c              # Entry point, execution engine
│   ├── hid.c/h             # USB HID interface
│   ├── cdcacm.c/h          # USB serial interface
│   ├── flash.c/h           # Flash memory operations
│   ├── hex_utils.c/h       # Hex encoding utilities
│   ├── version.h           # Version string
│   ├── bluepill.ld         # Linker script
│   └── Makefile            # Build configuration
├── js/                     # JavaScript utilities
│   ├── hid.js              # HID report codec
│   ├── test.js             # Unit tests
│   └── web/                # Browser interface
├── doc/                    # Documentation
├── libopencm3/             # HAL library (submodule)
└── README.md
```

---

## License

This project is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).

See [COPYING](../COPYING) for details.
