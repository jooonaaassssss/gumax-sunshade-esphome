// Status replies of the awning motors (README → Response frame).
//
// A reply is 0xd8, ID, CL, CH, b3, voltage, b5, position, zone, checksum
// (XOR of ID … zone), usually sent twice.  On this installation replies
// often lose one byte between CH and the position byte.  The first bytes
// (0xd8, ID, CL, CH) and the last ones (position, zone, checksum) arrived
// intact in every recorded reply, so a damaged reply still identifies the
// motor, and its position can be read from the end of the frame.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace awning {

constexpr uint8_t RX_HEADER = 0xd8;

inline uint32_t make_key(uint8_t id, uint8_t chan_l, uint8_t chan_h) {
  return (uint32_t(id) << 16) | (uint32_t(chan_l) << 8) | uint32_t(chan_h);
}

// Motor key (ID, CL, CH) of a reply burst; false if it is not a reply.
inline bool reply_key(const std::vector<uint8_t> &burst, uint32_t &key) {
  if (burst.size() < 4 || burst[0] != RX_HEADER)
    return false;
  key = make_key(burst[1], burst[2], burst[3]);
  return true;
}

inline uint8_t xor_of(const uint8_t *data, size_t len) {
  uint8_t x = 0;
  for (size_t i = 0; i < len; i++)
    x ^= data[i];
  return x;
}

// 23/24 V observed; also used to tell the voltage byte from b3/b5.
inline bool is_voltage(uint8_t v) { return v >= 20 && v <= 30; }

struct Status {
  uint8_t position;  // 0 = closed … 100 = open
  uint8_t zone;      // bit 0: running
  uint8_t voltage;
};

// Status of a burst holding one damaged copy: 0xd8 + 8 bytes (one byte lost
// between CH and the position byte), followed only by 0x00 line noise.
// Returns false for anything else, including intact frames (uartex decodes
// those) and bursts with a second copy (the patched uartex resynchronises on
// it).  The lost byte m follows from the checksum.  The voltage byte shows
// where the loss was: if it is m or directly after CH, the loss was before
// the position byte and position/zone are intact; if it is one byte later,
// a byte after it was lost and the burst is ignored.
inline bool decode_damaged(const std::vector<uint8_t> &burst, Status &out) {
  if (burst.size() < 9 || burst[0] != RX_HEADER)
    return false;
  if (burst.size() >= 10 && xor_of(&burst[1], 8) == burst[9] && is_voltage(burst[5]) && burst[7] <= 100)
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
  if (r[5] > 100 || (r[6] & 0xfc) != 0)
    return false;
  out.position = r[5];
  out.zone = r[6];
  out.voltage = lost_voltage ? m : r[3];
  return true;
}

}  // namespace awning
