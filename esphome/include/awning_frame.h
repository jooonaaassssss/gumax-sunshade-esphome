// RS485 frames of the awning motors (README → RS485 protocol reference).
//
// Command (ESP → motor): 0x9a, ID, CL, CH, Cmd1, Cmd2, checksum.
// Reply (motor → ESP):   0xd8, ID, CL, CH, b3, voltage, b5, position, zone,
//                        checksum (XOR of ID … zone), usually sent twice.
//
// On this installation replies often lose bytes.  Three patterns were
// recorded:
//  - bytes at the junction of the two copies are lost (the end of the first
//    copy and/or the 0xd8 of the second), the rest of the second copy is
//    intact;
//  - while a motor moves, the second copy also loses a byte of its address
//    (ID, CL or CH); its end is intact;
//  - a single copy loses one byte between CH and the position byte.
// The first bytes of a reply (0xd8, ID, CL, CH) arrived intact in every
// recorded reply.  read_reply() handles all three patterns.
//
// No ESPHome dependencies: tests/awning_test.cpp builds this on the host.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace awning {

constexpr uint8_t RX_HEADER = 0xd8;
constexpr uint8_t TX_HEADER = 0x9a;

// Motor address (ID, CL, CH) as one value.
inline uint32_t make_key(uint8_t id, uint8_t chan_l, uint8_t chan_h) {
  return (uint32_t(id) << 16) | (uint32_t(chan_l) << 8) | uint32_t(chan_h);
}
inline uint8_t key_id(uint32_t key) { return uint8_t(key >> 16); }
inline uint8_t key_chan_l(uint32_t key) { return uint8_t(key >> 8); }
inline uint8_t key_chan_h(uint32_t key) { return uint8_t(key); }

inline uint32_t key_at(const std::vector<uint8_t> &burst, size_t offset) {
  return make_key(burst[offset], burst[offset + 1], burst[offset + 2]);
}

inline uint8_t xor_of(const uint8_t *data, size_t len) {
  uint8_t x = 0;
  for (size_t i = 0; i < len; i++)
    x ^= data[i];
  return x;
}

// 23/24 V observed.  Also tells the voltage byte from b3/b5 and rejects
// frames shifted by a lost byte.
inline bool is_voltage(uint8_t v) { return v >= 20 && v <= 30; }

// Zone byte values observed: 0x00 and 0x02 idle (0x02 after a move,
// meaning not confirmed), 0x03 running after open/close, 0x43 running to
// a target position (position command), both in either direction.  Bit 0
// is "running", bit 6 "to a target"; the zone does not tell the direction.
// Other values are treated as implausible (README → Response frame).
constexpr uint8_t ZONE_RUNNING = 0x01;
constexpr uint8_t ZONE_TARGET = 0x40;
inline bool is_zone(uint8_t z) { return z == 0x00 || z == 0x02 || z == 0x03 || z == 0x43; }

// Raw position byte (as the motor reports and takes it, 0–100) ↔ percent
// open (0 = closed … 100 = open, ESPHome's convention).  Which end the motor
// counts from depends on how its end limits were set: `reversed` means raw
// 0 is open (README → Position conventions).  The mapping is its own
// inverse.
inline uint8_t open_percent(uint8_t raw, bool reversed) { return reversed ? uint8_t(100 - raw) : raw; }

struct Status {
  uint8_t position;  // raw position byte 0–100, see open_percent()
  uint8_t zone;      // see ZONE_*
  uint8_t voltage;   // 0: unknown (damaged reply)
  bool running() const { return (zone & ZONE_RUNNING) != 0; }
  bool operator==(const Status &o) const {
    return position == o.position && zone == o.zone && voltage == o.voltage;
  }
};

// Plausible block body (ID CL CH b3 voltage b5 position zone).  b3 and b5
// were 0 in every reply of an idle motor and non-zero in every reply of a
// running one.  Together with the voltage and zone checks this rejects
// blocks shifted by a lost byte, whose XOR checksum can still match when
// 0x00 bytes move in.
inline bool plausible(const uint8_t *r) {
  if (!is_voltage(r[4]) || r[6] > 100 || !is_zone(r[7]))
    return false;
  const bool running = (r[7] & ZONE_RUNNING) != 0;
  return running ? (r[3] != 0 && r[5] != 0) : (r[3] == 0 && r[5] == 0);
}

