# Wiring Guide

## Components

- ESP32 development board
- MAX485 RS485-to-TTL module
- 6 jumper wires (female-to-female recommended)
- 2× 120Ω resistors (1/4W)
- RS485 cable (twisted pair)

## Wiring Diagram

```
┌─────────────┐         ┌──────────────┐         ┌──────────────┐
│    ESP32    │         │   MAX485     │         │ Modbus Device│
│             │         │   Module     │         │   (Slave)    │
│         3.3V├────────►│VCC           │         │              │
│          GND├────────►│GND           │         │              │
│      GPIO 17├────────►│DI   (TX in)  │         │              │
│      GPIO 16│◄────────┤RO   (RX out) │         │              │
│       GPIO 4├────┬───►│DE   (TX en)  │         │              │
│             │    └───►│RE   (RX en)  │         │              │
│             │         │              │         │              │
│             │         │A (+) ────────┼─────────┤A or Data+    │
│             │         │B (-) ────────┼─────────┤B or Data-    │
└─────────────┘         └──────────────┘         └──────────────┘
                              │    │                    │    │
                              │    │                   [120Ω]
                             [120Ω]                 Termination
                         Termination              (at last device)
                     (if first device)
```

## Step-by-Step Connection

### 1. Power Connections

| ESP32 | MAX485 | Notes |
|-------|--------|-------|
| **3.3V** | VCC | Some MAX485 modules accept 5V too |
| **GND** | GND | Common ground is critical |

⚠️ **Important:** Use 3.3V if your MAX485 module is marked as 3.3V. If it says "3.3V-5V", either works.

### 2. Data Connections

| ESP32 Pin | MAX485 Pin | Direction | Description |
|-----------|------------|-----------|-------------|
| **GPIO17** | DI | ESP32 → MAX485 | Transmit data |
| **GPIO16** | RO | MAX485 → ESP32 | Receive data |

💡 **Tip:** If communication doesn't work, try swapping TX/RX

### 3. Control Signals

| ESP32 Pin | MAX485 Pins | Description |
|-----------|-------------|-------------|
| **GPIO4** | DE **and** RE | Direction control (tie together) |

⚠️ **Critical:** DE and RE pins must be connected **together** to the same GPIO4

### 3b. Standard RS485 Module (without DE/RE pins)

If you're using a standard RS485 module (not MAX485), the wiring is **simplified**:

```
┌─────────────┐         ┌──────────────┐         ┌──────────────┐
│    ESP32    │         │ Standard     │         │ Modbus Device│
│             │         │ RS485        │         │   (Slave)    │
│         3.3V├────────►│VCC           │         │              │
│          GND├────────►│GND           │         │              │
│      GPIO 17├────────►│DI   (TX in)  │         │              │
│      GPIO 16│◄────────┤RO   (RX out) │         │              │
│             │         │              │         │              │
│             │         │A (+) ────────┼─────────┤A or Data+    │
│             │         │B (-) ────────┼─────────┤B or Data-    │
└─────────────┘         └──────────────┘         └──────────────┘
                              │    │                    │    │
                              │    │                   [120Ω]
                             [120Ω]                 Termination
                         Termination              (at last device)
```

**Connection table for standard RS485:**

| ESP32 Pin | RS485 Pin | Direction | Description |
|-----------|-----------|-----------|-------------|
| **3.3V** | VCC | ESP32 → RS485 | Power supply |
| **GND** | GND | ESP32 → RS485 | Common ground |
| **GPIO17** | DI | ESP32 → RS485 | Transmit data |
| **GPIO16** | RO | RS485 → ESP32 | Receive data |
| *(unused)* | DE/RE | - | Not used with standard modules |

⚠️ **Note:** Standard RS485 modules don't have DE/RE control pins. They automatically handle direction switching.

**Code configuration:**

In `esp32-modbus-gateway.ino`, set:
```cpp
#define MODBUS_DE_PIN     -1  // Standard RS485 Module (no DE/RE control)
```

Instead of:
```cpp
#define MODBUS_DE_PIN     4   // MAX485 Module with DE/RE control on GPIO4
```

### 4. RS485 Bus Connections

Connect all devices in **parallel** to the A and B lines:

```
MAX485 (Gateway)   Device 1        Device 2        Device N
    │                │                │                │
    A ───────────────┼────────────────┼────────────────┤ A/+
    B ───────────────┼────────────────┼────────────────┤ B/-
    │                │                │                │
  [120Ω]             │                │              [120Ω]
```

