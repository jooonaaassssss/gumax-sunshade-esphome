// Host test for the bus logic in esphome/include (no ESPHome needed):
//   g++ -std=c++17 -Wall -Wextra -Werror -I esphome/include tests/awning_test.cpp -o awning_test && ./awning_test
// Run by CI (.github/workflows/validate.yml).  The bursts are recorded from
// the installation (README → Response frame).
#include "awning_frame.h"
#include "awning_motion.h"

#include <cstdio>
#include <random>
#include <sstream>
#include <string>
#include <vector>

using namespace awning;

static int failures = 0;

#define CHECK(cond)                                               \
  do {                                                            \
    if (!(cond)) {                                                \
      failures++;                                                 \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    }                                                             \
  } while (0)

static std::vector<uint8_t> hex(const std::string &s) {
  std::vector<uint8_t> v;
  std::istringstream in(s);
  std::string t;
  while (in >> t)
    v.push_back(uint8_t(std::stoul(t, nullptr, 16)));
  return v;
}

static const uint32_t M1 = make_key(1, 1, 0);
static const uint32_t M2 = make_key(2, 1, 0);

static void check_reply(const char *burst, uint32_t key, Seen seen, int position = -1, int zone = -1,
                        int voltage = -1) {
  Status s{};
  const Seen got = read_reply(hex(burst), key, s);
  bool ok = got == seen;
  if (ok && (seen == Seen::STATUS || seen == Seen::REPAIRED))
    ok = s.position == position && s.zone == zone && s.voltage == voltage;
  if (!ok) {
    failures++;
    std::printf("FAIL read_reply(%s): seen %d pos %d zone %d %dV\n", burst, int(got), s.position, s.zone,
                s.voltage);
  }
}

