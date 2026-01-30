# JavaScript API Reference

The `hid.js` module provides utilities for encoding and decoding USB HID reports compatible with the Pill Duck device.

## Table of Contents

- [Installation](#installation)
- [Quick Start](#quick-start)
- [API Reference](#api-reference)
  - [decode()](#decode)
  - [encode()](#encode)
  - [splitReports()](#splitreports)
  - [decodeAll()](#decodeall)
- [Constants](#constants)
- [Report Formats](#report-formats)
- [Examples](#examples)

---

## Installation

### Node.js

```javascript
const hid = require('./js/hid.js');
```

### Browser (with bundler)

```javascript
import { decode, encode, decodeAll } from './js/hid.js';
```

---

## Quick Start

```javascript
const hid = require('./hid.js');

// Decode a keyboard report
const buf = Buffer.from([
  0x01,                         // Report ID: Keyboard
  0x02,                         // Modifiers: Left Shift
  0x00,                         // Reserved
  0x04, 0x00, 0x00, 0x00, 0x00, 0x00,  // Keys: 'a'
  0x00,                         // LEDs
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00   // Padding
]);

const report = hid.decode(buf);
console.log(report);
// {
//   report_id: 1,
//   modifiers: 2,
//   reserved: 0,
//   keys_down: [4, 0, 0, 0, 0, 0],
//   leds: 0
// }

// Encode a mouse report
const mouseReport = hid.encode({
  report_id: 2,
  buttons: 0,
  x: 10,
  y: -5,
  wheel: 0
});
```

---

## API Reference

### decode()

Decodes a single USB HID report from a Buffer.

```javascript
function decode(buf: Buffer): Object
```

**Parameters**:

| Parameter | Type | Description |
|-----------|------|-------------|
| `buf` | `Buffer` | 16-byte buffer containing HID report |

**Returns**: Object with decoded report fields

**Return Object (Keyboard)**:

| Property | Type | Description |
|----------|------|-------------|
| `report_id` | `number` | Always `1` for keyboard |
| `modifiers` | `number` | Modifier key bitmask |
| `reserved` | `number` | Reserved byte |
| `keys_down` | `number[]` | Array of 6 key codes |
| `leds` | `number` | LED indicator bitmask |

**Return Object (Mouse)**:

| Property | Type | Description |
|----------|------|-------------|
| `report_id` | `number` | Always `2` for mouse |
| `buttons` | `number` | Button state bitmask |
| `x` | `number` | X-axis movement (0-255) |
| `y` | `number` | Y-axis movement (0-255) |
| `wheel` | `number` | Scroll wheel movement |

**Example**:

```javascript
// Decode keyboard report for Ctrl+C
const buf = Buffer.from([
  0x01,       // Keyboard report
  0x01,       // Left Ctrl
  0x00,       // Reserved
  0x06,       // 'c' key
  0x00, 0x00, 0x00, 0x00, 0x00,
  0x00,       // LEDs
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00
]);

const report = hid.decode(buf);
// report.modifiers === 1 (Left Ctrl)
// report.keys_down[0] === 6 ('c')
```

---

### encode()

Encodes a report object to a 16-byte Buffer.

```javascript
function encode(result: Object): Buffer
```

**Parameters**:

| Parameter | Type | Description |
|-----------|------|-------------|
| `result` | `Object` | Report object to encode |

**Input Object (Keyboard)**:

| Property | Type | Required | Description |
|----------|------|----------|-------------|
| `report_id` | `number` | Yes | Must be `1` |
| `modifiers` | `number` | Yes | Modifier bitmask |
| `reserved` | `number` | Yes | Reserved byte |
| `keys_down` | `number[]` | Yes | Array of 6 key codes |
| `leds` | `number` | Yes | LED bitmask |

**Input Object (Mouse)**:

| Property | Type | Required | Description |
|----------|------|----------|-------------|
| `report_id` | `number` | Yes | Must be `2` |
| `buttons` | `number` | Yes | Button bitmask |
| `x` | `number` | Yes | X movement |
| `y` | `number` | Yes | Y movement |
| `wheel` | `number` | Yes | Scroll movement |

**Returns**: 16-byte Buffer

**Example**:

```javascript
// Create a mouse click report
const buf = hid.encode({
  report_id: 2,
  buttons: 1,  // Left button pressed
  x: 0,
  y: 0,
  wheel: 0
});

// buf is a 16-byte Buffer: [0x02, 0x01, 0x00, 0x00, 0x00, ...]
```

---

### splitReports()

Splits a concatenated buffer into individual 16-byte report buffers.

```javascript
function splitReports(bufs: Buffer): Buffer[]
```

**Parameters**:

| Parameter | Type | Description |
|-----------|------|-------------|
| `bufs` | `Buffer` | Concatenated 16-byte reports |

**Returns**: Array of 16-byte Buffers

**Example**:

```javascript
// Split a 32-byte buffer into two reports
const combined = Buffer.alloc(32);
combined[0] = 0x01;   // First report: keyboard
combined[16] = 0x02;  // Second report: mouse

const reports = hid.splitReports(combined);
// reports.length === 2
// reports[0][0] === 1
// reports[1][0] === 2
```

---

### decodeAll()

Decodes all reports from a concatenated buffer.

```javascript
function decodeAll(buf: Buffer): Object[]
```

**Parameters**:

| Parameter | Type | Description |
|-----------|------|-------------|
| `buf` | `Buffer` | Concatenated 16-byte reports |

**Returns**: Array of decoded report objects

**Example**:

```javascript
const fs = require('fs');

// Read a payload dump and decode all reports
const flashData = fs.readFileSync('payload.bin');
const reports = hid.decodeAll(flashData);

reports.forEach((report, i) => {
  if (report.report_id === 1) {
    console.log(`${i}: Keyboard, key=${report.keys_down[0]}`);
  } else if (report.report_id === 2) {
    console.log(`${i}: Mouse, x=${report.x}, y=${report.y}`);
  }
});
```

---

## Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `REPORT_ID_KEYBOARD` | 1 | Keyboard report type |
| `REPORT_ID_MOUSE` | 2 | Mouse report type |

---

## Report Formats

### Keyboard Report (16 bytes)

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0 | 1 | `report_id` | Always `1` |
| 1 | 1 | `modifiers` | Modifier key bitmask |
| 2 | 1 | `reserved` | Reserved (OEM use) |
| 3-8 | 6 | `keys_down` | Up to 6 simultaneous keys |
| 9 | 1 | `leds` | LED indicators |
| 10-15 | 6 | (padding) | Unused |

**Modifier Bitmask**:

| Bit | Key |
|-----|-----|
| 0 | Left Control |
| 1 | Left Shift |
| 2 | Left Alt |
| 3 | Left GUI |
| 4 | Right Control |
| 5 | Right Shift |
| 6 | Right Alt |
| 7 | Right GUI |

### Mouse Report (16 bytes)

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0 | 1 | `report_id` | Always `2` |
| 1 | 1 | `buttons` | Button state bitmask |
| 2 | 1 | `x` | X movement (-127 to +127) |
| 3 | 1 | `y` | Y movement (-127 to +127) |
| 4 | 1 | `wheel` | Scroll (-127 to +127) |
| 5-15 | 11 | (padding) | Unused |

**Button Bitmask**:

| Bit | Button |
|-----|--------|
| 0 | Left |
| 1 | Right |
| 2 | Middle |

---

## Examples

### Type "Hello"

```javascript
const hid = require('./hid.js');

// USB HID key codes for "Hello"
const H = 0x0B;  // 'h'
const E = 0x08;  // 'e'
const L = 0x0F;  // 'l'
const O = 0x12;  // 'o'

const SHIFT = 0x02;  // Left Shift modifier

function makeKeyPress(key, modifiers = 0) {
  return hid.encode({
    report_id: 1,
    modifiers: modifiers,
    reserved: 0,
    keys_down: [key, 0, 0, 0, 0, 0],
    leds: 0
  });
}

function makeKeyRelease() {
  return hid.encode({
    report_id: 1,
    modifiers: 0,
    reserved: 0,
    keys_down: [0, 0, 0, 0, 0, 0],
    leds: 0
  });
}

// Generate reports for "Hello"
const reports = [
  makeKeyPress(H, SHIFT),  // 'H' (Shift + h)
  makeKeyRelease(),
  makeKeyPress(E),         // 'e'
  makeKeyRelease(),
  makeKeyPress(L),         // 'l'
  makeKeyRelease(),
  makeKeyPress(L),         // 'l'
  makeKeyRelease(),
  makeKeyPress(O),         // 'o'
  makeKeyRelease()
];

// Combine into single buffer
const payload = Buffer.concat(reports);
console.log(payload.toString('hex'));
```

### Mouse Square

```javascript
const hid = require('./hid.js');

function makeMouseMove(x, y) {
  return hid.encode({
    report_id: 2,
    buttons: 0,
    x: x,
    y: y,
    wheel: 0
  });
}

// Generate reports to move mouse in a square
const size = 50;
const reports = [];

// Move right
for (let i = 0; i < size; i++) {
  reports.push(makeMouseMove(1, 0));
}
// Move down
for (let i = 0; i < size; i++) {
  reports.push(makeMouseMove(0, 1));
}
// Move left
for (let i = 0; i < size; i++) {
  reports.push(makeMouseMove(-1, 0));
}
// Move up
for (let i = 0; i < size; i++) {
  reports.push(makeMouseMove(0, -1));
}

const payload = Buffer.concat(reports);
```

### Analyze Flash Dump

```javascript
const hid = require('./hid.js');
const fs = require('fs');

// Read flash dump
const data = fs.readFileSync('flash_dump.bin');
const reports = hid.decodeAll(data);

// Analyze payload
let keyCount = 0;
let mouseCount = 0;
let delays = 0;

reports.forEach(report => {
  switch (report.report_id) {
    case 1:
      keyCount++;
      break;
    case 2:
      mouseCount++;
      break;
    case 254:
      delays++;
      break;
    case 255:
      console.log('End of payload reached');
      break;
  }
});

console.log(`Keyboard reports: ${keyCount}`);
console.log(`Mouse reports: ${mouseCount}`);
console.log(`Delays: ${delays}`);
```

---

## USB HID Key Codes Reference

Common key codes used in keyboard reports:

| Key | Code | Key | Code |
|-----|------|-----|------|
| a-z | 0x04-0x1D | 1-9 | 0x1E-0x26 |
| 0 | 0x27 | Enter | 0x28 |
| Escape | 0x29 | Backspace | 0x2A |
| Tab | 0x2B | Space | 0x2C |
| - | 0x2D | = | 0x2E |
| [ | 0x2F | ] | 0x30 |
| \\ | 0x31 | ; | 0x33 |
| ' | 0x34 | ` | 0x35 |
| , | 0x36 | . | 0x37 |
| / | 0x38 | Caps Lock | 0x39 |
| F1-F12 | 0x3A-0x45 | Print Screen | 0x46 |

For a complete list, see the [USB HID Usage Tables](https://usb.org/sites/default/files/hut1_22.pdf).