**A/B Connections:**
- **A** = Data+ = usually marked with "+" or "A"
- **B** = Data- = usually marked with "-" or "B"

💡 **If unsure about polarity:** Try both ways. Wrong polarity won't damage anything, just won't communicate.

## Termination Resistors

**Why needed:** Prevents signal reflections on long cables

**Where to install:**
- At **both ends** of the RS485 bus
- Between A and B lines
- Value: **120Ω** (±10%)

**Installation:**

```
First device (Gateway):          Last device:
┌──────┐                        ┌──────┐
│  A ──┴──[120Ω]──┬── B        │  A ──┴──[120Ω]──┬── B
└──────┘          │             └──────┘          │
```

⚠️ **Don't add termination to middle devices**, only the two ends!

## Cable Requirements

### Cable Type
- **Twisted pair** (Cat5/Cat6 ethernet cable works great)
- **Shielded** (recommended for noisy environments)
- Use **one pair** from Cat5 (blue/blue-white or orange/orange-white)

### Maximum Distances

| Baud Rate | Max Distance |
|-----------|--------------|
| 9600 | 1200m |
| 19200 | 1000m |
| 38400 | 500m |
| 115200 | 100m |

### Shielding
- Ground shield at **one end only** (prevent ground loops)
- Usually ground at power supply end

## Common Mistakes

### ❌ Wrong: DE and RE separate
```
GPIO4 → DE
GPIO5 → RE    ← Don't do this!
```

### ✅ Correct: DE and RE tied together
```
GPIO4 → DE ┐
        RE ┘  ← Both connected to same pin
```

### ❌ Wrong: Star topology
```
    Gateway
    ├── Device 1
    ├── Device 2
    └── Device 3    ← Don't do this!
```

### ✅ Correct: Daisy chain
```
Gateway ─── Device 1 ─── Device 2 ─── Device 3
```

## Verification

### Visual Check
- [ ] All connections secure
- [ ] No shorts between A and B
- [ ] DE and RE tied together
- [ ] Termination at both ends only
- [ ] Cable polarity consistent

### Multimeter Check

**With power OFF:**
1. Check A-B resistance: Should be ~60Ω (two 120Ω in parallel)
2. Check A-GND: Should be >1MΩ (no short)
3. Check B-GND: Should be >1MΩ (no short)

**With power ON:**
1. ESP32 3.3V pin: Should measure 3.2-3.4V
2. MAX485 VCC: Should match ESP32 (3.3V)

### Oscilloscope Check (Advanced)

If available, check with oscilloscope:
- A-B differential: Should show ±200mV to ±5V when transmitting
- Clean square waves during transmission
- Idle state: A higher than B (typically A=3V, B=0V)

## Troubleshooting

### No Communication

**Try:**
1. Swap A and B connections
2. Check termination resistors
3. Verify correct baud rate
4. Check slave device address
5. Measure A-B voltage (should be non-zero at idle)

### Intermittent Communication

**Check:**
- Poor wire connections (re-seat)
- Missing termination
- Cable too long
- Electrical noise (add ferrite beads)

### CRC Errors

**Likely causes:**
- Wrong baud rate
- Missing/incorrect termination
- Poor quality cable
- Electromagnetic interference

## Example Installations

### Minimal Test Setup (1 Device)
```
ESP32 ─── MAX485 ─── (1m cable) ─── Test Device
         [120Ω]                      [120Ω]
```

### Small Installation (2-5 Devices)
```
ESP32 ─── MAX485 ─── Device1 ─── Device2 ─── Device3
         [120Ω]                              [120Ω]
         <────────── 50m total ──────────────>
```

### Large Installation (10+ Devices)
```
ESP32 ─── MAX485 ─── [... 32 devices max ...] ─── Last Device
         [120Ω]                                    [120Ω]
         <──────────── up to 1200m @ 9600 baud ────────────>
```

## Safety Notes

- ⚡ Keep RS485 cables away from high-voltage AC lines
- 🔌 Use isolated power supplies if devices are far apart
- ⚠️ Never hot-plug RS485 connections (power off first)
- 🛡️ Consider surge protection for outdoor installations

## Next Steps

After wiring:
1. Power up the ESP32
2. Check Serial Monitor for boot messages
3. Test communication via web interface
4. See [API.md](API.md) for automation