static void test_read_reply() {
  // Intact replies, single and doubled.
  check_reply("D8 02 01 00 00 18 00 00 00 1B", M2, Seen::STATUS, 0, 0, 24);
  check_reply("D8 01 01 00 00 18 00 64 02 7E D8 01 01 00 00 18 00 64 02 7E", M1, Seen::STATUS, 100, 2, 24);
  check_reply("D8 01 01 00 26 18 65 39 03 61", M1, Seen::STATUS, 57, 3, 24);
  // While moving (zone 0x43 opening, 0x03 closing); first copy cut off.
  check_reply("D8 01 01 00 19 17 3F 02 D8 01 01 00 1A 17 40 02 43 0C 00 00", M1, Seen::STATUS, 2, 0x43, 23);
  check_reply("D8 01 01 00 1C 17 4F 0F D8 01 01 00 1A 17 4D 0F 43 0C 00 00", M1, Seen::STATUS, 15, 0x43, 23);
  check_reply("D8 01 01 00 11 17 3E 11 D8 01 01 00 0D 17 40 11 03 48 00 00", M1, Seen::STATUS, 17, 3, 23);
  // Junction noise 0xd8 before the second copy.
  check_reply("D8 02 01 00 00 17 00 00 02 16 D8 D8 02 01 00 00 17 00 00 02 16 00 00", M2, Seen::STATUS, 0, 2, 23);
  // While moving, the second copy lost a byte of its address.
  check_reply("D8 02 01 00 22 17 D8 01 00 22 17 67 08 43 1A 00 00", M2, Seen::REPAIRED, 8, 0x43, 23);
  check_reply("D8 02 01 00 1A 17 D8 01 00 1A 17 3E 15 43 66 00 00", M2, Seen::REPAIRED, 21, 0x43, 23);
  check_reply("D8 02 01 00 19 17 D8 01 00 19 17 65 10 03 7B 00 00", M2, Seen::REPAIRED, 16, 3, 23);
  check_reply("D8 01 01 00 10 17 4E D8 01 00 12 17 4D 04 03 4F 00 00", M1, Seen::REPAIRED, 4, 3, 23);
  // First copy cut off, second copy intact.
  check_reply("D8 01 01 00 00 17 00 00 D8 01 01 00 00 17 00 00 02 15 00 00", M1, Seen::STATUS, 0, 2, 23);
  check_reply("D8 02 01 00 00 17 00 D8 02 01 00 00 17 00 00 02 16 00 00", M2, Seen::STATUS, 0, 2, 23);
  check_reply("D8 01 01 00 00 17 D8 01 01 00 00 17 00 00 00 17 00 00", M1, Seen::STATUS, 0, 0, 23);
  // Bytes lost at the junction, including the 0xd8 of the second copy.
  check_reply("D8 01 01 00 00 17 00 00 02 01 01 00 00 17 00 00 02 15 00 00", M1, Seen::STATUS, 0, 2, 23);
  // One copy with one byte lost before the position byte (voltage unknown).
  check_reply("D8 01 01 00 00 00 4B 02 5E 00 00", M1, Seen::REPAIRED, 75, 2, 0);
  check_reply("D8 02 01 00 00 00 4B 02 5D 00 00", M2, Seen::REPAIRED, 75, 2, 0);
  check_reply("D8 01 01 00 17 65 11 03 56 00 00", M1, Seen::REPAIRED, 17, 3, 0);
  check_reply("D8 02 01 00 00 00 0F 02 19 00 00", M2, Seen::REPAIRED, 15, 2, 0);
  check_reply("D8 01 01 00 00 00 0F 02 1A 00 00", M1, Seen::REPAIRED, 15, 2, 0);
  check_reply("D8 01 01 00 00 00 00 00 17 00 00", M1, Seen::REPAIRED, 0, 0, 0);
  check_reply("D8 02 01 00 17 00 00 02 16 00", M2, Seen::REPAIRED, 0, 2, 0);
  check_reply("D8 02 01 00 17 00 4B 02 5D 00 00", M2, Seen::REPAIRED, 75, 2, 0);
  // Recognised, but the status cannot be read safely.
  check_reply("D8 01 01 00", M1, Seen::YES);
  check_reply("D8 01 01 00 00 17 00 02 1A 00 00", M1, Seen::YES);  // position byte lost
  // Unknown zone bits are implausible.
  check_reply("D8 01 01 00 00 17 00 00 82 95", M1, Seen::YES);
  // Other motor, line noise.
  check_reply("D8 02 01 00 00 18 00 00 00 1B", M1, Seen::NO);
  check_reply("00", M1, Seen::NO);
  check_reply("00 00 00 00 00 00 00 00 00 00 00 00", M1, Seen::NO);
  // An intact block of motor 1 without a preceding 0xd8 in a reply of
  // motor 2 is not used for motor 1.
  check_reply("D8 02 01 00 00 01 01 00 00 17 00 00 02 15 00", M1, Seen::NO);
}