// Status block at burst[offset]: ID CL CH b3 voltage b5 position zone
// checksum, with a matching checksum and plausible values.
inline bool block_at(const std::vector<uint8_t> &burst, size_t offset, Status &out) {
  if (offset + 9 > burst.size())
    return false;
  const uint8_t *r = &burst[offset];
  if (xor_of(r, 8) != r[8] || !plausible(r))
    return false;
  out = {r[6], r[7], r[4]};
  return true;
}

// Status of a burst holding one damaged copy: 0xd8 + 8 bytes (one byte lost
// between CH and the position byte), followed by at least one 0x00 of line
// noise and nothing else.  Returns false for anything else.  The lost byte m
// follows from the checksum.  The voltage byte shows where the loss was: if
// it is m or directly after CH, the loss was before the position byte and
// position/zone are intact; if it is one byte later, a byte after it was
// lost and the burst is ignored.
inline bool decode_damaged(const std::vector<uint8_t> &burst, Status &out) {
  if (burst.size() < 10 || burst[0] != RX_HEADER)
    return false;
  Status intact;
  if (block_at(burst, 1, intact))
    return false;  // intact frame
  for (size_t i = 1; i < burst.size(); i++) {
    if (burst[i] == RX_HEADER)
      return false;  // second copy
    if (i >= 9 && burst[i] != 0x00)
      return false;  // not line noise
  }
  const uint8_t *r = &burst[1];  // ID CL CH x y position zone
  const uint8_t m = xor_of(r, 7) ^ burst[8];
  const bool lost_voltage = is_voltage(m);    // loss at the voltage byte
  const bool lost_before = is_voltage(r[3]);  // loss at b3
  const bool lost_after = is_voltage(r[4]);   // loss behind the voltage byte
  if (lost_voltage + lost_before + lost_after != 1 || lost_after)
    return false;
  // The repaired body: ID CL CH b3 voltage b5 position zone.
  const uint8_t body[8] = {r[0], r[1], r[2], lost_voltage ? r[3] : m, lost_voltage ? m : r[3], r[4], r[5], r[6]};
  if (!plausible(body))
    return false;
  // The voltage is not reported: it is right for one lost byte, but often
  // wrong when a copy lost two (position and zone stay right).
  out = {body[6], body[7], 0};
  return true;
}

// Status from the end of a reply burst of the motor with address `key`:
// the last copy's b3 voltage b5 position zone checksum, followed only by
// 0x00 line noise, checked against the known address.  Covers a last copy
// that lost bytes of its start (0xd8, ID, CL or CH).
inline bool tail_block(const std::vector<uint8_t> &burst, uint32_t key, Status &out) {
  size_t last = burst.size();  // end of the last non-zero byte
  while (last > 0 && burst[last - 1] == 0x00)
    last--;
  // The checksum (and zone) may be 0x00 themselves.
  for (size_t end = last; end <= burst.size() && end <= last + 2; end++) {
    if (end < 4 + 6)
      continue;  // must follow 0xd8 ID CL CH
    const uint8_t *t = &burst[end - 6];  // b3 voltage b5 position zone checksum
    const uint8_t body[8] = {key_id(key), key_chan_l(key), key_chan_h(key), t[0], t[1], t[2], t[3], t[4]};
    if (xor_of(body, 8) != t[5] || !plausible(body))
      continue;
    out = {t[3], t[4], t[1]};
    return true;
  }
  return false;
}

enum class Seen : uint8_t {
  NO,        // not a reply of this motor
  YES,       // reply of this motor, status unreadable or contradictory
  STATUS,    // status from an intact block
  REPAIRED,  // status from a damaged reply (tail_block, decode_damaged)
};

