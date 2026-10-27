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
- **Position feedback** read from the motor's own status frames, polled every 30 s
- **All-awnings broadcast** cover — open/close/stop/position all motors with one command
- **RS485 Raw Command** text entity to send any frame from Home Assistant (motor addressing, debugging)
- **Motor programming from the UI**: one `Program` button per motor, guarded by a bus scan and a self-expiring `Programming Mode`, see [Addressing motors](#addressing-motors-programming-ids-and-channels)
- Every received RS485 frame is logged at `DEBUG` level
- **Web interface** built into the firmware (ESPHome `web_server` v3, password-protected) — works without Home Assistant, see [Web interface](#web-interface)
- **Online, voltage and last-seen sensors per motor**, an **RS485 error** sensor, a **fallback access point** with captive portal, and diagnostic entities (WiFi signal, uptime, IP, ESPHome version, restart / safe mode)
- Plain ESPHome YAML on top of the [uartex](https://github.com/eigger/espcomponents) external component
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
- [ESPHome](https://esphome.io/guides/installing_esphome) 2026.9 (tested with 2026.9.1). The uartex component is bundled in [esphome/components/uartex](esphome/components/uartex) (patched copy of tag `v260904`, see [uartex](#uartex-bundled-patched-copy)); update ESPHome and uartex together.
- Home Assistant (optional but recommended for the cover UI; it is also the clock for the `Last Seen` sensors)
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
   With the ESPHome Device Builder in Home Assistant instead, copy `awning-motor.yaml` and the `packages/` folder to `/config/esphome/` and add the six keys from `secrets.yaml.example` to the Device Builder's own `secrets.yaml`.
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

The IDs were programmed one motor at a time with [tools/esp_probe.py](tools/esp_probe.py)
and each one verified by a status query. A motor that is factory-reset forgets
its ID and has to be programmed again.

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

### 4. Waveshare ESP32-S3-RS485-CAN

The board has an isolated RS485 transceiver on board; the UART pins are fixed by
the board and already set in the YAML:

| Signal | ESP32-S3 pin | Substitution |
|---|---|---|
| RX | GPIO18 | `uart_rx_pin` |
| TX | GPIO17 | `uart_tx_pin` |
| Driver enable (DE/RE) | GPIO21 | `uart_flow_control_pin` |

Connect the board's RS485 **A** terminal to motor pin 1 and **B** to motor pin 6.
If the terminal block has an RS485 ground, connect it to motor pin 5.

- Power the board either from its terminal block or from USB-C, never from both at the same time (Waveshare FAQ).
- If communication is unreliable, move the board's termination jumper to **120R**.
- Logs are available on the USB-C port (`USB_SERIAL_JTAG`) and over Wi-Fi.

### 5. Other boards

Any ESP32 with a TTL-to-RS485 converter works. Change the `esp32:` block to your
board and set the pins in `substitutions:`.

| Converter pin | ESP32 pin | Notes |
|---|---|---|
| A (or D+) | — | Connect to motor RS485-A |
| B (or D−) | — | Connect to motor RS485-B |
| GND | GND | Must share ground with motors |
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

> **Beta (1.3.0-beta.1):** the scan and the refusals are tested on hardware;
> programming a motor and the confirmation are not tested yet.

The **Programming** group of the web interface, and the Configuration section of
the device page in Home Assistant, hold a `Program` button per configured motor.
The button programs the ID and channel from that motor's package entry, so
nothing has to be looked up.

1. Switch on **`Programming Mode`**. It switches itself off after 10 minutes
   without use; every Program press restarts the 10 minutes.
2. Connect **only** the motor to be programmed to the bus.
3. Press its button, e.g. **`Awning 3 Program`**. The firmware
   - scans the bus: the broadcast status query and one status query per
     configured motor (a reply lost on one query is caught by another),
   - refuses unless exactly one motor answered and no frame was garbled,
   - sends the programming frame (the motor blinks blue three times),
   - queries the new ID and channel to confirm.
4. Read the result in **`Programming Status`**, e.g.
   `Awning 3 programmed: ID 3 / channel 1 confirmed`, or
   `Refused (2 motors: ID 1 / channel 1, ID 2 / channel 1). Connect only the motor to program.`
5. Connect the next motor and continue with step 3.

**`Scan Bus`** runs the same scan without programming and works without
Programming Mode, e.g. `Scan: 1 motor: ID 0 (unprogrammed)`.

If Program is refused with "no motor answered" although the motor is
connected, its replies were lost on the bus (see
[Troubleshooting](#troubleshooting)); press `Scan Bus` a few times, or program
it with the raw frames below.

**Procedure with raw frames** (used for [this installation](#this-installation)):

Send the frames below either through the **RS485 Raw Command** entity in Home
Assistant (and watch the ESPHome log) or with [tools/esp_probe.py](tools/esp_probe.py).
The checksum is appended automatically.

1. Connect **only** the motor to be programmed to the bus.
2. Check the bus: send `9a 00 00 00 cc 00`. Every connected motor answers this
   broadcast query with its own ID, so exactly one reply must come back
   (`D8 00 00 00 …` for an unprogrammed motor).
3. Program it, e.g. ID 2 on channel 1: `9a 02 01 00 aa aa`.
4. Verify: `9a 02 01 00 cc 00` must be answered with `D8 02 01 00 …`.
5. Disconnect the motor, connect the next one, continue with step 2.

With esp_probe.py, steps 2–4 for motor 2 are:

```bash
python tools/esp_probe.py "9a 00 00 00 cc 00"
python tools/esp_probe.py "9a 02 01 00 aa aa" "9a 02 01 00 cc 00"
```

> **Upstream walk-through** (8 motors, ESP powered from the 5 V tap of the nearest motor): daisy-chain all motors, but power only the motor feeding the ESP and the one being programmed. Program from the far end; the motor acknowledges by blinking blue three times. Program the motor feeding the ESP last — it has received every programming frame along the way.

> **IDs are hex**: motor 9 is `0x09`, motor 10 is `0x0A`, not `10`. For setups with up to 9 motors this distinction doesn't matter.

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
| `0xDD` | `0x00`–`0x64` | Go to absolute position (0=closed, 100=open) |
| `0xAA` | `0xAA` | Program ID and channel (see [Addressing motors](#addressing-motors-programming-ids-and-channels)) |

### Response frame (motor → ESP32, 10 bytes)

The motor usually sends this frame **twice**, back to back, after each status query. While a motor is running it often does not answer status queries; when it does, the frame shows the intermediate position and the running flag.

On this installation bytes of the reply are sometimes lost: the first copy
breaks off after a few bytes and the second copy follows directly, or a single
copy arrives with one byte missing (the checksum still covers the missing
byte, so the frame fails). The loss depends on the content: with byte 5 =
`0x18` (24 V) all replies were intact; with `0x17` (23 V) the `0x17` or the
`0x00` before it went missing on every query, identically at 2350, 2400 and
2460 baud, so the motor probably sends the frame that way. The bundled uartex
resynchronises on the second copy's header (see
[uartex](#uartex-bundled-patched-copy)); a single damaged copy is lost. A
frame that lost a `0x00` byte can still pass the XOR checksum, shifted by one
byte; the covers ignore frames with a voltage byte outside 16–40 or a position
above 100.

| Byte | Meaning |
|------|---------|
| 0 | `0xD8` — response header |
| 1 | Motor ID (`0x00` = not programmed) |
| 2 | Channel low byte |
| 3 | Channel high byte |
| 4–6 | Unknown. Byte 5 appears to be the supply voltage in volts (`0x17`/`0x18` observed); reported as `Awning N Voltage` |
| 7 | Position: 0–100 |
| 8 | Zone flags: bit 0 = motor running |
| 9 | Checksum: XOR of bytes 1–8 |

Example: `D8 02 01 00 00 18 00 00 00 1B` — motor 2, channel 1, position 0 (closed), idle.

Observed on motor 1 (zone bit 1 is set after a move; its meaning is not confirmed):

| Frame | State |
|---|---|
| `D8 01 01 00 00 18 00 64 02 7E` | Fully open: position 100, zone `0x02` |
| `D8 01 01 00 26 18 65 39 03 61` | Moving: position 57, zone `0x03` (running) |
| `D8 01 01 00 00 18 00 00 02 1A` | Fully closed: position 0, zone `0x02` |

### Position conventions

Raw position 0 is closed and 100 is open, confirmed on this installation in
both end positions and while moving. ESPHome uses the
same direction (`0.0 = closed`, `1.0 = open`), so the covers report
`raw / 100` and position commands send `position × 100`.

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
├── packages/
│   ├── motor.yaml             # template: cover + diagnostic sensors per motor
│   ├── group.yaml             # template: all-awnings broadcast cover
│   └── programming.yaml       # motor programming: mode, bus scan, status
├── secrets.yaml.example       # template for secrets.yaml
└── secrets.yaml               # your credentials (git-ignored)
tools/
├── esp_probe.py               # send frames / watch the bus over the native API
└── requirements.txt
.github/workflows/
└── validate.yml               # CI: esphome config + compile
```

### Entities

| Entity | Source | Category | Web UI group | Purpose |
|---|---|---|---|---|
| One cover per motor (`awning_N`, e.g. `Awning 1`) | `packages/motor.yaml` | — | Awnings | Open/close/stop/position, position feedback |
| `All Awnings` cover | `packages/group.yaml` | — | Awnings | Open/close/stop/position for every motor at once (broadcast, no feedback) |
| `RS485 Raw Command` text | `awning-motor.yaml` | — | Service | Send any frame, e.g. for addressing |
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

  poll_interval: 30s               # How often to query each motor

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
The cover polls its motor every `poll_interval` and only accepts response
frames that carry its `motor_id`. In the web interface the covers are sorted
by `motor_id` (`sorting_weight: ${motor_id}` in `motor.yaml`).

### Step 3 — All-awnings broadcast cover

A single `group_all` package entry covers all motors at once:

```yaml
  group_all:
    !include
      file: packages/group.yaml
      vars:
        group_name: "All Awnings"
        group_id: awning_all
```

This sends a broadcast frame with ID=0x00, ChanL=0x00, ChanH=0x00, which all motors obey. Channel-filtered broadcast (ID=0 with non-zero channel bytes) **does not work** — motors obey any ID=0 frame regardless of their programmed channel.

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
`channel_l` and `channel_h`. Nothing else needs to change: every cover carries
its own poll command and state filter.

All `motor_id` values must match the ID programmed into the physical motor hardware (see [Addressing motors](#addressing-motors-programming-ids-and-channels)).

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

- It turns **on** as soon as a valid status frame from that motor arrives:
  correct header, length and checksum (checked by the uartex hub) and the
  motor's own ID (checked by the cover's state filter).
- It turns **off** only after **3 poll intervals** without a reply (90 s with
  `poll_interval: 30s`). Motors often do not answer while they are moving, so a
  single missed poll is normal and must not mark a motor offline.
- After a reboot it is off until the motor answers its first poll (at most
  one poll interval).

Each valid frame restarts an internal script (`awning_N_online_watchdog`,
`mode: restart`) that waits 3 × the cover's update interval; the sensor
reports whether that script is still running. The timeout therefore follows
`poll_interval` automatically.

### Voltage and last seen

- `Awning N Voltage` shows byte 5 of the status frame in volts. `0x17`/`0x18`
  (23/24 V) have been observed; it is probably the motor's internal DC
  supply, but this is not confirmed. The value is sent only when it changes.
- `Awning N Last Seen` (device class `timestamp`) is the time of the motor's
  latest valid reply. The clock comes from Home Assistant (`time: platform:
  homeassistant`), so the sensor stays empty until Home Assistant has
  connected once after a reboot. To keep the history small it is updated
  when the motor comes back online and then at most every 5 minutes; when the
  motor goes offline it is set to the exact time of its last reply.

### RS485 error sensor

`RS485 Error` is uartex's built-in error sensor. It shows the result of the
last received frame: `None` for a valid frame, otherwise `Size Error`,
`Header Error`, `Checksum Error` and so on. It is only sent when the value
changes, so a single disturbed frame shows up as a short error followed by
`None` with the next valid frame. A value that stays on an error points to
wiring, termination or a second device sending on the bus.

Frames that break off (fewer than 10 bytes before the next header or a
pause) are discarded by uartex without an error, so `RS485 Error` can stay on
`None` while replies are lost. `Awning N Online` is the better indicator.

### uartex (bundled, patched copy)

[esphome/components/uartex](esphome/components/uartex) is a copy of
[eigger/espcomponents](https://github.com/eigger/espcomponents) tag `v260904`
(commit `0028799`, MIT licence, [LICENSE](esphome/components/uartex/LICENSE)),
reduced to the hub and the cover platform. The configuration loads it as a
local external component.

The only change is in `Parser::parse_byte()` in
[parser.cpp](esphome/components/uartex/parser.cpp), marked
`gumax-sunshade-esphome patch`. When the 10-byte window no longer starts with
the header `D8`, the original parser cleared the whole buffer, including a
header that had already arrived inside it. The motors send each reply twice,
so a broken first copy took the intact second copy with it. The patched
parser drops leading bytes until the buffer starts with a header again.

To update uartex: copy the hub files and `cover/` of the new tag over this
folder (without the upstream README), re-apply the patch, update the tag and
commit here, and re-test.

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

1. installs ESPHome **2026.9.1** (pinned, matching the tested version),
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
| No reply to any frame | A and B swapped; wrong `uart_flow_control_pin`; on the Waveshare board try the 120R jumper |
| Only `D8 00 …` replies | Motor not programmed, or factory-reset — see [Addressing motors](#addressing-motors-programming-ids-and-channels) |
| A cover never updates | `motor_id` or channel bytes in the YAML don't match the motor |
| Position always 0, running flag never set | End limits not set — see [End limits](#end-limits) |
| No status replies while a motor moves | Observed behaviour: motors often don't answer while running |
| `Awning N Online` stays off | No valid reply from that motor: check wiring, `motor_id` and channel bytes, as for a cover that never updates |
| `Awning N Online` off or flapping although the motor obeys commands | Replies are damaged (lost bytes; uartex drops such frames without an `RS485 Error`), see [Response frame](#response-frame-motor--esp32-10-bytes). Check the RS485 ground to motor pin 5, the connectors (moisture) and the 120R jumper. For a raw byte log add `debug:` to the `uart:` block (ESPHome [UART debugging](https://esphome.io/components/uart/#debugging)) |
| `Awning N Last Seen` stays empty | No time yet: Home Assistant has not connected since the last reboot |
| `RS485 Error` stays on an error | Disturbed frames: check A/B wiring, the 120R termination and that nothing else transmits on the bus |
| A cover shows "open" although its motor never answered | ESPHome covers have no "unknown" state; the cover keeps its start value until the first reply. Check `Awning N Online` |
| OTA upload refused | The uploader's `api_key` differs from the one in the firmware; see [Firmware updates (OTA)](#firmware-updates-ota) |
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
- uartex pinned to tag `v260904` instead of `@latest` with `refresh: always`, so a rebuild produces the same firmware. Since 1.3.0-beta.2 a patched copy of that tag is bundled instead, see [uartex](#uartex-bundled-patched-copy).
- `rx_priority: loop`: receiving no longer blocks the ESPHome main loop, which had produced "took a long time" warnings.
- Every received frame is logged at `DEBUG`, including replies no cover listens to (an unprogrammed motor answering as ID 0). The per-motor decode log moved from `INFO` to `DEBUG`.
- `poll_interval` now takes effect: a package default in `motor.yaml` shadowed the value from the main file.
- The package templates no longer have defaults for required values, so a missing `motor_id` fails the build instead of silently creating `awning_1`. The broadcast cover no longer takes channel vars the motors ignore.

**Web interface and operation (versions 1.1.0 to 1.2.0)**

- ESPHome web interface (`web_server` v3, local, Digest login, web OTA disabled) with the groups Awnings, Service and Diagnostics; see [Web interface](#web-interface).
- New per-motor online sensors, implemented in `packages/motor.yaml` with a watchdog script; see [Online sensors](#online-sensors).
- Fallback access point with captive portal; see [Fallback access point](#fallback-access-point).
- Diagnostic and service entities: WiFi signal, uptime, IP address, ESPHome version, restart and safe-mode buttons.
- `esphome: project:` name and version; see [Versioning](#versioning).
- CI workflow that validates and compiles the configuration; see [Continuous integration](#continuous-integration).
- `secrets.yaml.example` gained `web_username`, `web_password` and `ap_password`, and a syntactically valid placeholder `api_key`.
- Version 1.1.1: OTA uploads are encrypted with the API key instead of protected by `ota_password`, which is gone; see [Firmware updates (OTA)](#firmware-updates-ota).
- Version 1.2.0: per-motor `Voltage` and `Last Seen` sensors, the `RS485 Error` sensor, and Home Assistant as time source; see [Voltage and last seen](#voltage-and-last-seen).
- Version 1.2.1: raw position direction fixed (0 = closed, 100 = open) for feedback and position commands. Closed awnings had been shown as open after the next status poll; see [Position conventions](#position-conventions).
- Version 1.3.0-beta.1: motor programming from the web interface and Home Assistant (`Programming Mode`, `Scan Bus`, `Programming Status`, one `Awning N Program` button per motor) in the new `packages/programming.yaml`; see [Addressing motors](#addressing-motors-programming-ids-and-channels). Beta: scan and refusals tested, the programming run itself not yet.
- Version 1.3.0-beta.1 also: `All Awnings` offers a position (slider in the web interface and Home Assistant). uartex only enables position support with a `state_position`; the broadcast cover now has one that returns its own optimistic position. Tested: all motors move to the commanded position.
- Version 1.3.0-beta.2: bundled, patched uartex whose parser resynchronises on the next header, so the intact second copy of a damaged reply is used (tested: `Scan Bus` found a motor it had missed before); covers ignore implausible (byte-shifted) frames, which showed as `Voltage` 0 V. Not fixed: replies that arrive as a single copy with a missing byte are still lost, so `Awning N Online` can stay off while the motor obeys commands. An earlier note that programmed motors ignore the broadcast status query was wrong: their replies had been lost the same way.

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