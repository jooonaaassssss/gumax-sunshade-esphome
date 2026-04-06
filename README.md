# gumax-sunshade-esphome

Control AOK-E AM68 / Gumax-compatible RS485 tubular awning motors from [ESPHome](https://esphome.io) and Home Assistant — no proprietary hub required.

> ⚠️ **SAFETY WARNING — READ BEFORE PROCEEDING**
>
> Tubular awning motors are mains-powered (typically 230 V AC). **Always disconnect mains power before opening the motor end cap, accessing the PCB, or soldering wires.** Failure to do so can result in **electric shock, serious injury, or death**. The low-voltage RS485 and signal wires are not isolated from the mains supply inside the motor housing. Proceed at your own risk.

> **Compatibility notice** — the RS485 protocol documented here was discovered by observing bus traffic on Gumax sunshade motors. These motors are electrically and protocol-identical to the AOK-E AM68 series. This project is not affiliated with, endorsed by, or sponsored by Gumax or AOK.

---

## What you get

- Individual open/close/stop and **position control** (0–100 %) for up to 8 motors
- Real-time **position feedback** read from the motor's own status frames
- Per-motor **online / offline detection** in Home Assistant
- **All-awnings broadcast** cover — open/close/stop/position all motors with one command
- **RS485 Raw Command** text entity for ad-hoc hex frame injection (useful for debugging or one-off firmware commands)
- Status-query buttons for individual motors and broadcast
- Single ESPHome YAML — no custom components, no extra libraries

---

## Compatible hardware

These motors share the same RS485 protocol:

| Brand | Model |
|---|---|
| AOK-E | AM68 (and AM68S) |
| Gumax | Sunshade tubular motors |

The Gumax motors are physically identical to AOK-E AM68. The only difference is that the two RJ12 service ports are hidden behind the rear cover and require to drill holes in the rear cover to access (see wiring section).

---

## Prerequisites

**Hardware**
- ESP32 development board (e.g. ESP32-DevKitC), or any ESP32/ESP8266 with a spare UART
- TTL-to-RS485 converter module. Use one with **automatic TX/RX direction switching** (these are labeled "auto", "flow control", or have a hardware DE/RE jumper). A bare MAX485 module requires a separate GPIO to drive DE/RE, which is not shown here.
- RJ12 (6P6C) patch cables and a crimping tool, or pre-made cables

**Software**
- [ESPHome](https://esphome.io/guides/installing_esphome) ≥ 2024.x
- Home Assistant (optional but recommended for the cover UI)

**Estimated parts cost**: < €30 for ESP32 + converter + cables

---

## Hardware wiring

### 1. Accessing the RJ12 ports (Gumax motors only)

Standard AOK-E AM68 motors expose two RJ12 sockets on the end cap. Gumax motors have a plastic rear cover over the same connectors. Drill two 10 mm holes through the cover to reach the RJ12 sockets.

### 2. RJ12 pin-out

Each motor has two identical RJ12 sockets (daisy-chain in / out). Pin numbering from left to right with the tab facing down:

| Pin | Signal   | Notes |
|-----|----------|-------|
| 1   | RS485-A  | Differential data A (non-inverting) |
| 2   | DOWN     | Hardwired control input (not used here) |
| 3   | STOP     | Hardwired control input (not used here) |
| 4   | UP       | Hardwired control input (not used here) |
| 5   | COM/GND  | Signal ground — also connect to ESP32 GND |
| 6   | RS485-B  | Differential data B (inverting) |

> **5 V power tap**: On the motor PCB itself (barely visible near the end cap) there is an unpopulated 5 V test point. You can solder a thin wire here to power the ESP32 directly from the motor's internal supply, avoiding a separate power source. Confirm voltage before connecting; the ESP32's 5 V pin and the VIN of the converter module should each draw < 200 mA combined.

### 3. Daisy-chaining motors

Connect motors in a chain using standard RJ12 straight-through cables:

```
Motor 1 ──RJ12── Motor 2 ──RJ12── Motor 3 ──RJ12── … ──RJ12── Motor N
    └─── RS485-A/B/GND wires to ESP32 RS485 converter
```

Connect the converter to either the first or last motor in the chain. Terminate long runs (> 10 m) with a 120 Ω resistor across A and B. (Some TTL converters already have that resistor soldered on)

### 4. RS485 converter to ESP32 wiring

| Converter pin | ESP32 pin | Notes |
|---|---|---|
| A (or D+) | — | Connect to motor RS485-A |
| B (or D−) | — | Connect to motor RS485-B |
| GND | GND | Must share ground with motors |
| RO (RX out) | GPIO16 (RX) | Crossed — converter RO → ESP32 RX |
| DI (TX in) | GPIO17 (TX) | Crossed — converter DI ← ESP32 TX |
| VCC | 3.3 V or 5 V | Check converter datasheet |

> **DE/RE**: If your converter exposes separate DE and RE pins, check whether they are already tied together on the board. For auto-direction modules they are internally controlled. For manual modules, tie DE+RE together and connect to a GPIO, then add `direction_pin:` to the `uart:` block in the YAML.

GPIO assignment can be changed in the `substitutions:` block.

---

## Addressing motors (programming IDs and channels)

Each motor must be programmed with a unique **ID** (0x01–0xFF in hex) and a **channel** (bitmask). Do this before connecting multiple motors to the bus — only one motor should be connected at a time during programming.

**Programming frame:**

```
9A  ID  CH_L  CH_H  AA  AA  VFY
```

| Byte | Meaning |
|------|---------|
| `9A` | Frame header (always) |
| `ID` | Target motor ID in hex (e.g. `01` for motor 1, `0A` for motor 10) |
| `CH_L` | Channel low byte (see table below) |
| `CH_H` | Channel high byte (usually `00` for channels 1–8) |
| `AA AA` | Programming command bytes (fixed) |
| `VFY` | `ID ^ CH_L ^ CH_H ^ 0xAA ^ 0xAA` = `ID ^ CH_L ^ CH_H` |

**Channel bitmask table:**

| Channel | CH_L | CH_H |
|---------|------|------|
| 1 | `0x01` | `0x00` |
| 2 | `0x02` | `0x00` |
| 3 | `0x04` | `0x00` |
| 4 | `0x08` | `0x00` |
| 5 | `0x10` | `0x00` |
| 6 | `0x20` | `0x00` |
| 7 | `0x40` | `0x00` |
| 8 | `0x80` | `0x00` |
| 9 | `0x00` | `0x01` |
| 16 | `0x00` | `0x80` |

**Example — assign Motor 1, Channel 1:**
```
9A 01 01 00 AA AA 00
          (VFY = 01 ^ 01 ^ 00 = 00)
```

**Worked walk-through for an 8-motor pergola installation:**
1. Daisychain all motors together
2. Start with the motor furthes away from your ESP. Only have power connected to the closest Motor to the ESP (So the esp gets its 5V supply) and the one that you are starting to pair with.
2. Use the **RS485 Raw Command** entity in Home Assistant and send: `9a 01 01 00 aa aa` (ID=1, Channel=1)
   (verify byte is appended automatically).
3. The motor acknowledges by blinking blue tree times. Disconnect power, connect motor 2. Keep the Motor powering the esp connected
4. Send `9a 02 01 00 aa aa` (ID=2, same channel 1). Repeat for motors 3 and 4.
5. For a second group on channel 2, send `9a 05 02 00 aa aa` through `9a 08 02 00 aa aa`.
6. Now program the motor powering your ESP to ensure it also has a correct address (it should have jumped through ids along with the other motors while programming).
7. Connect all motors to power and ensure all of them are controllable individually.

> **IDs are hex**: motor 9 is `0x09`, motor 10 is `0x0A`, not `10`. For 8-motor setups (IDs 1–8) this distinction doesn't matter.

> **Channels are for addressing only**: the channel bytes in a broadcast frame (ID=0x00) are ignored — all motors respond regardless of their programmed channel. Channels serve purely as an identifier during the ID-programming step and for per-motor individual commands.

---

## RS485 protocol reference

All multi-byte values are big-endian. Baud rate: **2400 8N1**.

### Command frame (ESP32 → motor, 7 bytes)

| Byte | Value | Meaning |
|------|-------|---------|
| 0 | `0x9A` | Frame header |
| 1 | `ID` | Motor ID (0x00 = group broadcast — channel bytes are ignored for broadcast) |
| 2 | `CH_L` | Channel low byte |
| 3 | `CH_H` | Channel high byte |
| 4 | `Cmd1` | `0x0A` = motion command, `0xDD` = position command |
| 5 | `Cmd2` | See table below |
| 6 | `VFY` | `ID ^ CH_L ^ CH_H ^ Cmd1 ^ Cmd2` |

**Cmd2 values:**

| Cmd1 | Cmd2 | Action |
|------|------|--------|
| `0x0A` | `0xDD` | Move UP (open) |
| `0x0A` | `0xCC` | STOP |
| `0x0A` | `0xEE` | Move DOWN (close) |
| `0xCC` | `0x00` | Status query (motor replies with a response frame) |
| `0xDD` | `0x00`–`0x64` | Go to absolute position (0=open, 100=closed) |

### Response frame (motor → ESP32, 10 bytes)

The motor sends this frame **twice** in quick succession after each status query. The firmware handles occasional byte loss at the frame boundary.

| Byte | Meaning |
|------|---------|
| 0 | `0xD8` — response header |
| 1 | Motor ID |
| 2–6 | Motor-specific data (voltage, speed, …) |
| 7 | Position: 0–100 |
| 8 | Zone flags: bit 0 = motor running |
| 9 | Checksum: XOR of bytes 1–8 |

### Position conventions

The motor's raw position value runs in opposite directions depending on how the motor was physically installed:

| Group | Raw 0 means | Raw 100 means | ESPHome `position` |
|-------|-------------|---------------|-------------------|
| All motors | Open | Closed | `1 – (raw / 100)` |

ESPHome convention: `0.0 = closed`, `1.0 = open`.

---

## ESPHome configuration

### File structure

```
esphome/
├── awning-motor.yaml          # main file — edit substitutions + packages here
├── packages/
│   ├── motor.yaml             # template: global + binary_sensor + cover per motor
│   └── group.yaml             # template: group broadcast cover
└── secrets.yaml               # copy from secrets.yaml.example and fill in
```

### Quick start

```bash
cp esphome/secrets.yaml.example esphome/secrets.yaml
# Edit secrets.yaml with your Wi-Fi and API credentials
esphome run esphome/awning-motor.yaml
```

### Step 1 — Substitutions

Edit the top of `awning-motor.yaml`:

```yaml
substitutions:
  device_name: awning-motor        # ESPHome device hostname
  friendly_name: Awning Motor Controller

  uart_rx_pin: GPIO16              # Connect to converter RO
  uart_tx_pin: GPIO17              # Connect to converter DI

  poll_interval: 30s               # How often to poll all motors
  offline_timeout_ms: "90000"      # Mark offline after 3 missed cycles

  group1_channel_l: "0x01"        # ChanL byte for group 1 motors
  group1_channel_h: "0x00"
  group2_channel_l: "0x02"        # ChanL byte for group 2 motors
  group2_channel_h: "0x00"
```

### Step 2 — Motor package includes

Each motor is declared as one `packages:` entry. Copy/remove blocks to match your motor count:

```yaml
packages:
  motor_1:
    !include
      file: packages/motor.yaml
      vars:
        motor_id: "1"            # Hardware motor ID (1–255)
        motor_name: "Awning 1"   # Name shown in Home Assistant
        channel_l: ${group1_channel_l}
        channel_h: ${group1_channel_h}
        offline_timeout_ms: ${offline_timeout_ms}
```

Each entry generates: one `motor_last_seen_N` global, one `awning_N_online` binary sensor, and one `awning_N` cover.

### Step 3 — All-awnings broadcast cover

A single `group_all` package entry is included by default and covers all motors at once:

```yaml
  group_all:
    !include
      file: packages/group.yaml
      vars:
        group_name: "All Awnings"
        group_id: awning_all
        channel_l: "0x00"
        channel_h: "0x00"
```

This sends a broadcast frame with ID=0x00, ChanL=0x00, ChanH=0x00, which all motors respond to. Channel-filtered broadcast (ID=0 with non-zero channel bytes) **does not work** — motors respond to any ID=0 broadcast regardless of their programmed channel.

### Scaling to fewer or more motors

To **remove** motors 7 and 8: delete the `motor_7` and `motor_8` package blocks from the `packages:` section. That's it — the poll query, availability check, and RX dispatch branch are all removed automatically.

To **add** a 9th motor: add a `motor_9:` package block (copy any existing entry, set `motor_id: "9"` and the correct `channel_l`/`channel_h`). The poll query, availability check, and RX dispatch branch are added automatically via `!extend`.

All `motor_id` values must match the ID programmed into the physical motor hardware (see [Addressing motors](#addressing-motors)).

---

## Legal & disclaimer

**Electrical safety**: Tubular awning motors operate at mains voltage (typically 230 V AC). Working on or near live mains wiring can cause **electric shock, fire, or death**. Always isolate mains power before accessing the motor internals, the PCB, or the RJ12 connectors. The author is not responsible for any injury, death, property damage, or fire resulting from the use of this project. **If you are not qualified to work with mains voltages, consult a licensed electrician.**

**No warranty / no liability**: This project is provided as-is, with no warranty of any kind, express or implied. The author accepts no responsibility or liability for any damage to motors, ESP32 boards, associated hardware, property, or any other loss arising from the use, misuse, or inability to use this project. **Use entirely at your own risk.**

**Reverse engineering**: The RS485 protocol documented here was derived by observing bus traffic on hardware already owned by the author. This is permitted for interoperability purposes under:
- EU Software Directive 2009/24/EC, Article 6 (decompilation for interoperability)
- US Digital Millennium Copyright Act, 17 U.S.C. §1201(f)

**Trademarks**: "Gumax" and "AOK" are trademarks of their respective owners. Use of these names here is purely for compatibility identification (nominative fair use) and does not imply any affiliation with or endorsement by those companies.

---

## Credits

- **Protocol reference**: [Loxone Library — Electric Curtain Drive A-OK](https://library.loxone.com/detail/electric-curtain-drive-a-ok-1922/overview) — community documentation of the AOK-E AM68 RS485 protocol that helped confirm the frame structure.
- **ESPHome** — open-source firmware framework ([Apache 2.0](https://github.com/esphome/esphome/blob/dev/LICENSE)). All UART, cover, and template components used here are part of the mainline ESPHome project.
- **uartex** by [eigger](https://github.com/eigger) — ESPHome external component that provides declarative RS485 frame parsing and command dispatch ([MIT License](https://github.com/eigger/espcomponents/blob/master/LICENSE), [source](https://github.com/eigger/espcomponents)).