// Simulated replies (one or two copies, junction noise, trailing 0x00) with
// 0–3 lost bytes at random places.  read_reply may give up, but what the
// firmware uses (position, running, opening, voltage unless unknown) must be
// right: always with no lost byte, all but a tiny residue otherwise (an XOR
// checksum cannot tell every damaged copy from a valid one).  A burst must
// never give a status for another motor.
static void test_lost_bytes() {
  std::mt19937 rng(1);
  const uint32_t keys[] = {M1, M2, make_key(5, 2, 0), make_key(0, 0, 0)};
  long total[4] = {}, decoded[4] = {}, wrong[4] = {}, foreign = 0;
  for (int i = 0; i < 400000; i++) {
    const size_t k = rng() % 4;
    const uint32_t key = keys[k], other = keys[(k + 1 + rng() % 3) % 4];
    const bool moving = rng() % 2;
    const uint8_t position = rng() % 101;
    const uint8_t zone = moving ? (rng() % 2 ? 0x43 : 0x03) : (rng() % 2 ? 0x02 : 0x00);
    const uint8_t voltage = 0x17 + rng() % 2;
    const uint8_t b3 = moving ? 1 + rng() % 0x3f : 0, b5 = moving ? 1 + rng() % 0x7f : 0;
    std::vector<uint8_t> frame{RX_HEADER, key_id(key), key_chan_l(key), key_chan_h(key), b3, voltage, b5,
                               position,  zone};
    frame.push_back(xor_of(&frame[1], 8));
    std::vector<uint8_t> burst = frame;
    if (rng() % 4 != 0) {  // usually two copies
      const unsigned junction = rng() % 3;
      if (junction == 1)
        burst.push_back(0x00);
      if (junction == 2)
        burst.push_back(RX_HEADER);
      burst.insert(burst.end(), frame.begin(), frame.end());
    }
    burst.push_back(0x00);
    burst.push_back(0x00);
    const int lost = rng() % 4;
    for (int n = 0; n < lost; n++)
      burst.erase(burst.begin() + rng() % burst.size());
    total[lost]++;
    Status s{};
    const Seen seen = read_reply(burst, key, s);
    if (seen == Seen::STATUS || seen == Seen::REPAIRED) {
      decoded[lost]++;
      const Status want{position, zone, voltage};
      if (s.position != position || s.running() != want.running() || s.opening() != want.opening() ||
          (s.voltage != 0 && s.voltage != voltage))
        wrong[lost]++;
    }
    const Seen seen_other = read_reply(burst, other, s);
    if (seen_other == Seen::STATUS || seen_other == Seen::REPAIRED)
      foreign++;
  }
  for (int lost = 0; lost < 4; lost++)
    std::printf("lost %d byte(s): %ld replies, %.1f %% decoded, %ld wrong\n", lost, total[lost],
                100.0 * decoded[lost] / total[lost], wrong[lost]);
  std::printf("status for another motor: %ld\n", foreign);
  CHECK(decoded[0] == total[0] && wrong[0] == 0);
  CHECK(wrong[1] * 50000 <= total[1]);                         // at most 0.002 %
  CHECK((wrong[2] + wrong[3]) * 5000 <= total[2] + total[3]);  // at most 0.02 %
  CHECK(foreign == 0);
}

static void test_reply_keys() {
  CHECK(reply_keys(hex("D8 05 02 00 00")) == std::vector<uint32_t>{make_key(5, 2, 0)});
  CHECK(reply_keys(hex("00 00")).empty());
  CHECK(reply_keys(hex("D8 01 01 00 00 17 00 00 02 01 01 00 00 17 00 00 02 15 00 00")) ==
        std::vector<uint32_t>{M1});
  // Two motors answering one broadcast query.
  CHECK((reply_keys(hex("D8 01 01 00 00 17 D8 02 01 00 00 18 00 00 00 1B")) == std::vector<uint32_t>{M1, M2}));
}

static void test_commands() {
  const auto open = parse_hex_payload("9a 01 01 00 0a dd");
  CHECK(open == hex("01 01 00 0a dd"));
  CHECK(parse_hex_payload("01 01 00 0A DD") == hex("01 01 00 0a dd"));
  CHECK(parse_hex_payload("  9a  02 01 00 cc 00 ") == hex("02 01 00 cc 00"));
  CHECK(parse_hex_payload("9a 01 01").empty());
  CHECK(parse_hex_payload("9a 01 01 00 0a dd 00").empty());
  CHECK(parse_hex_payload("9a 01 01 00 0a zz").empty());
  CHECK(parse_hex_payload("9a 101 01 00 0a dd").empty());
  CHECK(parse_hex_payload("").empty());

  CHECK(parse_command(open.data()).command == Command::OPEN);
  CHECK(parse_command(hex("01 01 00 0a ee").data()).command == Command::CLOSE);
  CHECK(parse_command(hex("01 01 00 0a cc").data()).command == Command::STOP);
  const auto position = parse_command(hex("01 01 00 dd 4b").data());
  CHECK(position.command == Command::POSITION && position.target == 75);
  CHECK(parse_command(hex("01 01 00 dd 65").data()).command == Command::OTHER);
  CHECK(parse_command(hex("01 01 00 cc 00").data()).command == Command::STATUS);
  CHECK(parse_command(hex("02 01 00 aa aa").data()).command == Command::PROGRAM);

  CommandFrame f;
  CHECK(parse_sent_command(hex("9a 00 00 00 0a dd dd").data(), 7, f));
  CHECK(f.command == Command::OPEN && f.addresses(M1) && f.addresses(M2));
  CHECK(parse_sent_command(hex("9a 01 01 00 0a dd d7").data(), 7, f));
  CHECK(f.addresses(M1) && !f.addresses(M2));
  CHECK(!parse_sent_command(hex("d8 01 01 00 0a dd d7").data(), 7, f));
  // The motors ignore position frames sent to ID 0.
  CHECK(parse_sent_command(hex("9a 00 00 00 dd 32 ef").data(), 7, f));
  CHECK(f.command == Command::POSITION && !f.addresses(M1) && !f.addresses(M2));
  CHECK(parse_sent_command(hex("9a 00 01 00 cc 00 cd").data(), 7, f));
  CHECK(f.command == Command::STATUS && f.addresses(M1));
}

