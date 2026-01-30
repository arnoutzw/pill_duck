# USB Protocol Documentation

This document describes the USB implementation of the Pill Duck device, including descriptors, endpoints, and HID report formats.

## Table of Contents

- [Device Overview](#device-overview)
- [USB Descriptors](#usb-descriptors)
  - [Device Descriptor](#device-descriptor)
  - [Configuration Descriptor](#configuration-descriptor)
  - [Interface Descriptors](#interface-descriptors)
  - [Endpoint Descriptors](#endpoint-descriptors)
  - [HID Report Descriptor](#hid-report-descriptor)
- [Endpoints](#endpoints)
- [HID Reports](#hid-reports)
  - [Keyboard Report](#keyboard-report)
  - [Mouse Report](#mouse-report)
- [CDC ACM Protocol](#cdc-acm-protocol)
- [Enumeration Sequence](#enumeration-sequence)

---

## Device Overview

Pill Duck is a USB 2.0 Full-Speed composite device with:

| Function | Class | Interfaces | Purpose |
|----------|-------|------------|---------|
| HID | 0x03 | 0 | Keyboard and mouse input |
| CDC ACM | 0x02 | 1, 2 | Virtual serial port |

**Identification**:

| Field | Value | Description |
|-------|-------|-------------|
| Vendor ID | `0x05AC` | Apple Inc.* |
| Product ID | `0x2227` | Aluminum Keyboard (ISO)* |
| Device Version | `2.00` | bcdDevice |
| USB Version | `2.00` | bcdUSB |

*Note: Using Apple VID/PID improves keyboard compatibility on some systems. For production use, obtain proper VID/PID allocation.

---

## USB Descriptors

### Device Descriptor

```
bLength            : 18
bDescriptorType    : 0x01 (DEVICE)
bcdUSB             : 0x0200 (USB 2.0)
bDeviceClass       : 0x00 (Defined at interface level)
bDeviceSubClass    : 0x00
bDeviceProtocol    : 0x00
bMaxPacketSize0    : 64
idVendor           : 0x05AC
idProduct          : 0x2227
bcdDevice          : 0x0200
iManufacturer      : 1 ("satoshinm")
iProduct           : 2 ("Pill Duck")
iSerialNumber      : 3 ("ABC")
bNumConfigurations : 1
```

### Configuration Descriptor

```
bLength             : 9
bDescriptorType     : 0x02 (CONFIGURATION)
wTotalLength        : (calculated)
bNumInterfaces      : 3
bConfigurationValue : 1
iConfiguration      : 0
bmAttributes        : 0xC0 (Self-powered)
bMaxPower           : 0x32 (100mA)
```

### Interface Descriptors

#### Interface 0: HID (Keyboard/Mouse)

```
bLength            : 9
bDescriptorType    : 0x04 (INTERFACE)
bInterfaceNumber   : 0
bAlternateSetting  : 0
bNumEndpoints      : 1
bInterfaceClass    : 0x03 (HID)
bInterfaceSubClass : 0x01 (Boot Interface)
bInterfaceProtocol : 0x02 (Mouse)
iInterface         : 0
```

**HID Descriptor** (follows interface):

```
bLength            : 9
bDescriptorType    : 0x21 (HID)
bcdHID             : 0x0100 (HID 1.0)
bCountryCode       : 0x00
bNumDescriptors    : 1
bDescriptorType    : 0x22 (Report)
wDescriptorLength  : (size of report descriptor)
```

#### Interface 1: CDC Communication

```
bLength            : 9
bDescriptorType    : 0x04 (INTERFACE)
bInterfaceNumber   : 1
bAlternateSetting  : 0
bNumEndpoints      : 1
bInterfaceClass    : 0x02 (CDC)
bInterfaceSubClass : 0x02 (ACM)
bInterfaceProtocol : 0x01 (AT Commands)
iInterface         : 4 ("Pill Duck UART Port")
```

**CDC Functional Descriptors** (follow interface):

| Descriptor | Type | Purpose |
|------------|------|---------|
| Header | 0x00 | CDC version (1.10) |
| Call Management | 0x01 | Call handling (none) |
| ACM | 0x02 | ACM capabilities |
| Union | 0x06 | Groups interfaces 1 and 2 |

#### Interface 2: CDC Data

```
bLength            : 9
bDescriptorType    : 0x04 (INTERFACE)
bInterfaceNumber   : 2
bAlternateSetting  : 0
bNumEndpoints      : 2
bInterfaceClass    : 0x0A (CDC Data)
bInterfaceSubClass : 0x00
bInterfaceProtocol : 0x00
iInterface         : 0
```

#### Interface Association Descriptor

Groups CDC interfaces (1 and 2) as a single function:

```
bLength            : 8
bDescriptorType    : 0x0B (INTERFACE_ASSOCIATION)
bFirstInterface    : 1
bInterfaceCount    : 2
bFunctionClass     : 0x02 (CDC)
bFunctionSubClass  : 0x02 (ACM)
bFunctionProtocol  : 0x01 (AT Commands)
iFunction          : 0
```

### Endpoint Descriptors

| Endpoint | Direction | Type | Size | Interval | Interface | Purpose |
|----------|-----------|------|------|----------|-----------|---------|
| 0x81 | IN | Interrupt | 9 | 32ms | 0 (HID) | HID reports |
| 0x84 | IN | Interrupt | 16 | 255ms | 1 (CDC Comm) | Notifications |
| 0x03 | OUT | Bulk | 128 | - | 2 (CDC Data) | Serial RX |
| 0x83 | IN | Bulk | 128 | - | 2 (CDC Data) | Serial TX |

### HID Report Descriptor

The HID report descriptor defines a composite device with keyboard and mouse functionality:

```c
// Keyboard Collection (Report ID 1)
0x05, 0x01,        // Usage Page (Generic Desktop)
0x09, 0x06,        // Usage (Keyboard)
0xA1, 0x01,        // Collection (Application)
0x85, 0x01,        //   Report ID (1)

// Modifier keys (8 bits)
0x05, 0x07,        //   Usage Page (Key Codes)
0x19, 0xE0,        //   Usage Minimum (Left Control)
0x29, 0xE7,        //   Usage Maximum (Right GUI)
0x15, 0x00,        //   Logical Minimum (0)
0x25, 0x01,        //   Logical Maximum (1)
0x75, 0x01,        //   Report Size (1)
0x95, 0x08,        //   Report Count (8)
0x81, 0x02,        //   Input (Data, Variable, Absolute)

// Reserved byte
0x95, 0x01,        //   Report Count (1)
0x75, 0x08,        //   Report Size (8)
0x81, 0x01,        //   Input (Constant)

// LEDs (output)
0x95, 0x05,        //   Report Count (5)
0x75, 0x01,        //   Report Size (1)
0x05, 0x08,        //   Usage Page (LEDs)
0x19, 0x01,        //   Usage Minimum (Num Lock)
0x29, 0x05,        //   Usage Maximum (Kana)
0x91, 0x02,        //   Output (Data, Variable, Absolute)

// LED padding
0x95, 0x01,        //   Report Count (1)
0x75, 0x03,        //   Report Size (3)
0x91, 0x01,        //   Output (Constant)

// Key array (6 keys)
0x95, 0x06,        //   Report Count (6)
0x75, 0x08,        //   Report Size (8)
0x15, 0x00,        //   Logical Minimum (0)
0x26, 0xFF, 0x00,  //   Logical Maximum (255)
0x05, 0x07,        //   Usage Page (Key Codes)
0x19, 0x00,        //   Usage Minimum (0)
0x2A, 0xFF, 0x00,  //   Usage Maximum (255)
0x81, 0x00,        //   Input (Data, Array)

0xC0,              // End Collection

// Mouse Collection (Report ID 2)
0x05, 0x01,        // Usage Page (Generic Desktop)
0x09, 0x02,        // Usage (Mouse)
0xA1, 0x01,        // Collection (Application)
0x09, 0x01,        //   Usage (Pointer)
0xA1, 0x00,        //   Collection (Physical)
0x85, 0x02,        //     Report ID (2)

// Buttons (3)
0x05, 0x09,        //     Usage Page (Buttons)
0x19, 0x01,        //     Usage Minimum (Button 1)
0x29, 0x03,        //     Usage Maximum (Button 3)
0x15, 0x00,        //     Logical Minimum (0)
0x25, 0x01,        //     Logical Maximum (1)
0x95, 0x03,        //     Report Count (3)
0x75, 0x01,        //     Report Size (1)
0x81, 0x02,        //     Input (Data, Variable, Absolute)

// Button padding
0x95, 0x01,        //     Report Count (1)
0x75, 0x05,        //     Report Size (5)
0x81, 0x01,        //     Input (Constant)

// X, Y movement
0x05, 0x01,        //     Usage Page (Generic Desktop)
0x09, 0x30,        //     Usage (X)
0x09, 0x31,        //     Usage (Y)
0x15, 0x81,        //     Logical Minimum (-127)
0x25, 0x7F,        //     Logical Maximum (127)
0x75, 0x08,        //     Report Size (8)
0x95, 0x02,        //     Report Count (2)
0x81, 0x06,        //     Input (Data, Variable, Relative)

// Wheel
0x09, 0x38,        //     Usage (Wheel)
0x15, 0x81,        //     Logical Minimum (-127)
0x25, 0x7F,        //     Logical Maximum (127)
0x75, 0x08,        //     Report Size (8)
0x95, 0x01,        //     Report Count (1)
0x81, 0x06,        //     Input (Data, Variable, Relative)

// Motion wakeup
0x05, 0x0C,        //     Usage Page (Consumer)
0x0A, 0x38, 0x02,  //     Usage (AC Pan)
0x95, 0x01,        //     Report Count (1)
0x81, 0x06,        //     Input (Data, Variable, Relative)

0xC0,              //   End Collection
0xC0,              // End Collection
```

---

## Endpoints

### Endpoint 0x81 - HID Reports (Interrupt IN)

| Property | Value |
|----------|-------|
| Direction | Device to Host (IN) |
| Type | Interrupt |
| Max Packet | 9 bytes |
| Interval | 32ms |

**Usage**: Sends keyboard and mouse HID reports to the host.

**Data Format**: `[Report ID][Data...]`

### Endpoint 0x84 - CDC Notifications (Interrupt IN)

| Property | Value |
|----------|-------|
| Direction | Device to Host (IN) |
| Type | Interrupt |
| Max Packet | 16 bytes |
| Interval | 255ms |

**Usage**: Sends CDC notifications (SERIAL_STATE) for modem signals.

### Endpoint 0x03 - Serial Data (Bulk OUT)

| Property | Value |
|----------|-------|
| Direction | Host to Device (OUT) |
| Type | Bulk |
| Max Packet | 128 bytes |

**Usage**: Receives serial data (commands) from host.

### Endpoint 0x83 - Serial Data (Bulk IN)

| Property | Value |
|----------|-------|
| Direction | Device to Host (IN) |
| Type | Bulk |
| Max Packet | 128 bytes |

**Usage**: Sends serial data (responses, echo) to host.

---

## HID Reports

### Keyboard Report

**Report ID**: 1

**Size**: 9 bytes (including report ID)

**Format**:

| Byte | Field | Description |
|------|-------|-------------|
| 0 | Report ID | 0x01 |
| 1 | Modifiers | Modifier key bitmask |
| 2 | Reserved | OEM use (0x00 or 0x01) |
| 3 | Key 1 | First key code |
| 4 | Key 2 | Second key code |
| 5 | Key 3 | Third key code |
| 6 | Key 4 | Fourth key code |
| 7 | Key 5 | Fifth key code |
| 8 | Key 6 | Sixth key code |

**Modifier Bitmask**:

| Bit | Key | Value |
|-----|-----|-------|
| 0 | Left Control | 0x01 |
| 1 | Left Shift | 0x02 |
| 2 | Left Alt | 0x04 |
| 3 | Left GUI | 0x08 |
| 4 | Right Control | 0x10 |
| 5 | Right Shift | 0x20 |
| 6 | Right Alt | 0x40 |
| 7 | Right GUI | 0x80 |

**Example - Type 'A' (Shift + a)**:

```
01 02 00 04 00 00 00 00 00
│  │  │  │
│  │  │  └── Key code 0x04 = 'a'
│  │  └───── Reserved
│  └──────── Modifier 0x02 = Left Shift
└─────────── Report ID 1 = Keyboard
```

### Mouse Report

**Report ID**: 2

**Size**: 5 bytes (including report ID)

**Format**:

| Byte | Field | Description |
|------|-------|-------------|
| 0 | Report ID | 0x02 |
| 1 | Buttons | Button state bitmask |
| 2 | X | Relative X movement (-127 to +127) |
| 3 | Y | Relative Y movement (-127 to +127) |
| 4 | Wheel | Scroll wheel (-127 to +127) |

**Button Bitmask**:

| Bit | Button | Value |
|-----|--------|-------|
| 0 | Left | 0x01 |
| 1 | Right | 0x02 |
| 2 | Middle | 0x04 |

**Movement Values**:
- Positive X: Move right
- Negative X: Move left
- Positive Y: Move down
- Negative Y: Move up
- Positive Wheel: Scroll up
- Negative Wheel: Scroll down

**Example - Move right 10 pixels**:

```
02 00 0A 00 00
│  │  │  │  │
│  │  │  │  └── Wheel = 0
│  │  │  └───── Y = 0
│  │  └──────── X = 10 (move right)
│  └─────────── Buttons = 0 (none)
└────────────── Report ID 2 = Mouse
```

---

## CDC ACM Protocol

### Control Requests

| Request | Code | Description | Action |
|---------|------|-------------|--------|
| SET_LINE_CODING | 0x20 | Set baud rate, parity, etc. | Accepted but ignored |
| SET_CONTROL_LINE_STATE | 0x22 | DTR/RTS assertion | Responds with DCD/DSR |

### Line Coding Structure

```c
struct usb_cdc_line_coding {
    uint32_t dwDTERate;    // Baud rate
    uint8_t  bCharFormat;  // Stop bits
    uint8_t  bParityType;  // Parity
    uint8_t  bDataBits;    // Data bits
};
```

**Note**: These values are accepted but ignored since this is a virtual serial port with no physical UART.

### Serial State Notification

Sent to inform host of modem signal changes:

```
bmRequestType : 0xA1
bNotification : 0x20 (SERIAL_STATE)
wValue        : 0x0000
wIndex        : Interface number
wLength       : 2
Data          : Modem state bitmask
```

**Modem State Bits**:

| Bit | Signal |
|-----|--------|
| 0 | DCD (Data Carrier Detect) |
| 1 | DSR (Data Set Ready) |

---

## Enumeration Sequence

1. **Device Reset** - Host resets device
2. **Get Device Descriptor** - Host reads device info
3. **Set Address** - Host assigns USB address
4. **Get Configuration Descriptor** - Host reads full config
5. **Get String Descriptors** - Host reads strings (manufacturer, product, etc.)
6. **Get HID Report Descriptor** - Host reads HID format
7. **Set Configuration** - Host activates configuration 1
8. **Device Ready** - All interfaces active

### Post-Enumeration

After enumeration:
1. `hid_set_config()` is called - sets up HID endpoint
2. `cdcacm_set_config()` is called - sets up CDC endpoints
3. DCD/DSR notification sent - signals serial port ready
4. Device begins execution (if payload exists)

---

## Debugging USB

### Linux

```bash
# View device info
lsusb -v -d 05ac:2227

# Monitor USB traffic
sudo modprobe usbmon
sudo cat /sys/kernel/debug/usb/usbmon/0u

# View HID reports
sudo cat /dev/hidraw0 | xxd
```

### Windows

- Use USBlyzer or Wireshark with USBPcap
- Device Manager shows HID and COM port

### macOS

- Use USB Prober (part of Xcode tools)
- System Information > USB shows device tree
