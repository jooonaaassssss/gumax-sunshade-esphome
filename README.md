# gumax-sunshade-esphome

Control AOK-E AM68 / Gumax-compatible RS485 tubular awning motors from [ESPHome](https://esphome.io) and Home Assistant — no proprietary hub required.

This is a modified fork of [s3bi77/gumax-sunshade-esphome](https://github.com/s3bi77/gumax-sunshade-esphome),
configured for a Waveshare ESP32-S3-RS485-CAN and five motors. It is released
under the same licence, the MIT License. See [Fork history](#fork-history),
[Changes in this fork](#changes-in-this-fork) and [Licence](#licence).

> ⚠️ **SAFETY WARNING — READ BEFORE PROCEEDING**
>
> Tubular awning motors are mains-powered (typically 230 V AC). **Always disconnect mains power before opening the motor end cap, accessing the PCB, or soldering wires.** Failure to do so can result in **electric shock, serious injury, or death**. The low-voltage RS485 and signal wires are not isolated from the mains supply inside the motor housing. Proceed at your own risk.

> **Compatibility notice** — the RS485 protocol documented here was discovered by observing bus traffic on Gumax sunshade motors. These motors are electrically and protocol-identical to the AOK-E AM68 series. This project is not affiliated with, endorsed by, or sponsored by Gumax or AOK.

---

## Features

- Individual open/close/stop and **position control** (0–100 %) per motor
- **Live feedback**: position read from the motor's own status replies, polled every 15 s and every 3 s while a motor moves; the covers show **opening/closing** until the motor reports idle, also for moves by the remote control, see [How feedback works](#how-feedback-works)
- Replies that lose bytes on the bus are still read where it is safe, see [Response frame](#response-frame-motor--esp32-10-bytes)
- **All-awnings** cover — open/close/stop/position for all motors with one command; it shows the average position and the motion of the online motors
- Positions are restored after a reboot; motors that do not answer (e.g. not connected) are polled only every 5 minutes
- **RS485 Raw Command** text entity to send any frame from Home Assistant (motor addressing, debugging); programming frames need Programming Mode
- **Motor programming from the UI**: one `Program` button per motor, guarded by a bus scan and a self-expiring `Programming Mode`, see [Addressing motors](#addressing-motors-programming-ids-and-channels)
- Every RS485 reply is logged at `DEBUG` level, damaged ones included
- **Web interface** built into the firmware (ESPHome `web_server` v3, password-protected) — works without Home Assistant, see [Web interface](#web-interface)
- **Online, voltage and last-seen sensors per motor**, an **RS485 error** sensor, a **fallback access point** with captive portal, and diagnostic entities (WiFi signal, uptime, IP, ESPHome version, restart / safe mode)
- Plain ESPHome YAML on top of the [uartex](https://github.com/eigger/espcomponents) external component, with the bus logic in three small C++ headers that are unit-tested in CI
- [tools/esp_probe.py](tools/esp_probe.py) — send frames and watch the bus from a PC, without Home Assistant

---

## Compatible hardware

These motors share the same RS485 protocol:

| Brand | Model |
|---|---|
| AOK-E | AM68 (and AM68S) |
| Gumax | Sunshade tubular motors |

The Gumax motors are physically identical to AOK-E AM68. The only difference is that the two RJ12 service ports are hidden behind the rear cover and require to drill holes in the rear cover to access (see wiring section).

---

## Requirements

**Hardware**
- [Waveshare ESP32-S3-RS485-CAN](https://www.waveshare.com/wiki/ESP32-S3-RS485-CAN) — the board this fork is configured and tested for. Any other ESP32 with an RS485 transceiver works with adjusted pins, see [Other boards](#5-other-boards).
- RJ12 (6P6C) patch cables and a crimping tool, or pre-made cables

**Software**
- [ESPHome](https://esphome.io/guides/installing_esphome) 2026.10 (tested with 2026.10.1). The uartex component is bundled in [esphome/components/uartex](esphome/components/uartex) (patched copy of tag `v260904`, see [uartex](#uartex-bundled-patched-copy)); update ESPHome and uartex together.
- Home Assistant (optional but recommended for the cover UI; it is also a clock for the `Last Seen` sensors, next to SNTP)
- Python 3 with [aioesphomeapi](https://pypi.org/project/aioesphomeapi/), only for [tools/esp_probe.py](tools/esp_probe.py)

---

## Installation

1. Create the secrets file and fill in Wi-Fi credentials, API key, web
   interface login and fallback AP password:
   ```bash
   cp esphome/secrets.yaml.example esphome/secrets.yaml
   ```
   Generate your own `api_key` (`openssl rand -base64 32`); the value in the
   example file is a public placeholder that only exists so CI can validate
   the configuration. The API key also encrypts firmware updates, see
   [Firmware updates (OTA)](#firmware-updates-ota).
2. Adjust the `substitutions:` and `packages:` blocks in [esphome/awning-motor.yaml](esphome/awning-motor.yaml) to your board, motor IDs, names and channels (see [ESPHome configuration](#esphome-configuration)).
3. Flash. The first time over USB-C, afterwards over Wi-Fi (OTA):
   ```bash
   esphome run esphome/awning-motor.yaml
   ```
   With the ESPHome Device Builder in Home Assistant instead, copy `awning-motor.yaml` and the folders `packages/`, `include/` and `components/` to `/config/esphome/` and add the six keys from `secrets.yaml.example` to the Device Builder's own `secrets.yaml`.
4. Add the discovered device in Home Assistant and enter the API key.
5. Give every motor its ID, once per motor: [Addressing motors](#addressing-motors-programming-ids-and-channels).

---

## This installation

The configuration in this repository is the one running on the author's pergola.
It is committed as-is, so it doubles as a worked example. Only the credentials
are kept out of Git (`esphome/secrets.yaml` is ignored).

| Motor | ESPHome id | Motor ID | Channel | ChanL / ChanH |
|---|---|---|---|---|
| 1 | `awning_1` | `0x01` | 1 | `01 00` |
| 2 | `awning_2` | `0x02` | 1 | `01 00` |
| 3 | `awning_3` | `0x03` | 1 | `01 00` |
| 4 | `awning_4` | `0x04` | 1 | `01 00` |
| 5 | `awning_5` | `0x05` | 2 | `02 00` |

Controller: Waveshare ESP32-S3-RS485-CAN, UART RX GPIO18, TX GPIO17, driver
enable GPIO21, hostname `awning-motor`.

All five motors are installed, programmed and answer (as of 2026-10-31;
until 1.4.1 only motors 1 and 2 were connected). Motor 1 was programmed with the RS485 Raw Command frames,
motor 2 kept its ID through a factory reset; new motors are programmed with
their `Program` button. A motor that is factory-reset may forget its ID and
then has to be programmed again.

Motors 1 and 2 are linked by a full 6-wire RJ12 cable on purpose, so each
remote channel moves both (see [Daisy-chaining motors](#3-daisy-chaining-motors)).

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
| 5   | COM/GND  | Signal ground — connect to the converter's GND if it has one (the Waveshare board has none) |
| 6   | RS485-B  | Differential data B (inverting) |

> **5 V power tap**: On the motor PCB itself (barely visible near the end cap) there is an unpopulated 5 V test point. You can solder a thin wire here to power the ESP32 directly from the motor's internal supply, avoiding a separate power source. Confirm voltage before connecting; the ESP32's 5 V pin and the VIN of the converter module should each draw < 200 mA combined.

### 3. Daisy-chaining motors

Connect motors in a chain using RJ12 straight-through cables:

```
Motor 1 ──RJ12── Motor 2 ──RJ12── Motor 3 ──RJ12── … ──RJ12── Motor N
    └─── RS485-A/B (and GND, if the converter has it) to the ESP32 RS485 converter
```

> **Use only pins 1, 5 and 6 between motors.** A full 6-wire cable also links
> the wired inputs DOWN/STOP/UP (pins 2–4). The motors then move together:
> on this installation, with motors 1 and 2 linked by a 6-wire cable, each
> remote channel moved both motors, while each motor alone only followed its
> own channel. No frame appeared on the RS485 bus while this happened, so it
> is the wired inputs, not the bus. Leave pins 2, 3 and 4 unconnected
> (cut those three wires or crimp a cable with A, COM and B only) to keep the
> motors independent. Keep pin 5 (COM): it gives the motors a common ground.

Connect the converter to either the first or last motor in the chain. Terminate long runs (> 10 m) with a 120 Ω resistor across A and B. (Some TTL converters already have that resistor soldered on)

### 4. Waveshare ESP32-S3-RS485-CAN

The board has an isolated RS485 transceiver on board; the UART pins are fixed by
the board and already set in the YAML:

| Signal | ESP32-S3 pin | Substitution |
|---|---|---|
| RX | GPIO18 | `uart_rx_pin` |
| TX | GPIO17 | `uart_tx_pin` |
| Driver enable (DE/RE) | GPIO21 | `uart_flow_control_pin` |

Connect the board's RS485 **A** terminal to motor pin 1 and **B** to motor pin 6.
The board's RS485 terminal has no ground terminal (A and B only), so motor
pin 5 stays unconnected.

- Power the board either from its terminal block or from USB-C, never from both at the same time (Waveshare FAQ).
- The 120R termination jumper is open by default. Bus runs of a few metres
  do not need termination.
- **Idle level (fail-safe bias)**: when no device drives the bus, A should
  sit above B so that the receiver reads 1. Without bias the idle bus
  floats; the receiver then reads `0x00` bytes after every reply and can
  lose bytes that start with several 1 bits (observed here after days of
  error-free operation, see
  [Response frame](#response-frame-motor--esp32-10-bytes)). A 120R
  termination without bias makes this worse. The firmware copes: online
  detection and programming need only the first bytes of a reply, and
  position, motion and voltage are read from damaged replies where that is
  safe (see [How feedback works](#how-feedback-works)).
  Bias resistors (560 Ω–1 kΩ from A to +5 V and from B to GND of the bus
  side) would fix it electrically, but this board has no terminal for the
  bus-side supply.
- Logs are available on the USB-C port (`USB_SERIAL_JTAG`) and over Wi-Fi.

### 5. Other boards

Any ESP32 with a TTL-to-RS485 converter works. Change the `esp32:` block to your
board (remove `flash_chip: generic` under `advanced:`, or set the value the
boot log suggests) and set the pins in `substitutions:`.

| Converter pin | ESP32 pin | Notes |
|---|---|---|
| A (or D+) | — | Connect to motor RS485-A |
| B (or D−) | — | Connect to motor RS485-B |
| GND | GND | Shared ground with the motors (pin 5), if the converter has a GND terminal |
| RO (RX out) | e.g. GPIO16 → `uart_rx_pin` | Converter RO → ESP32 RX |
| DI (TX in) | e.g. GPIO17 → `uart_tx_pin` | Converter DI ← ESP32 TX |
| DE + RE | any GPIO → `uart_flow_control_pin` | Only for converters without automatic direction switching |
| VCC | 3.3 V or 5 V | Check converter datasheet |

> **DE/RE**: Converters labelled "auto" or "flow control" switch direction by themselves; remove the `flow_control_pin:` line from the `uart:` block for those. A bare MAX485 module needs DE and RE tied together and driven by a GPIO through `flow_control_pin`.

---

## Addressing motors (programming IDs and channels)

Each motor must be programmed with a unique **ID** (0x01–0xFF in hex) and a **channel** (bitmask). A motor that has never been programmed, or has been factory-reset, answers as ID `0x00`. Program one motor at a time: **only the motor being programmed may be connected to the bus**, otherwise every connected motor takes the new ID.

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

**From the web interface or Home Assistant** (recommended):

The **Programming** group of the web interface, and the Configuration section of
the device page in Home Assistant, hold a `Program` button per configured motor.
The button programs the ID and channel from that motor's package entry, so
nothing has to be looked up.

1. Switch on **`Programming Mode`**. It switches itself off after 10 minutes
   without use; every Program press restarts the 10 minutes.
2. Connect **only** the motor to be programmed to the bus.
3. Press its button, e.g. **`Awning 3 Program`**. The firmware
   - scans the bus: the broadcast status query and one status query per
     configured motor (a reply lost on one query is caught by another);
     replies count by their first bytes (`D8`, ID, channel), so damaged
     replies still identify the motor,
   - refuses unless exactly one motor answered and no frame was garbled,
   - sends the programming frame (the motor blinks blue three times),
   - queries the new ID and channel to confirm.
4. Read the result in **`Programming Status`**, e.g.
   `Awning 3 programmed: ID 3 / channel 1 confirmed`, or
   `Refused (2 motors: ID 1 / channel 1, ID 2 / channel 1). Connect only the motor to program.`
5. Connect the next motor and continue with step 3.

The scan is a safety net, not a guarantee: two motors with the same ID and
channel, e.g. two factory-reset motors, answer at the same moment and can
look like one. Connecting only the motor to be programmed stays the rule.

**`Scan Bus`** runs the same scan without programming and works without
Programming Mode, e.g. `Scan: 1 motor: ID 0 (unprogrammed)`.

If Program is refused with "no motor answered" although the motor is
connected, press `Scan Bus` and check the log for `RX D8 …` lines; no reply
at all points to wiring, `uart_flow_control_pin` or a motor without power.
As a last resort program it with the raw frames below.

**Procedure with raw frames** (used for [this installation](#this-installation)):

Send the frames below either through the **RS485 Raw Command** entity in Home
Assistant (and watch the ESPHome log) or with [tools/esp_probe.py](tools/esp_probe.py).
The checksum is appended automatically. The programming frame (`aa aa`) is
only sent while **`Programming Mode`** is on; otherwise it is refused (log
warning, `Programming Status`).

1. Switch on `Programming Mode` and connect **only** the motor to be programmed to the bus.
2. Check the bus: send `9a 00 00 00 cc 00`. Every connected motor answers this
   broadcast query with its own ID, so exactly one reply must come back
   (`D8 00 00 00 …` for an unprogrammed motor).
3. Program it, e.g. ID 2 on channel 1: `9a 02 01 00 aa aa`.
4. Verify: `9a 02 01 00 cc 00` must be answered with `D8 02 01 00 …`.
5. Disconnect the motor, connect the next one, continue with step 2.

With esp_probe.py (and `Programming Mode` on), steps 2–4 for motor 2 are:

```bash
python tools/esp_probe.py "9a 00 00 00 cc 00"
python tools/esp_probe.py "9a 02 01 00 aa aa" "9a 02 01 00 cc 00"
```

> **Upstream walk-through** (8 motors, ESP powered from the 5 V tap of the nearest motor): daisy-chain all motors, but power only the motor feeding the ESP and the one being programmed. Program from the far end; the motor acknowledges by blinking blue three times. Program the motor feeding the ESP last — it has received every programming frame along the way.

> **IDs are hex**: motor 9 is `0x09`, motor 10 is `0x0A`, not `10`. For setups with up to 9 motors this distinction doesn't matter.

> **Channels are for addressing only**: the channel bytes in a broadcast frame (ID=0x00) are ignored — all motors respond regardless of their programmed channel. Channels serve purely as an identifier during the ID-programming step and for per-motor individual commands. Broadcasts work for open, close, stop and the status query, not for positions (see [Command frame](#command-frame-esp32--motor-7-bytes)).

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
| 4 | `Cmd1` | `0x0A` = motion, `0xDD` = position, `0xCC` = status query, `0xAA` = programming |
| 5 | `Cmd2` | See table below |
| 6 | `VFY` | `ID ^ CH_L ^ CH_H ^ Cmd1 ^ Cmd2` |

**Cmd2 values:**

| Cmd1 | Cmd2 | Action |
|------|------|--------|
| `0x0A` | `0xDD` | Move UP (open) |
| `0x0A` | `0xCC` | STOP |
| `0x0A` | `0xEE` | Move DOWN (close) |
| `0xCC` | `0x00` | Status query (motor replies with a response frame) |
| `0xDD` | `0x00`–`0x64` | Go to absolute position (0=closed, 100=open). Ignored when sent to ID 0 (broadcast), with or without channel bytes; tested on this installation |
| `0xAA` | `0xAA` | Program ID and channel (see [Addressing motors](#addressing-motors-programming-ids-and-channels)) |

### Response frame (motor → ESP32, 10 bytes)

The motor usually sends this frame **twice**, back to back, after each status query. While a motor is running it often does not answer status queries; when it does, the frame shows the intermediate position and the running flag.

On this installation bytes of the reply are sometimes lost. Recorded
patterns:

- The end of the first copy and/or the start of the second copy (its `D8`,
  while the motor moves also its ID or a channel byte) are lost; the rest of
  the second copy is intact. Between the copies there is often a `00` or an
  extra `D8`.
- A single copy arrives with one byte missing between the channel and the
  position byte (`b3` or the voltage).

It started after days of error-free operation, with byte 5 = `0x17` (23 V)
instead of `0x18` (24 V), and was identical at 2350, 2400 and 2460 baud.
Each reply is followed by `0x00` bytes, i.e. the receiver reads 0 once the
motor stops sending, which points to a bus without fail-safe bias, see
[Waveshare ESP32-S3-RS485-CAN](#4-waveshare-esp32-s3-rs485-can). Since the
update to ESPHome 2026.10 most idle replies arrive intact again; replies of
running motors are still often damaged. How the firmware reads them is
described in [How feedback works](#how-feedback-works).

| Byte | Meaning |
|------|---------|
| 0 | `0xD8` — response header |
| 1 | Motor ID (`0x00` = not programmed) |
| 2 | Channel low byte |
| 3 | Channel high byte |
| 4 | `b3`, unknown: `0x00` while idle, non-zero while running (motor current or speed?) |
| 5 | Supply voltage in volts, it seems (`0x17`/`0x18` observed); reported as `Awning N Voltage` |
| 6 | `b5`, unknown: `0x00` while idle, non-zero while running |
| 7 | Raw position: 0–100; which end is 0 depends on the motor, see [Position conventions](#position-conventions) |
| 8 | Zone: bit 0 = running, bit 1 = position known, bit 6 = to a target (position command). Observed: `0x02` idle, `0x03` running after an open/close command (or the remote), `0x43` running to a target, both in either direction; after a power loss `0x00` idle, `0x01` running, `0x41` running to a target, `0x40` stopped at a target, always with position 0 (see [Position conventions](#position-conventions)) |
| 9 | Checksum: XOR of bytes 1–8 |

Example: `D8 02 01 00 00 18 00 00 00 1B` — motor 2, channel 1, raw position 0, idle.

Observed on motor 1 on 2026-10-31 (raw 0 = open, see
[Position conventions](#position-conventions)):

| Frame | State |
|---|---|
| `D8 01 01 00 00 18 00 00 02 1A` | Fully open: raw 0, zone `0x02` |
| `D8 01 01 00 34 18 65 4E 03 04` | Opening (command `0a dd`): raw 78, zone `0x03` |
| `D8 01 01 00 2A 18 62 08 03 5B` | Closing (command `0a ee`): raw 8, zone `0x03` |
| `D8 01 01 00 00 18 00 64 02 7E` | Fully closed: raw 100, zone `0x02` |
| — | Position commands `dd 0a` and `dd 00`: running replies with zone `0x43` in both directions, stopped at raw 10 and 0 |

Up to 1.4.1 the zone was read as the direction (`0x43` opening, `0x03`
closing). The test above disproves that: opening and closing with `0a dd` /
`0a ee` both give `0x03`, and `0x43` appears with position commands either
way. The firmware takes the direction from the command and the position
change (see [How feedback works](#how-feedback-works)).

The firmware treats other zone values, and `b3`/`b5` that do not match the
running bit, as implausible: such a reply only counts as a sign of life (see
[How feedback works](#how-feedback-works)). If a motor shows other values,
the log has `Motor N  reply not readable` after its `RX D8 …` line.

### Position conventions

The motors report and take the position as a raw value 0–100 between their
end limits. Position commands (`dd XX`) use the same scale as the replies.
Which end is 0 is **not fixed**, it was found on this installation on
2026-10-31:

- After a power loss a motor has lost its position: zone bit 1 is clear and
  the position byte is 0 (zone `0x00` idle, `0x01` running, `0x40`/`0x41`
  after a position command). A position command then moves it to an
  unpredictable place (a motor asked for raw 50 ran to the open end).
- It counts from 0 at the **first** end limit it reaches after power-up and
  reports its position again only at the **second** one, as raw 100.
- So raw 0 is open for a motor that reached the open end first, and closed
  for one that reached the closed end first, per motor and per power loss.
  This explains why raw 0 was closed on 2026-10-25 and open on 2026-10-31
  for motors 1 and 2; after the test on 2026-10-31 motors 1–2 counted
  raw 0 = open and motors 3–5 raw 0 = closed.

ESPHome uses `0.0 = closed`, `1.0 = open`. The firmware learns the counting
direction of each motor and keeps it across reboots (a `restore_value`
global per motor):

- when the motor finds its position at the end of an open (raw 100 = open)
  or close command (raw 100 = closed),
- from the raw position change during open and close commands (after the
  first 5 s), when two readings in a row disagree with the current
  direction; the log then shows `Motor N  counts the other way now`.

Moves by the remote control and position commands do not count. The
substitution `position_reversed` (`"true"`: raw 0 = open) is only the
start value for a motor that has not shown its direction yet; it can be
set per motor in its `vars` (see [Step 2](#step-2--motor-package-includes)).
Until then an awning may be shown mirrored; the first open or close fixes
it.

**After a power loss** (see [How feedback works](#how-feedback-works)) the
covers keep their last position, an open or close ends at the end limit and
shows it, and a position command first finds the position: the motor
closes fully and, if that was its first end limit, opens fully, then goes
to the position. Any other command for that motor cancels it.

### End limits

Position feedback needs the motor's end limits to be set (see the motor's manual
for the procedure with its remote). On motors without limits, observed on this
installation before mounting, the position stays at 0 and the running flag at 0
even after a move. With the limits set (the mounted motors), feedback works.

---

## ESPHome configuration

### File structure

```
esphome/
├── awning-motor.yaml          # main file — board, substitutions, motor list
├── components/uartex/         # bundled, patched uartex (hub + cover)
├── include/
│   ├── awning_frame.h         # frames: reads replies (also damaged ones), commands
│   ├── awning_motion.h        # per motor: online, opening/closing, poll schedule
│   └── awning_motor.h         # connects both to the covers and sensors
├── packages/
│   ├── motor.yaml             # template: cover + diagnostic sensors per motor
│   ├── group.yaml             # template: all-awnings cover
│   └── programming.yaml       # motor programming: mode, bus scan, status
├── secrets.yaml.example       # template for secrets.yaml
└── secrets.yaml               # your credentials (git-ignored)
tests/
└── awning_test.cpp            # host test of awning_frame.h and awning_motion.h
tools/
├── esp_probe.py               # send frames / watch the bus over the native API
└── requirements.txt
.github/workflows/
└── validate.yml               # CI: host test, esphome config + compile
```

### Entities

| Entity | Source | Category | Web UI group | Purpose |
|---|---|---|---|---|
| One cover per motor (`awning_N`, e.g. `Awning 1`) | `packages/motor.yaml` | — | Awnings | Open/close/stop/position; position and opening/closing from the motor, see [How feedback works](#how-feedback-works) |
| `All Awnings` cover | `packages/group.yaml` | — | Awnings | Open/close/stop/position for every motor at once; shows the average position and the motion of the online motors |
| `RS485 Raw Command` text | `awning-motor.yaml` | — | Service | Send any frame, e.g. for addressing; programming frames only in Programming Mode |
| `Restart`, `Safe Mode` buttons | `awning-motor.yaml` | config | Service | Reboot; reboot into safe mode (OTA only) |
| `Programming Mode` switch | `packages/programming.yaml` | config | Programming | Enables the Program buttons; off at boot and after 10 min without use |
| `Scan Bus` button | `packages/programming.yaml` | config | Programming | Lists the motors that answer, see [Addressing motors](#addressing-motors-programming-ids-and-channels) |
| `Programming Status` text sensor | `packages/programming.yaml` | diagnostic | Programming | Result of the last scan or programming run |
| One `Awning N Program` button per motor | `packages/motor.yaml` | config | Programming | Programs the only motor on the bus with this motor's ID and channel |
| One `Awning N Online` binary sensor per motor | `packages/motor.yaml` | diagnostic | Diagnostics | Motor answers on the bus, see [Online sensors](#online-sensors) |
| One `Awning N Voltage` sensor per motor | `packages/motor.yaml` | diagnostic | Diagnostics | Byte 5 of the status frame (V), see [Voltage and last seen](#voltage-and-last-seen) |
| One `Awning N Last Seen` timestamp per motor | `packages/motor.yaml` | diagnostic | Diagnostics | Time of the motor's latest reply, see [Voltage and last seen](#voltage-and-last-seen) |
| `RS485 Error` text sensor | `awning-motor.yaml` (uartex) | diagnostic | Diagnostics | Result of the last received frame, see [RS485 error sensor](#rs485-error-sensor) |
| `WiFi Signal`, `Uptime` sensors | `awning-motor.yaml` | diagnostic | Diagnostics | RSSI (every 60 s), seconds since boot |
| `IP Address`, `ESPHome Version` text sensors | `awning-motor.yaml` | diagnostic | Diagnostics | Network and firmware info |

### Step 1 — Substitutions

Edit the top of `awning-motor.yaml`:

```yaml
substitutions:
  device_name: awning-motor        # ESPHome device hostname
  friendly_name: Awning Motor Controller

  uart_rx_pin: GPIO18
  uart_tx_pin: GPIO17
  uart_flow_control_pin: GPIO21    # RS485 driver enable (DE/RE)

  poll_interval: 15s               # status query of an idle motor
  fast_poll_interval: 3s           # ... while it moves
  offline_poll_interval: 5min      # ... while it is offline (e.g. not connected)
  offline_timeout: 90s             # no reply for this long: Online off

  position_reversed: "true"        # start value: raw 0 = open (README → Position conventions)

  group1_channel_l: "0x01"         # ChanL byte for group 1 motors
  group1_channel_h: "0x00"
  group2_channel_l: "0x02"         # ChanL byte for group 2 motors
  group2_channel_h: "0x00"
```

### Step 2 — Motor package includes

Each motor is declared as one `packages:` entry. All four vars are required:

```yaml
packages:
  motor_1:
    !include
      file: packages/motor.yaml
      vars:
        motor_id: 1              # Hardware motor ID (1–255)
        motor_name: "Awning 1"   # Name shown in Home Assistant
        channel_l: ${group1_channel_l}
        channel_h: ${group1_channel_h}
```

Each entry generates one `awning_N` cover, the `Awning N Online`,
`Awning N Voltage` and `Awning N Last Seen` diagnostic sensors and the
`Awning N Program` button (which needs the `programming` package entry).
The motor is polled as described in [How feedback works](#how-feedback-works);
only replies that carry its `motor_id` and channel count. In the web interface the covers are sorted
by `motor_id` (`sorting_weight: ${motor_id}` in `motor.yaml`). An optional
`position_reversed` in `vars` overrides the substitution for this motor
(see [Position conventions](#position-conventions)).

### Step 3 — All-awnings cover

A single `group_all` package entry covers all motors at once:

```yaml
  group_all:
    !include
      file: packages/group.yaml
      vars:
        group_name: "All Awnings"
        group_id: awning_all
```

Open, close and stop are sent as one broadcast frame (ID=0x00, ChanL=0x00, ChanH=0x00), which all motors obey, so they start together. Channel-filtered broadcast (ID=0 with non-zero channel bytes) **does not work** — motors obey any ID=0 frame regardless of their programmed channel. Position frames sent to ID 0 are ignored by the motors, so a position (e.g. 25 %) goes to every motor that has answered since boot as its own command, 1.5 s apart.

### Step 4 — Programming package

```yaml
  programming: !include packages/programming.yaml
```

Required by `motor.yaml`: it provides `Programming Mode`, `Scan Bus`,
`Programming Status` and the script behind the per-motor `Program` buttons,
see [Addressing motors](#addressing-motors-programming-ids-and-channels).

### Adding or removing motors

To **remove** a motor, delete its `motor_N` package block. To **add** one, copy
an existing block, increment the key and set `motor_id`, `motor_name`,
`channel_l` and `channel_h`. Nothing else needs to change: every motor
registers itself at boot (polling, `All Awnings`, bus scan).

All `motor_id` values must match the ID programmed into the physical motor hardware (see [Addressing motors](#addressing-motors-programming-ids-and-channels)).

---

## How feedback works

The motors report their state only when asked (status query `cc 00`). The
logic is in three headers in [esphome/include](esphome/include):
[awning_frame.h](esphome/include/awning_frame.h) reads the frames,
[awning_motion.h](esphome/include/awning_motion.h) keeps the state of each
motor, [awning_motor.h](esphome/include/awning_motor.h) connects them to the
entities. The first two have no ESPHome dependencies and are tested on the
host by [tests/awning_test.cpp](tests/awning_test.cpp), see
[Continuous integration](#continuous-integration).

**Polling.** Each motor gets a status query

| State | Interval (substitution) |
|---|---|
| Idle | 15 s (`poll_interval`) |
| Moving, after a command or a reply with the running bit | 3 s (`fast_poll_interval`) |
| Offline, or not answered since boot (e.g. not connected) | 5 min (`offline_poll_interval`) |

and one right after boot. At most one query per motor waits in uartex's
queue, behind any commands; uartex sends one frame every 1.5 s (`tx_delay`),
so with several motors moving each one is polled every 3 s or every
(number of moving motors × 1.5 s), whichever is longer.

**Reading the replies.** Every burst of received bytes goes through
`read_reply()`. A burst belongs to a motor if it starts with `D8` and the
motor's ID and channel bytes; that alone sets `Online` and `Last Seen`. The
status (position, zone, voltage) comes from

1. every intact block (ID … checksum) with the motor's address in the burst,
   including a second copy that lost its `D8`,
2. the end of the burst: the last copy's `b3 voltage b5 position zone
   checksum`, checked against the motor's known address (covers a second
   copy that lost its ID or a channel byte),

and is only used if all of these agree. Failing both, a single copy with one
lost byte before the position is repaired from the checksum (its voltage is
not used). Every block must be plausible: voltage 20–30, position ≤ 100, a
known zone value, `b3`/`b5` zero exactly when idle (see
[Response frame](#response-frame-motor--esp32-10-bytes)). These checks matter
because an XOR checksum also matches many blocks that lost a `0x00` byte.

The host test feeds simulated replies (one or two copies, noise between
and after them) with 0–3 bytes deleted at random places through
`read_reply()`, 400 000 of motors that know their position and 200 000 of
motors that lost it (after a power loss):

| Lost bytes | Status read | Wrong (position known) | Wrong (position lost) |
|---|---|---|---|
| 0 | 100 % | 0 | 0 |
| 1 | 84 % | 0 in 100 000 | 11 in 50 000 |
| 2–3 | 33–57 % | 8 in 200 000 | 29 in 100 000 |

No burst gave a status for another motor: a burst with an intact block of
another motor is only read through intact blocks after a `D8` (addresses
such as `01 01 00` and `00 00 00` have the same XOR). The remaining errors
are copies that lost bytes in a way an XOR checksum cannot detect, e.g. the
position byte `0x00` of a running motor that lost its position, whose
checksum then reads as a running zone; the next poll corrects them.

**Opening and closing.** When a command for a motor goes out on the bus
(its cover, `All Awnings` or `RS485 Raw Command`), its cover shows opening or
closing at once (direction from the target and the current position); the
position stays at the motor's real value and follows its replies. Commands
for a motor that has not answered since boot (e.g. not connected) and
position frames to ID 0 (which the motors ignore) show no motion. The move
ends with the first reply without the running bit, except that a reply
"idle at the start position" within 5 s after the command is ignored (the
motor may not have started yet), or after 2 minutes without a running
reply. Stop shows idle at once and polls until the final position is in.
A move by the remote control shows up with the next poll that has the
running bit (within 15 s), then fast polling follows it. Motors often skip
replies while running; then the cover jumps to the end position when the
move is over. While running, the direction comes from the position change
between two replies; the zone does not tell it (see
[Response frame](#response-frame-motor--esp32-10-bytes)).
Without a change the cover keeps the command's direction. A move by the
remote control that starts at an end position can only go one way; one that
starts in between shows idle (but is polled fast) until the position
changes.

**All Awnings** shows the average position of the online motors and
opening/closing while they move (opening if at least as many open as
close). Open, close and stop are broadcasts, so every motor follows them
itself; a position is sent to each motor (see
[Step 3](#step-3--all-awnings-cover)).

**After a reboot** each cover shows its last position (saved by ESPHome's
preferences, written to flash at most once a minute) until the first reply,
which comes right after boot. A motor that never answered shows the
default (open). The learned counting direction is kept as well.

**After a power loss** of motors and controller (tested on 2026-10-31) the
motors have lost their position (see
[Position conventions](#position-conventions)): the log shows
`Motor N  has lost its position`, and the covers keep the last position
saved before the power loss. Open and close work as usual; at their end the
cover shows the end position. A position command (single cover or
`All Awnings`) first finds the position: `closing to find it`, then if
needed `opening fully to find the position`, then `position found, moving
to N%`. Tested on 2026-10-31: with the close alone motor 2 found its
position and went to the target, motors 3–5 did not (closed end reached
first); close and open found it for all of them. The automatic two-step
sequence (close, open, target) is covered by the host test, not yet by a
second power loss.

---

## Web interface

The firmware contains ESPHome's own web interface (`web_server` version 3,
`local: true`, so it needs no internet access). Open
[http://awning-motor.local/](http://awning-motor.local/) (or the controller's
IP address) and log in with `web_username` / `web_password` from
`secrets.yaml`.

The entities are arranged in four groups:

| Group | Entities |
|---|---|
| Awnings | `Awning 1` … `Awning 5` in motor-ID order, then `All Awnings` |
| Service | `RS485 Raw Command`, `Restart`, `Safe Mode` |
| Programming | `Programming Mode`, `Scan Bus`, `Programming Status`, then `Awning 1 Program` … `Awning 5 Program` |
| Diagnostics | Per motor `Awning N Online`, `Voltage`, `Last Seen`; then `RS485 Error`, `WiFi Signal`, `Uptime`, `IP Address`, `ESPHome Version` |

The page also shows the live device log. The same login protects ESPHome's
[REST and event-stream API](https://esphome.io/web-api/), e.g.:

```bash
curl --digest -u admin:YourWebPassword "http://awning-motor.local/cover/Awning%201"
curl --digest -u admin:YourWebPassword -X POST "http://awning-motor.local/cover/Awning%201/stop"
```

Firmware upload through the web interface is disabled (`ota: false`);
updates go through ESPHome's encrypted OTA (`esphome run` / `esphome upload`),
see [Firmware updates (OTA)](#firmware-updates-ota).

> **Security** — The login uses HTTP Digest authentication (`type: digest`),
> so the password itself is not sent in clear text, but the connection is
> plain, unencrypted HTTP: everything else, including commands and the device
> log, can be read by anyone on the network path. Use the web interface on
> your own LAN only. **Never expose port 80 to the internet** (no port
> forwarding, no reverse proxy without its own TLS and authentication). Use a
> password that you do not use anywhere else.

A custom overview page loaded through `js_include` was evaluated and left
out: with `local: true` the firmware serves the bundled v3 page, which never
loads the `js_include` script (`/0.js`); it is only loaded together with the
online (CDN) build of the interface. The sorting groups above give the same
overview without extra code.

---

## Firmware updates (OTA)

Over-the-air updates use ESPHome's native OTA (`ota: platform: esphome`)
with `encryption:` and no password. The upload is encrypted and
authenticated with the same `api_key` as the native API, so there is no
separate OTA password, and the firmware image (which contains the Wi-Fi
credentials and the API key) never crosses the network in clear text. The
controller refuses unencrypted uploads.

- Whoever builds and uploads the firmware (ESPHome CLI or Device Builder)
  needs the same `api_key` in `secrets.yaml`.
- If the key is lost, the firmware can only be replaced over USB-C (or via
  the captive portal in fallback access point mode).

**Migrating an existing device from an OTA password** must take two steps,
because firmware built before ESPHome 2026.9 cannot receive an encrypted
upload:

1. Flash firmware built with ESPHome 2026.9 or newer that has an API
   encryption key, still with `password:`. The device log (`dump_config`)
   then shows `Encryption: offered, plaintext accepted`.
2. Replace `password:` with `encryption:`, flash again and check that the log
   shows `Encryption: required`. Then `ota_password` can be removed from
   `secrets.yaml`.

ESPHome drops the plaintext fallback in 2027.3, so step 1 alone is not
enough in the long run.

---

## Fallback access point

If the configured Wi-Fi network cannot be reached for 90 s (`ap_timeout`
default), the controller opens its own access point named `awning-motor`,
protected with `ap_password` from `secrets.yaml`. Connect to it and the
captive portal opens, where you can enter new Wi-Fi credentials. The access
point closes again once the controller is back on the normal network.

> The captive portal has no login of its own and also accepts a firmware
> upload, so the access point password is its only protection: choose a
> strong one.

`wifi: fast_connect` is deliberately left off. With a mesh or several access
points that share one SSID, the default full scan connects to the strongest
one; `fast_connect` takes the first match. Only enable it for a single fixed
access point or a hidden SSID.

---

## Online sensors

Every motor gets a binary sensor `Awning N Online` (device class
`connectivity`, category diagnostic), generated by `packages/motor.yaml`.

- It turns **on** as soon as a reply from that motor arrives: a burst of
  bytes from the raw receive tap (the `debug:` block of `uart:` in
  `awning-motor.yaml`) that starts with `D8`, the motor's ID and its channel
  bytes. The rest of the reply may be damaged; these first bytes survived in
  every damaged reply observed (see
  [Response frame](#response-frame-motor--esp32-10-bytes)).
- It turns **off** after `offline_timeout` (90 s) without a reply. Motors
  often do not answer while they are moving, so a few missed polls are
  normal and must not mark a motor offline.
- After a reboot it is off until the motor answers its first poll, which
  is sent right after boot.

The state is kept by `awning::Motor` (see
[How feedback works](#how-feedback-works)); while a motor is offline it is
polled only every `offline_poll_interval` (5 min), so a newly connected
motor shows up within 5 minutes, or at once after `Scan Bus` or its
`Program` button.

### Voltage and last seen

- `Awning N Voltage` shows byte 5 of the status frame in volts. `0x17`/`0x18`
  (23/24 V) have been observed; it is probably the motor's internal DC
  supply, but this is not confirmed. The value is sent only when it changes.
  It comes from intact blocks and from the end of a damaged reply (checked
  by the checksum), not from a single copy repaired from the checksum (see
  [How feedback works](#how-feedback-works)).
- `Awning N Last Seen` (device class `timestamp`) is the time of the motor's
  latest reply (as for the online sensor, damaged replies count). The clock
  comes from Home Assistant (`time: platform: homeassistant`) or, without
  Home Assistant, from SNTP (`pool.ntp.org`); both set the system clock. The
  sensor stays empty until one of them has synchronised after a reboot. To
  keep the history small it is updated when the motor comes back online and
  then at most every 5 minutes; when the motor goes offline it is set to the
  exact time of its last reply.

### RS485 error sensor

`RS485 Error` is uartex's built-in error sensor. It shows the result of the
last frame uartex's parser received: `None` for a valid frame, otherwise
`Size Error`, `Header Error`, `Checksum Error` and so on. It is only sent when
the value changes, so a single disturbed frame shows up as a short error
followed by `None` with the next valid frame. A value that stays on an error
points to wiring, termination or a second device sending on the bus.

The covers do not use uartex's parser (see
[How feedback works](#how-feedback-works)), and frames that break off (fewer
than 10 bytes before the next header or a pause) are discarded by it without
an error, so `RS485 Error` can stay on `None` while replies are damaged.
`Awning N Online` and the `RX` lines in the log are the better indicators.

### uartex (bundled, patched copy)

[esphome/components/uartex](esphome/components/uartex) is a copy of
[eigger/espcomponents](https://github.com/eigger/espcomponents) tag `v260904`
(commit `0028799`, MIT licence, [LICENSE](esphome/components/uartex/LICENSE)),
reduced to the hub and the cover platform. The configuration loads it as a
local external component. Here it sends the commands and status queries
(one every `tx_delay`) and feeds the `RS485 Error` sensor; its write
callback tells each motor which commands went out.

Three changes, each marked `gumax-sunshade-esphome patch`:

1. `Parser::parse_byte()` in [parser.cpp](esphome/components/uartex/parser.cpp):
   when the 10-byte window no longer starts with the header `D8`, the
   original parser cleared the whole buffer, including a header that had
   already arrived inside it. The patched parser drops leading bytes until
   the buffer starts with a header again.
2. `UARTExCover::control()` in
   [uartex_cover.cpp](esphome/components/uartex/cover/uartex_cover.cpp): only
   an `optimistic` cover takes the target position at once. Upstream also did
   so whenever a command was queued, so covers with feedback jumped to the
   target and back.
3. `UARTExCover::setup()`: restores the last published position after a
   reboot (ESPHome's usual cover restore), which upstream did not do.

To update uartex: copy the hub files and `cover/` of the new tag over this
folder (without the upstream README), re-apply the patches, update the tag
and commit here, and re-test. The next tag, `v261021`, changes nothing in the
hub or the cover platform (only uartex's README), so the copy stays at
`v260904`.

---

## Tools

[tools/esp_probe.py](tools/esp_probe.py) connects to the controller over the
ESPHome native API, sends frames through the **RS485 Raw Command** entity and
prints the device log, which includes every RX/TX frame. Home Assistant is not
involved, which makes it the quickest way to address motors or check the bus.

```bash
python -m venv .venv
.venv/bin/pip install -r tools/requirements.txt      # Windows: .venv\Scripts\pip

# status of motor 5
.venv/bin/python tools/esp_probe.py "9a 05 02 00 cc 00"

# close motor 4 for six seconds, then stop it
.venv/bin/python tools/esp_probe.py "9a 04 01 00 0a ee" @6 "9a 04 01 00 0a cc"

# find programmed motors on channel 1 (queries IDs 01–ff)
.venv/bin/python tools/esp_probe.py --scan "01 00"
```

- The controller is reached at `awning-motor.local`; use `--host` or `ESPHOME_HOST` for an IP address.
- The API key is read from `esphome/secrets.yaml`, or from `ESPHOME_API_KEY`.
- `--gap` sets the pause between frames (default 4 s), `--listen` how long to keep logging afterwards (default 5 s). `@<seconds>` tokens replace `--gap` with explicit pauses.

---

## Versioning

The firmware reports a project name and version through `esphome: project:`
in [esphome/awning-motor.yaml](esphome/awning-motor.yaml). Home Assistant
shows it in the device info next to the ESPHome version.

The version follows [Semantic Versioning](https://semver.org/) and is bumped
in the same commit as the change:

| Bump | When | Example |
|---|---|---|
| Major (`2.0.0`) | Existing entities are renamed or removed, or behaviour changes so that Home Assistant automations or dashboards must be adapted | Renaming `Awning 1`, changing the position convention |
| Minor (`1.2.0`) | New entities or features; existing ones keep working | New sensor, new web UI group, new secret key |
| Patch (`1.1.1`) | Fixes and internal changes without visible entity changes | Timing fix, uartex or ESPHome update, comments |
| Pre-release (`1.3.0-beta.1`) | The upcoming version while parts of it are not tested on hardware yet; becomes the plain version once tested | New feature flashed but not fully tried |

README-only or CI-only changes do not change the firmware and need no bump.

---

## Continuous integration

[.github/workflows/validate.yml](.github/workflows/validate.yml) runs on every
push and pull request:

- job `test`: builds and runs [tests/awning_test.cpp](tests/awning_test.cpp)
  with g++ (`-Wall -Wextra -Werror`, AddressSanitizer and
  UndefinedBehaviorSanitizer): reply decoding with recorded replies
  and the lost-byte simulation, command parsing, motor state and polling
  (see [How feedback works](#how-feedback-works)). Locally:
  `g++ -std=c++17 -I esphome/include tests/awning_test.cpp -o awning_test && ./awning_test`.
- job `validate`:
  1. installs ESPHome **2026.10.1** (pinned, matching the tested version),
  2. copies `secrets.yaml.example` to `secrets.yaml` (placeholders only),
  3. runs `esphome config` and `esphome compile` in `esphome/`.

PlatformIO and toolchain downloads are cached between runs. The workflow
publishes nothing: no firmware artifacts, no releases. When updating ESPHome,
change `ESPHOME_VERSION` in the workflow together with the version under
[Requirements](#requirements).

---

## Troubleshooting

| Symptom | Likely cause |
|---|---|
| No reply to any frame | A and B swapped; wrong `uart_flow_control_pin`; long bus runs may need the 120R jumper, but without bias it can make lost bytes worse (see [Waveshare ESP32-S3-RS485-CAN](#4-waveshare-esp32-s3-rs485-can)) |
| Only `D8 00 …` replies | Motor not programmed, or factory-reset — see [Addressing motors](#addressing-motors-programming-ids-and-channels) |
| A cover never updates | `motor_id` or channel bytes in the YAML don't match the motor |
| Open awnings shown as closed (and the other way round), position commands mirrored | The counting direction of that motor is not learned yet (e.g. a new motor, or the remote control found the position after a power loss) — fully open or close it once from Home Assistant; see [Position conventions](#position-conventions) |
| Log `Motor N  position still unknown, N% not sent` | After a power loss the calibration moves were stopped (e.g. by the remote control) before both end limits; close and open the awning fully once |
| Position always 0, running flag never set | End limits not set — see [End limits](#end-limits) |
| No status replies while a motor moves | Observed behaviour: motors often don't answer while running; the cover shows the end position when the move is over |
| `Awning N Online` stays off | No valid reply from that motor: check wiring, `motor_id` and channel bytes, as for a cover that never updates |
| Position or voltage not updating although `Awning N Online` is on | Replies arrive damaged in a way that cannot be read safely, or with values the firmware does not know (the log shows `Motor N  reply not readable` after the `RX D8 …` line). See [How feedback works](#how-feedback-works). The cover keeps showing the last known position |
| `Awning N Last Seen` stays empty | No time yet: neither Home Assistant nor SNTP has synchronised since the last reboot |
| `RS485 Error` stays on an error | Disturbed frames: check A/B wiring, the 120R termination and that nothing else transmits on the bus |
| A cover shows "open" although its motor never answered | ESPHome covers have no "unknown" state; a motor that never answered keeps the default (open). Check `Awning N Online` |
| A cover shows opening/closing, but the motor does not move | The motor ignored the command; the cover returns to idle 5 s after the command with the next reply |
| `RS485 Raw Command` sends nothing | Malformed input (log warning), or a programming frame while `Programming Mode` is off |
| OTA upload refused | The uploader's `api_key` differs from the one in the firmware; see [Firmware updates (OTA)](#firmware-updates-ota) |
| A remote channel moves several motors | The motors are linked by a 6-wire RJ12 cable, which connects their wired inputs (pins 2–4); see [Daisy-chaining motors](#3-daisy-chaining-motors) |
| `Program` refused: "no motor answered" | The motor is not connected, or it carries an ID/channel that is not in the configuration; see [Addressing motors](#addressing-motors-programming-ids-and-channels) |
| `Program` refused: "2 motors" / "garbled replies" | Another motor is still on the bus; disconnect it, press `Scan Bus` to check |

---

## Fork history

| Repository | |
|---|---|
| [s3bi77/gumax-sunshade-esphome](https://github.com/s3bi77/gumax-sunshade-esphome) | The original project: protocol analysis, documentation and the uartex-based ESPHome configuration. All of the design and most of the configuration is theirs. |
| this repository | Forked from s3bi77/gumax-sunshade-esphome at `7e81ed4` to run on a Waveshare ESP32-S3-RS485-CAN with five motors. |

---

## Changes in this fork

Modified in October 2026. The full record is in the Git history; this is the summary.

**Hardware**

- Target board Waveshare ESP32-S3-RS485-CAN: `esp32-s3-devkitc-1`, 16 MB flash, ESP-IDF.
- UART on RX GPIO18 / TX GPIO17; the transceiver's driver enable is switched through `flow_control_pin` on GPIO21.
- Logging over the native USB port (`USB_SERIAL_JTAG`).

**Configuration**

- Five motors instead of eight: IDs 1–4 on channel 1, ID 5 on channel 2. The motors are programmed to match, see [This installation](#this-installation).
- uartex pinned to tag `v260904` instead of `@latest` with `refresh: always`, so a rebuild produces the same firmware. Since 1.3.0 a patched copy of that tag is bundled instead, see [uartex](#uartex-bundled-patched-copy).
- `rx_priority: loop`: receiving no longer blocks the ESPHome main loop, which had produced "took a long time" warnings.
- Every received frame is logged at `DEBUG`, including replies no cover listens to (an unprogrammed motor answering as ID 0). The per-motor decode log moved from `INFO` to `DEBUG`.
- `poll_interval` now takes effect: a package default in `motor.yaml` shadowed the value from the main file.
- The package templates no longer have defaults for required values, so a missing `motor_id` fails the build instead of silently creating `awning_1`. The broadcast cover no longer takes channel vars the motors ignore.

**Web interface and operation (versions 1.1.0 to 1.6.0)**

- ESPHome web interface (`web_server` v3, local, Digest login, web OTA disabled) with the groups Awnings, Service and Diagnostics; see [Web interface](#web-interface).
- New per-motor online sensors, implemented in `packages/motor.yaml` with a watchdog script (since 1.4.0 in `include/awning_motor.h`); see [Online sensors](#online-sensors).
- Fallback access point with captive portal; see [Fallback access point](#fallback-access-point).
- Diagnostic and service entities: WiFi signal, uptime, IP address, ESPHome version, restart and safe-mode buttons.
- `esphome: project:` name and version; see [Versioning](#versioning).
- CI workflow that validates and compiles the configuration; see [Continuous integration](#continuous-integration).
- `secrets.yaml.example` gained `web_username`, `web_password` and `ap_password`, and a syntactically valid placeholder `api_key`.
- Version 1.1.1: OTA uploads are encrypted with the API key instead of protected by `ota_password`, which is gone; see [Firmware updates (OTA)](#firmware-updates-ota).
- Version 1.2.0: per-motor `Voltage` and `Last Seen` sensors, the `RS485 Error` sensor, and Home Assistant as time source; see [Voltage and last seen](#voltage-and-last-seen).
- Version 1.2.1: raw position direction fixed (0 = closed, 100 = open) for feedback and position commands. Closed awnings had been shown as open after the next status poll; see [Position conventions](#position-conventions).
- Version 1.3.0 (tested on this installation; developed as 1.3.0-beta.1 to beta.4):
  - Motor programming from the web interface and Home Assistant (`Programming Mode`, `Scan Bus`, `Programming Status`, one `Awning N Program` button per motor) in the new `packages/programming.yaml`; see [Addressing motors](#addressing-motors-programming-ids-and-channels).
  - `All Awnings` offers a position (slider in the web interface and Home Assistant). uartex only enables position support with a `state_position`; the broadcast cover now has one that returns its own optimistic position.
  - Bundled, patched uartex whose parser resynchronises on the next header, so the intact second copy of a damaged reply is used; see [uartex](#uartex-bundled-patched-copy).
  - Raw receive tap (`uart:` `debug:` block) that logs every reply, damaged ones included. `Awning N Online`, `Last Seen` and the bus scan recognise a motor by the first bytes of its reply (`D8`, ID, channel).
  - Position, zone and voltage are also read from damaged replies ([include/awning_frame.h](esphome/include/awning_frame.h)), anchored at the intact end of the frame and only when the voltage byte shows that the lost byte was before the position; byte-shifted frames are ignored. Moves by remote were reported by the motors all along; only the damaged replies hid them. See [Response frame](#response-frame-motor--esp32-10-bytes).
  - Dropped on the way: a repair that inserted the lost byte from the checksum (wrong values in about a quarter of the cases in a simulation).
  - Corrected: programmed motors do answer the broadcast status query (their replies had been lost); a 6-wire RJ12 cable between motors couples them through the wired inputs (see [Daisy-chaining motors](#3-daisy-chaining-motors)).
- Version 1.4.0 (tested on this installation with motors 1 and 2; developed as 1.4.0-beta.1), see [How feedback works](#how-feedback-works):
  - Feedback no longer goes through uartex's frame parser. All replies are read from the raw receive tap by `include/awning_frame.h`: intact blocks anywhere in a reply (also a second copy that lost its `D8`), the end of a reply checked against the motor's address (a copy that lost its ID or a channel byte), stricter plausibility (zone values, `b3`/`b5` against the running bit) and no status from contradicting copies. Motor 1 had not reported any position for some time because its replies lost the `D8` of the second copy. A host test with recorded replies and a lost-byte simulation runs in CI.
  - Covers show opening/closing, follow the motor live (status query every 3 s while it moves, also after a move by the remote control) and no longer jump to the target and back (uartex cover patch). Newly observed: zone `0x43` while opening, `0x03` while closing; `b3`/`b5` are non-zero only while running.
  - Polling per motor instead of a fixed interval: 15 s idle (was 30 s), 3 s moving, 5 min offline or not connected; at most one query per motor in the queue. New substitutions `fast_poll_interval`, `offline_poll_interval`, `offline_timeout` (the online timeout no longer follows `poll_interval`).
  - `All Awnings` shows the average position and the motion of the online motors instead of an optimistic state. Its position command is now sent to each motor: the motors ignore position frames sent to ID 0, which went unnoticed in 1.3.0.
  - Positions are restored after a reboot (uartex cover patch).
  - SNTP as a second clock for `Last Seen`.
  - `RS485 Raw Command` refuses programming frames while `Programming Mode` is off, and no longer sends a junk frame for malformed input.
  - ESPHome 2026.10.0 (was 2026.9.1). With it, idle replies mostly arrive intact again.
- Version 1.4.1 (tested on this installation):
  - Commands for a motor that has not answered since boot (e.g. not connected) no longer show opening/closing; after `All Awnings` open or close, the covers of unconnected motors showed it for 2 minutes. Position frames to ID 0, which the motors ignore, no longer show motion either.
  - ESPHome 2026.10.1; only the generic flash driver is linked (`esp32: framework: advanced: flash_chip: generic`, suggested by the boot log), which saves IRAM.
  - CI: the host test runs with AddressSanitizer and UndefinedBehaviorSanitizer; GitHub Actions updated (checkout v7, setup-python v7, cache v6).
- Version 1.5.0 (tested on this installation):
  - All awnings were open but shown as closed: the motors now report raw 0 when open and 100 when closed, the reverse of what was observed on 2026-10-25. New substitution `position_reversed` (default `"true"` here, per motor overridable) maps raw positions to ESPHome's convention for the feedback and the position commands (individual covers, `All Awnings`, position frames of `RS485 Raw Command`); see [Position conventions](#position-conventions). The log shows both values (`Motor N  open=…% (raw …)`).
  - Direction while running from the command and the position change instead of the zone: zone `0x03` comes with open and close commands in either direction, `0x43` with position commands in either direction. Before, an opening awning (`0x03`) was shown as closing.
- Version 1.6.0 (tested on this installation, including a power loss of motors and controller; the full calibration after a second power loss is not yet tested end to end):
  - Power loss: the motors lose their position (zone bit 1 clear, position 0) and count from the first end limit they reach, so raw 0 is open or closed per motor. 1.5.0 showed such motors as open. Now the counting direction is learned per motor from its open/close moves and kept across reboots (`position_reversed` is only the start value); see [Position conventions](#position-conventions).
  - Without a known position the covers keep their last position, open/close end at the end position, and position commands first find the position (close, then open if needed), then go to the target.
  - Reply decoding: zone values of motors without position accepted (`0x00`, `0x01`, `0x40`, `0x41`, position must be 0); a burst with an intact block of another motor is not read through its end or as a damaged copy (fixes rare statuses for the wrong motor in the simulation).
  - Programming and other frames to a motor no longer end the move the cover shows.

**Documentation**

- README brought in line with the uartex configuration: removed features that no longer existed at the time (the upstream online/offline sensors, status buttons, `offline_timeout_ms`, `!extend`-based dispatch); `direction_pin` corrected to `flow_control_pin`. Online sensors were later added back with a new implementation (see above).
- Added board wiring, a tested addressing procedure, the programming command in the protocol table, observations on response bytes, end limits and status while moving, and a troubleshooting table.

**Tooling and housekeeping**

- [tools/esp_probe.py](tools/esp_probe.py) for addressing and bus checks without Home Assistant.
- Comments in English throughout.
- Line endings normalised to LF, with `.gitattributes` and `.editorconfig`; one `.gitignore` at the root instead of two overlapping ones.

---

## Legal & disclaimer

**Electrical safety**: Tubular awning motors operate at mains voltage (typically 230 V AC). Working on or near live mains wiring can cause **electric shock, fire, or death**. Always isolate mains power before accessing the motor internals, the PCB, or the RJ12 connectors. The authors are not responsible for any injury, death, property damage, or fire resulting from the use of this project. **If you are not qualified to work with mains voltages, consult a licensed electrician.**

**No warranty / no liability**: This project is provided as-is, with no warranty of any kind, express or implied. The authors accept no responsibility or liability for any damage to motors, ESP32 boards, associated hardware, property, or any other loss arising from the use, misuse, or inability to use this project. **Use entirely at your own risk.**

**Reverse engineering**: The RS485 protocol documented here was derived by observing bus traffic on hardware already owned by the author. This is permitted for interoperability purposes under:
- EU Software Directive 2009/24/EC, Article 6 (decompilation for interoperability)
- US Digital Millennium Copyright Act, 17 U.S.C. §1201(f)

**Trademarks**: "Gumax" and "AOK" are trademarks of their respective owners. Use of these names here is purely for compatibility identification (nominative fair use) and does not imply any affiliation with or endorsement by those companies.

---

## Credits

- **Original project**: [s3bi77/gumax-sunshade-esphome](https://github.com/s3bi77/gumax-sunshade-esphome) — protocol analysis, documentation and ESPHome configuration this fork is built on.
- **Protocol reference**: [Loxone Library — Electric Curtain Drive A-OK](https://library.loxone.com/detail/electric-curtain-drive-a-ok-1922/overview) — community documentation of the AOK-E AM68 RS485 protocol that helped confirm the frame structure.
- **ESPHome** — open-source firmware framework ([Apache 2.0](https://github.com/esphome/esphome/blob/dev/LICENSE)).
- **uartex** by [eigger](https://github.com/eigger) — ESPHome external component that provides declarative RS485 frame parsing and command dispatch ([MIT License](https://github.com/eigger/espcomponents/blob/master/LICENSE), [source](https://github.com/eigger/espcomponents)). A patched copy is bundled in [esphome/components/uartex](esphome/components/uartex), see [uartex](#uartex-bundled-patched-copy).
- **aioesphomeapi** — Python client for the ESPHome native API, used by `tools/esp_probe.py` ([MIT License](https://github.com/esphome/aioesphomeapi)).
- **Inspiration for the web interface and operational features**: [Tysonpower/HCPBridgeMqtt_tynet](https://github.com/Tysonpower/HCPBridgeMqtt_tynet) — its ESPHome configuration was the inspiration for the web interface, fallback access point, diagnostics and project versioning here, adapted to five motors on one RS485 bus.

---

## Licence

Copyright of the original work belongs to its respective authors; see the Git
history for authorship.

This project is released under the MIT License, the licence of the upstream
project. See the [LICENSE](LICENSE) file for the full text.

The `LICENSE` file is carried over from the original project byte for byte,
including its unfilled copyright placeholder; `.gitattributes` keeps it out of
the repository's line-ending normalisation.