static_assert(duration_ms("500ms") == 500, "ms");
static_assert(duration_ms("15s") == 15000, "s");
static_assert(duration_ms("5min") == 300000, "min");
static_assert(duration_ms("1h") == 3600000, "h");
static_assert(duration_ms("15") == 0 && duration_ms("s") == 0 && duration_ms("15 s") == 0, "invalid");

static Timing timing() {
  Timing t{};
  t.poll_ms = 15000;
  t.fast_poll_ms = 3000;
  t.offline_poll_ms = 300000;
  t.offline_ms = 90000;
  return t;
}

static Status st(uint8_t position, bool running) { return {position, uint8_t(running ? 0x03 : 0x02), 23}; }
static Status opening(uint8_t position) { return {position, 0x43, 23}; }

static void test_polling() {
  MotorState m(timing());
  // Never answered: offline, first poll at once, then every offline_poll_ms.
  CHECK(!m.online(0));
  CHECK(m.poll_due(0));
  m.poll_queued(0);
  CHECK(!m.poll_due(100));  // one query in the queue at a time
  m.poll_sent(1000);
  CHECK(!m.poll_due(2000));
  CHECK(m.poll_interval(2000) == 300000);
  // A reply: online, normal interval.
  m.reply(1200);
  m.status(1200, st(0, false));
  CHECK(m.online(1200));
  CHECK(m.poll_interval(1200) == 15000);
  CHECK(!m.poll_due(15999));
  CHECK(m.poll_due(16000));
  // A queued query that is never sent is dropped after poll_timeout_ms.
  m.poll_queued(16000);
  CHECK(!m.poll_due(45999));
  CHECK(m.poll_due(46000));
  // Offline after offline_ms without a reply.
  CHECK(m.online(91199));
  CHECK(!m.online(91200));
  CHECK(m.poll_interval(91200) == 300000);
}

