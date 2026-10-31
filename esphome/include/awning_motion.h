// Per-motor state: online, position, counting direction, motion
// (opening/closing/idle), calibration after a power loss and poll schedule.
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
  // move yet: the motor may not have started.  Position changes in this
  // time do not count for the counting direction (the motor may still run
  // the other way).
  uint32_t start_grace_ms = 5000;
  // A move ends after this long without a command or a "running" reply,
  // e.g. when the motor stops answering.
  uint32_t max_move_ms = 120000;
  // A queued status query that was not sent within this time is dropped,
  // as is a calibration whose command was not sent.
  uint32_t poll_timeout_ms = 30000;
};

enum class Motion : uint8_t { IDLE, OPENING, CLOSING };

// Counting direction (README → Position conventions).  After a power loss
// a motor has lost its position (zone bit 1 clear, position 0).  It counts
// from 0 at the first end limit it reaches and has its position again at
// the second one, where it reports raw 100.  So raw 0 is open or closed
// depending on which end limit it reached first, per motor and per power
// loss.  MotorState learns it:
//  - when the motor finds its position at the end of an open (raw 100 =
//    open) or close (raw 100 = closed) command,
//  - from the raw position change during open and close commands (after
//    start_grace_ms), if two readings in a row disagree with the current
//    direction.
// `reversed` (raw 0 = open) is kept by the caller across reboots.
class MotorState {
 public:
  explicit MotorState(const Timing &timing, bool reversed = false) : timing_(timing), reversed_(reversed) {}

  // A command for this motor was sent.  raw_target: raw position for
  // Command::POSITION.  current: the position shown before (percent open),
  // used for the direction when no status has been read yet.  direct: sent
  // to this motor's address (not a broadcast).
  // Ignored for a motor that has not answered since boot (e.g. not
  // connected): no reply would end the motion it shows.
  void command(uint32_t now, Command cmd, uint8_t raw_target, uint8_t current, bool direct = true) {
    if (cmd != Command::OPEN && cmd != Command::CLOSE && cmd != Command::STOP && cmd != Command::POSITION)
      return;  // status queries, programming: no motion
    if (calibration_ && direct && cmd == calibration_expected_)
      calibration_expected_ = Command::OTHER;  // the command of calibration()
    else
      cancel_calibration_();  // a later command wins
    if (!seen_)
      return;
    const uint8_t from = has_position_ ? position_ : current;
    const uint8_t target = open_percent(raw_target, reversed_);
    move_command_ = cmd;
    disagree_ = 0;
    switch (cmd) {
      case Command::OPEN:
        start_(now, from, from < 100 || !referenced_ ? Motion::OPENING : Motion::IDLE, true);
        break;
      case Command::CLOSE:
        start_(now, from, from > 0 || !referenced_ ? Motion::CLOSING : Motion::IDLE, true);
        break;
      case Command::POSITION:
        start_(now, from, target > from ? Motion::OPENING : target < from ? Motion::CLOSING : Motion::IDLE, true);
        break;
      default:
        // Stops at once; keep polling until a reply shows the final position.
        start_(now, from, Motion::IDLE, false);
        break;
    }
  }

  // A position command (target in percent open) for a motor that has lost
  // its position would end up anywhere (observed: a motor ran to the open
  // end instead of raw 50).  Returns true if the position is lost: the
  // caller sends a close instead; calibration() then asks for an open if
  // the motor has not found its position at the closed end, and returns the
  // target once it has.  Any other command cancels it.
  bool begin_calibration(uint32_t now, uint8_t target) {
    if (!position_lost())
      return false;
    calibration_ = true;
    calibration_target_ = target;
    calibration_expected_ = Command::CLOSE;
    calibration_time_ = now;
    return true;
  }

  enum class Calibration : uint8_t { NONE, SEND_OPEN, SEND_TARGET, FAILED };
  // Call regularly.  SEND_OPEN: send an open command now.  SEND_TARGET: the
  // motor has its position again, send `target` (percent open) now.
  // FAILED: a command was not sent, or the moves ended without the
  // position (e.g. stopped by the remote control).
  Calibration calibration(uint32_t now, uint8_t &target) {
    if (!calibration_)
      return Calibration::NONE;
    target = calibration_target_;
    if (calibration_expected_ != Command::OTHER) {
      if (now - calibration_time_ < timing_.poll_timeout_ms)
        return Calibration::NONE;
      cancel_calibration_();
      return Calibration::FAILED;
    }
    if (active_)
      return Calibration::NONE;
    if (!position_lost()) {
      cancel_calibration_();
      return Calibration::SEND_TARGET;
    }
    if (calibration_step_ == 0) {
      // At the closed end without the position: it was the first end limit
      // since the power loss; the open end is the second.
      calibration_step_ = 1;
      calibration_expected_ = Command::OPEN;
      calibration_time_ = now;
      return Calibration::SEND_OPEN;
    }
    cancel_calibration_();
    return Calibration::FAILED;
  }
  bool calibrating() const { return calibration_; }

  // Any reply of this motor, readable or not.
  void reply(uint32_t now) {
    seen_ = true;
    online_ = true;
    last_reply_ = now;
  }

