// Per-motor state: online, motion (opening/closing/idle) and poll schedule.
//
// Driven by bus events (commands sent, replies received) and the time in
// milliseconds; no ESPHome dependencies, so tests/awning_test.cpp builds
// it on the host.  include/awning_motor.h connects it to the entities.
#pragma once

#include <cstdint>

#include "awning_frame.h"

namespace awning {

// "500ms", "15s", "5min", "1h" → milliseconds; 0 for anything else.
// constexpr, so a bad substitution fails the build (static_assert).
constexpr uint32_t duration_ms(const char *text) {
  uint32_t value = 0;
  const char *p = text;
  if (*p < '0' || *p > '9')
    return 0;
  for (; *p >= '0' && *p <= '9'; p++)
    value = value * 10 + uint32_t(*p - '0');
  const auto is = [](const char *a, const char *b) {
    for (; *a && *a == *b; a++, b++) {
    }
    return *a == *b;
  };
  if (is(p, "ms"))
    return value;
  if (is(p, "s"))
    return value * 1000;
  if (is(p, "min"))
    return value * 60000;
  if (is(p, "h"))
    return value * 3600000;
  return 0;
}

struct Timing {
  uint32_t poll_ms;          // status query while idle
  uint32_t fast_poll_ms;     // status query while moving
  uint32_t offline_poll_ms;  // status query while offline
  uint32_t offline_ms;       // offline after this long without a reply
  // After a command, a reply "idle at the start position" does not end the
  // move yet: the motor may not have started.
  uint32_t start_grace_ms = 5000;
  // A move ends after this long without a command or a "running" reply,
  // e.g. when the motor stops answering.
  uint32_t max_move_ms = 120000;
  // A queued status query that was not sent within this time is dropped.
  uint32_t poll_timeout_ms = 30000;
};

enum class Motion : uint8_t { IDLE, OPENING, CLOSING };

class MotorState {
 public:
  explicit MotorState(const Timing &timing) : timing_(timing) {}

  // A command for this motor was sent.  target: percent open for
  // Command::POSITION.  current: the position shown before (percent open),
  // used for the direction when no status has been read yet.
  // Ignored for a motor that has not answered since boot (e.g. not
  // connected): no reply would end the motion it shows.
  void command(uint32_t now, Command cmd, uint8_t target, uint8_t current) {
    if (!seen_)
      return;
    const uint8_t from = has_position_ ? position_ : current;
    switch (cmd) {
      case Command::OPEN:
        start_(now, from, from < 100 ? Motion::OPENING : Motion::IDLE, true);
        break;
      case Command::CLOSE:
        start_(now, from, from > 0 ? Motion::CLOSING : Motion::IDLE, true);
        break;
      case Command::POSITION:
        start_(now, from, target > from ? Motion::OPENING : target < from ? Motion::CLOSING : Motion::IDLE, true);
        break;
      case Command::STOP:
        // Stops at once; keep polling until a reply shows the final position.
        start_(now, from, Motion::IDLE, false);
        break;
      default:
        break;
    }
  }

  // Any reply of this motor, readable or not.
  void reply(uint32_t now) {
    seen_ = true;
    online_ = true;
    last_reply_ = now;
  }

  // A decoded status of this motor, with the position already converted to
  // percent open (open_percent()).
  void status(uint32_t now, const Status &s) {
    const bool had_position = has_position_;
    const uint8_t previous = position_;
    has_position_ = true;
    position_ = s.position;
    if (s.running()) {
      // Also a move started by the remote control.  The zone does not tell
      // the direction (awning_frame.h), the position change does.  Without
      // one, keep the direction of the command; for a move by the remote
      // control, an end position allows only one direction.
      active_ = true;
      last_activity_ = now;
      if (had_position && s.position > previous)
        motion_ = Motion::OPENING;
      else if (had_position && s.position < previous)
        motion_ = Motion::CLOSING;
      else if (motion_ == Motion::IDLE && s.position == 0)
        motion_ = Motion::OPENING;
      else if (motion_ == Motion::IDLE && s.position == 100)
        motion_ = Motion::CLOSING;
      return;
    }
    if (active_ && grace_ && now - command_time_ < timing_.start_grace_ms && s.position == start_position_)
      return;  // not started yet
    active_ = false;
    motion_ = Motion::IDLE;
  }

  // Ends a move without replies after max_move_ms and latches offline (so
  // that millis() wrapping after 49.7 days cannot bring back an old reply).
  // Call regularly.  True if the motion changed.
  bool tick(uint32_t now) {
    if (online_ && now - last_reply_ >= timing_.offline_ms)
      online_ = false;
    if (!active_ || now - last_activity_ < timing_.max_move_ms)
      return false;
    active_ = false;
    const bool changed = motion_ != Motion::IDLE;
    motion_ = Motion::IDLE;
    return changed;
  }

  bool online(uint32_t now) const { return online_ && now - last_reply_ < timing_.offline_ms; }
  bool seen() const { return seen_; }

  // Fast polling while the motor moves (it may be silent, and so offline,
  // while running).  Motors that never answered, e.g. ones not connected,
  // never move here (see command()) and are polled at the offline interval.
  uint32_t poll_interval(uint32_t now) const {
    if (active_)
      return timing_.fast_poll_ms;
    return online(now) ? timing_.poll_ms : timing_.offline_poll_ms;
  }

  // At most one status query per motor is queued at a time.
  bool poll_due(uint32_t now) const {
    if (pending_ && now - queued_time_ < timing_.poll_timeout_ms)
      return false;
    return !polled_ || now - poll_time_ >= poll_interval(now);
  }
  void poll_queued(uint32_t now) {
    pending_ = true;
    queued_time_ = now;
  }
  // A status query for this motor went out on the bus (any sender).
  void poll_sent(uint32_t now) {
    pending_ = false;
    polled_ = true;
    poll_time_ = now;
  }

  Motion motion() const { return motion_; }
  bool moving() const { return active_; }
  bool has_position() const { return has_position_; }
  uint8_t position() const { return position_; }

 private:
  void start_(uint32_t now, uint8_t from, Motion motion, bool grace) {
    active_ = true;
    grace_ = grace;
    motion_ = motion;
    command_time_ = now;
    last_activity_ = now;
    start_position_ = from;
  }

  Timing timing_;
  bool seen_{false};
  bool online_{false};
  uint32_t last_reply_{0};
  bool has_position_{false};
  uint8_t position_{0};
  bool active_{false};
  bool grace_{false};
  Motion motion_{Motion::IDLE};
  uint32_t command_time_{0};
  uint32_t last_activity_{0};
  uint8_t start_position_{0};
  bool pending_{false};
  bool polled_{false};
  uint32_t queued_time_{0};
  uint32_t poll_time_{0};
};

}  // namespace awning