static void test_motion() {
  MotorState m(timing());
  m.reply(0);
  m.status(0, st(0, false));
  // Open: opening at once, fast polling.
  m.command(1000, Command::OPEN, 0, 0);
  CHECK(m.motion() == Motion::OPENING && m.moving());
  CHECK(m.poll_interval(1000) == 3000);
  // Idle at the start position within the start grace: not started yet.
  m.reply(3000);
  m.status(3000, st(0, false));
  CHECK(m.motion() == Motion::OPENING && m.moving());
  m.status(6000, opening(30));
  CHECK(m.motion() == Motion::OPENING && m.position() == 30);
  // Idle at the end position: done.
  m.status(40000, st(100, false));
  CHECK(m.motion() == Motion::IDLE && !m.moving() && m.position() == 100);
  CHECK(m.poll_interval(40000) == 15000);

  // Close, a running reply, then silence: ends after max_move_ms.
  m.command(50000, Command::CLOSE, 0, 100);
  CHECK(m.motion() == Motion::CLOSING);
  m.status(53000, st(57, true));
  CHECK(m.motion() == Motion::CLOSING);
  CHECK(!m.tick(53000 + 119999));
  CHECK(m.tick(53000 + 120000));
  CHECK(m.motion() == Motion::IDLE && !m.moving());

  // Idle at the start position after the start grace ends the move
  // (e.g. a position command to the current position).
  m.status(200000, st(40, false));
  m.command(201000, Command::POSITION, 40, 40);
  CHECK(m.motion() == Motion::IDLE && m.moving());
  m.status(203000, st(40, false));
  CHECK(m.moving());
  m.status(206000, st(40, false));
  CHECK(!m.moving());

  // Position commands: direction from the known position.
  m.command(210000, Command::POSITION, 75, 0);
  CHECK(m.motion() == Motion::OPENING);
  m.command(211000, Command::POSITION, 10, 0);
  CHECK(m.motion() == Motion::CLOSING);
  // Stop: idle at once, polled until a reply shows the final position.
  m.command(212000, Command::STOP, 0, 0);
  CHECK(m.motion() == Motion::IDLE && m.moving());
  m.status(213000, st(35, false));
  CHECK(!m.moving() && m.position() == 35);

  // Moved by the remote control: detected by a "running" reply.
  m.status(300000, opening(45));
  CHECK(m.motion() == Motion::OPENING && m.moving());
  m.status(303000, opening(60));
  CHECK(m.motion() == Motion::OPENING);
  m.status(306000, st(50, true));
  CHECK(m.motion() == Motion::CLOSING);
  m.status(309000, st(0, false));
  CHECK(m.motion() == Motion::IDLE && !m.moving());

  // Remote control, first reply already shows the opening bit.
  m.status(400000, opening(0));
  CHECK(m.motion() == Motion::OPENING && m.moving());
  m.status(403000, opening(12));
  CHECK(m.motion() == Motion::OPENING);
  m.status(406000, st(12, false));
  CHECK(m.motion() == Motion::IDLE && !m.moving());

  // Closing by the remote control, first running reply at the old position.
  m.status(500000, st(12, true));
  CHECK(m.motion() == Motion::CLOSING && m.moving());
}

// Online is latched off by tick(), so millis() wrapping around after 49.7
// days does not bring a long-silent motor back.
static void test_online_wrap() {
  MotorState m(timing());
  m.reply(1000);
  CHECK(m.online(1000));
  m.tick(50000);
  CHECK(m.online(50000));
  m.tick(91000);
  CHECK(!m.online(91000));
  const uint32_t wrapped = 1000 + 10;  // 2^32 ms later
  m.tick(wrapped);
  CHECK(!m.online(wrapped));
  CHECK(m.poll_interval(wrapped) == 300000);
}

static void test_unknown_motor() {
  MotorState m(timing());
  // A motor that never answered (not connected): commands show no motion
  // and do not start fast polling.
  m.command(1000, Command::POSITION, 20, 80);
  CHECK(m.motion() == Motion::IDLE && !m.moving());
  m.command(2000, Command::OPEN, 0, 80);
  CHECK(m.motion() == Motion::IDLE && !m.moving());
  CHECK(m.poll_interval(2000) == 300000);
  CHECK(!m.has_position());
  // Once it answers, commands count, with the direction from the shown
  // position until a status has been read.
  m.reply(3000);
  m.command(4000, Command::POSITION, 20, 80);
  CHECK(m.motion() == Motion::CLOSING && m.moving());
  CHECK(m.poll_interval(4000) == 3000);
}

int main() {
  test_read_reply();
  test_lost_bytes();
  test_reply_keys();
  test_commands();
  test_polling();
  test_motion();
  test_unknown_motor();
  test_online_wrap();
  std::printf("%d failure(s)\n", failures);
  return failures != 0;
}