  // A decoded status of this motor (raw position).
  void status(uint32_t now, const Status &s) {
    const bool known = s.referenced();
    const bool was_known = has_status_ && referenced_;
    const bool was_lost = has_status_ && !referenced_;
    has_status_ = true;
    referenced_ = known;
    if (known)
      learn_direction_(now, s.position, was_known, was_lost);  // may mirror position_
    const bool had_position = has_position_;
    const uint8_t previous = position_;
    if (known) {
      raw_ = s.position;
      has_raw_ = true;
      has_position_ = true;
      position_ = open_percent(s.position, reversed_);
    } else {
      has_raw_ = false;
    }
    if (s.running()) {
      // Also a move started by the remote control.  The zone does not tell
      // the direction (awning_frame.h), the position change does.  Without
      // one, keep the direction of the command; for a move by the remote
      // control, an end position allows only one direction.
      if (!active_)
        move_command_ = Command::OTHER;  // not ours
      active_ = true;
      last_activity_ = now;
      if (!known)
        return;  // keep the command's direction (IDLE for the remote control)
      if (had_position && position_ > previous)
        motion_ = Motion::OPENING;
      else if (had_position && position_ < previous)
        motion_ = Motion::CLOSING;
      else if (motion_ == Motion::IDLE && position_ == 0)
        motion_ = Motion::OPENING;
      else if (motion_ == Motion::IDLE && position_ == 100)
        motion_ = Motion::CLOSING;
      return;
    }
    if (active_ && grace_ && now - command_time_ < timing_.start_grace_ms && (!known || position_ == start_position_))
      return;  // not started yet
    if (active_ && !known && (move_command_ == Command::OPEN || move_command_ == Command::CLOSE)) {
      // Position lost, but a full open/close not stopped by us ends at the
      // end limit (unless the remote control stopped it): the first one
      // since the power loss, which does not give the position yet.
      has_position_ = true;
      position_ = move_command_ == Command::OPEN ? 100 : 0;
    }
    active_ = false;
    move_command_ = Command::OTHER;
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
    move_command_ = Command::OTHER;
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
  // Percent open: from a reply, or assumed after a full open/close of a
  // motor that lost its position.  Otherwise the shown position stays.
  bool has_position() const { return has_position_; }
  uint8_t position() const { return position_; }
  // The motor reports that it does not know its position (after a power
  // loss, until it has reached both end limits).
  bool position_lost() const { return has_status_ && !referenced_; }
  // Counting direction: raw 0 = open.
  bool reversed() const { return reversed_; }
  // Percent open (0–100) → raw position for a position command.
  uint8_t raw_position(uint8_t percent_open) const { return open_percent(percent_open, reversed_); }

 private:
  void start_(uint32_t now, uint8_t from, Motion motion, bool grace) {
    active_ = true;
    grace_ = grace;
    motion_ = motion;
    command_time_ = now;
    last_activity_ = now;
    start_position_ = from;
  }
  void cancel_calibration_() {
    calibration_ = false;
    calibration_expected_ = Command::OTHER;
    calibration_step_ = 0;
  }

  // Before position_ is updated from `raw` (see the class comment).
  void learn_direction_(uint32_t now, uint8_t raw, bool was_known, bool was_lost) {
    if (!active_ || (move_command_ != Command::OPEN && move_command_ != Command::CLOSE))
      return;
    const bool opening = move_command_ == Command::OPEN;
    if (was_lost) {
      // Found its position: raw 100 is the end limit just reached.  The
      // shown position did not come from the old direction: no mirroring.
      if (raw == 100)
        set_reversed_(!opening, false);
      return;
    }
    if (!was_known || !has_raw_ || raw == raw_ || now - command_time_ < timing_.start_grace_ms)
      return;
    const bool reversed = (raw > raw_) != opening;
    if (reversed == reversed_) {
      disagree_ = 0;
    } else if (++disagree_ >= 2) {
      set_reversed_(reversed, true);
    }
  }
  void set_reversed_(bool reversed, bool mirror) {
    disagree_ = 0;
    if (reversed == reversed_)
      return;
    reversed_ = reversed;
    if (mirror) {
      // Positions read with the old direction.
      position_ = uint8_t(100 - position_);
      start_position_ = uint8_t(100 - start_position_);
    }
  }

  Timing timing_;
  bool reversed_;
  bool seen_{false};
  bool online_{false};
  uint32_t last_reply_{0};
  bool has_position_{false};
  uint8_t position_{0};
  bool has_status_{false};
  bool referenced_{true};
  bool has_raw_{false};
  uint8_t raw_{0};
  uint8_t disagree_{0};
  bool active_{false};
  bool grace_{false};
  Motion motion_{Motion::IDLE};
  Command move_command_{Command::OTHER};
  uint32_t command_time_{0};
  uint32_t last_activity_{0};
  uint8_t start_position_{0};
  bool calibration_{false};
  Command calibration_expected_{Command::OTHER};
  uint8_t calibration_step_{0};
  uint8_t calibration_target_{0};
  uint32_t calibration_time_{0};
  bool pending_{false};
  bool polled_{false};
  uint32_t queued_time_{0};
  uint32_t poll_time_{0};
};

}  // namespace awning