// What a reply burst (one burst of the raw receive tap) says about the
// motor with address `key`.  The motor is recognised by a burst that starts
// with 0xd8 and its address, even if the rest is damaged.  Its status is
// read from
//  - every intact block with its address that follows a 0xd8 or, if the
//    burst starts with its address, any intact block with its address (the
//    second copy whose 0xd8 was lost),
//  - if the burst starts with its address: the end of the burst
//    (tail_block),
// and used only if all of these readings agree.  A single damaged copy
// (decode_damaged) is the last resort.
inline Seen read_reply(const std::vector<uint8_t> &burst, uint32_t key, Status &out) {
  const bool header = burst.size() >= 4 && burst[0] == RX_HEADER && key_at(burst, 1) == key;
  bool found = false, contradiction = false;
  Seen seen = header ? Seen::YES : Seen::NO;
  auto add = [&](const Status &status, Seen kind) {
    if (!found) {
      out = status;
      seen = kind;
      found = true;
    } else if (!(status == out)) {
      contradiction = true;
    }
  };
  Status status;
  for (size_t offset = 1; offset + 9 <= burst.size(); offset++) {
    if (key_at(burst, offset) == key && (burst[offset - 1] == RX_HEADER || header) &&
        block_at(burst, offset, status))
      add(status, Seen::STATUS);
  }
  if (header && tail_block(burst, key, status))
    add(status, Seen::REPAIRED);
  if (contradiction)
    return Seen::YES;
  if (found)
    return seen;
  if (header && decode_damaged(burst, out))
    return Seen::REPAIRED;
  return seen;
}
// Addresses of all motors that answered in a burst: the address after a
// leading 0xd8 (even if the rest is damaged) and the address of every
// intact block that follows a 0xd8.
inline std::vector<uint32_t> reply_keys(const std::vector<uint8_t> &burst) {
  std::vector<uint32_t> keys;
  auto add = [&keys](uint32_t key) {
    for (const uint32_t k : keys)
      if (k == key)
        return;
    keys.push_back(key);
  };
  if (burst.size() >= 4 && burst[0] == RX_HEADER)
    add(key_at(burst, 1));
  Status status;
  for (size_t offset = 1; offset + 9 <= burst.size(); offset++)
    if (burst[offset - 1] == RX_HEADER && block_at(burst, offset, status))
      add(key_at(burst, offset));
  return keys;
}

enum class Command : uint8_t { OTHER, OPEN, CLOSE, STOP, POSITION, STATUS, PROGRAM };

struct CommandFrame {
  uint32_t key;     // ID 0: broadcast to every motor
  Command command;
  uint8_t target;   // raw position for Command::POSITION, see open_percent()
  // The motor acts on this frame.  The motors ignore position frames sent
  // to ID 0 (README → Command frame).
  bool addresses(uint32_t motor) const {
    return key == motor || (key_id(key) == 0 && command != Command::POSITION);
  }
};

// Command payload: ID CL CH Cmd1 Cmd2 (without header and checksum).
inline CommandFrame parse_command(const uint8_t *payload) {
  CommandFrame f{make_key(payload[0], payload[1], payload[2]), Command::OTHER, 0};
  const uint8_t cmd1 = payload[3], cmd2 = payload[4];
  if (cmd1 == 0x0a && cmd2 == 0xdd)
    f.command = Command::OPEN;
  else if (cmd1 == 0x0a && cmd2 == 0xee)
    f.command = Command::CLOSE;
  else if (cmd1 == 0x0a && cmd2 == 0xcc)
    f.command = Command::STOP;
  else if (cmd1 == 0xdd && cmd2 <= 100) {
    f.command = Command::POSITION;
    f.target = cmd2;
  } else if (cmd1 == 0xcc && cmd2 == 0x00)
    f.command = Command::STATUS;
  else if (cmd1 == 0xaa && cmd2 == 0xaa)
    f.command = Command::PROGRAM;
  return f;
}

// Command frame as sent on the bus: header, 5 payload bytes, checksum.
inline bool parse_sent_command(const uint8_t *frame, size_t len, CommandFrame &out) {
  if (len != 7 || frame[0] != TX_HEADER)
    return false;
  out = parse_command(frame + 1);
  return true;
}

// "9a ID CL CH Cmd1 Cmd2" or "ID CL CH Cmd1 Cmd2", hex bytes separated by
// spaces.  Returns the 5 payload bytes, or an empty vector if the text is
// not such a frame.
inline std::vector<uint8_t> parse_hex_payload(const std::string &text) {
  std::vector<uint8_t> bytes;
  size_t i = 0;
  while (i < text.size()) {
    if (text[i] == ' ') {
      i++;
      continue;
    }
    unsigned value = 0;
    size_t digits = 0;
    for (; i < text.size() && text[i] != ' '; i++, digits++) {
      const char c = text[i];
      unsigned d;
      if (c >= '0' && c <= '9')
        d = c - '0';
      else if (c >= 'a' && c <= 'f')
        d = c - 'a' + 10;
      else if (c >= 'A' && c <= 'F')
        d = c - 'A' + 10;
      else
        return {};
      value = value * 16 + d;
    }
    if (digits == 0 || digits > 2)
      return {};
    bytes.push_back(uint8_t(value));
  }
  if (bytes.size() == 6 && bytes[0] == TX_HEADER)
    bytes.erase(bytes.begin());
  if (bytes.size() != 5)
    return {};
  return bytes;
}

}  // namespace awning
