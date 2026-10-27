// Connects the bus to the ESPHome entities of each motor and of the
// all-awnings cover (README → How feedback works).
//
//  - Motor: decodes reply bursts of the raw receive tap (awning_frame.h),
//    follows the commands uartex sends, keeps the MotorState
//    (awning_motion.h) and publishes cover position and operation, voltage,
//    last seen; queues the status queries (the cover's command_update).
//  - Group: the all-awnings cover shows the average position and the
//    motion of the online motors.
//
// Created at boot by packages/motor.yaml and packages/group.yaml;
// loop_all() runs from the interval in awning-motor.yaml.
#pragma once

#include <string>
#include <vector>

#include "esphome/components/cover/cover.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include "awning_frame.h"
#include "awning_motion.h"

namespace awning {

class Motor;
class Group;

// All motors and groups, in the order they were created.
inline std::vector<Motor *> &motors() {
  static std::vector<Motor *> list;
  return list;
}
inline std::vector<Group *> &groups() {
  static std::vector<Group *> list;
  return list;
}

inline esphome::cover::CoverOperation to_operation(Motion motion) {
  switch (motion) {
    case Motion::OPENING:
      return esphome::cover::COVER_OPERATION_OPENING;
    case Motion::CLOSING:
      return esphome::cover::COVER_OPERATION_CLOSING;
    default:
      return esphome::cover::COVER_OPERATION_IDLE;
  }
}

class Motor {
 public:
  Motor(uint8_t id, uint8_t chan_l, uint8_t chan_h, const Timing &timing)
      : id_(id), key_(make_key(id, chan_l, chan_h)), state_(timing) {
    motors().push_back(this);
  }

  // cover: the uartex cover (an esphome Cover and PollingComponent).
  template<typename C> void set_cover(C *cover) {
    this->cover_ = cover;
    this->poller_ = cover;
  }
  void set_voltage_sensor(esphome::sensor::Sensor *sensor) { this->voltage_ = sensor; }
  void set_last_seen_sensor(esphome::text_sensor::TextSensor *sensor) { this->last_seen_ = sensor; }
  void set_clock(esphome::time::RealTimeClock *clock) { this->clock_ = clock; }

  uint8_t id() const { return this->id_; }
  uint32_t key() const { return this->key_; }
  esphome::cover::Cover *cover() const { return this->cover_; }
  bool online() const { return this->state_.online(esphome::millis()); }
  // Answered at least once since boot (connected).
  bool seen() const { return this->state_.seen(); }
  bool has_position() const { return this->state_.has_position(); }
  Motion motion() const { return this->state_.motion(); }

  // One burst of the raw receive tap.
  void on_burst(const std::vector<uint8_t> &burst) {
    Status status;
    const Seen seen = read_reply(burst, this->key_, status);
    if (seen == Seen::NO)
      return;
    const uint32_t now = esphome::millis();
    const bool was_online = this->state_.online(now);
    this->state_.reply(now);
    if (seen == Seen::STATUS || seen == Seen::REPAIRED) {
      ESP_LOGD("awning", "Motor %u  pos=%u%%  running=%u  zone=0x%02X%s", this->id_, status.position,
               status.running(), status.zone, seen == Seen::REPAIRED ? "  (damaged reply)" : "");
      this->state_.status(now, status);
      if (this->voltage_ != nullptr && status.voltage != 0 && this->voltage_->state != status.voltage)
        this->voltage_->publish_state(status.voltage);
      this->publish_cover_();
    } else {
      ESP_LOGD("awning", "Motor %u  reply not readable", this->id_);
    }
    this->note_seen_(was_online);
  }

  // A frame uartex wrote to the bus (header, payload, checksum).
  void on_write(const uint8_t *frame, uint16_t len) {
    CommandFrame f;
    if (!parse_sent_command(frame, len, f) || !f.addresses(this->key_))
      return;
    const uint32_t now = esphome::millis();
    if (f.command == Command::STATUS) {
      if (f.key == this->key_)
        this->state_.poll_sent(now);
      return;
    }
    const uint8_t current = uint8_t(this->cover_->position * 100.0f + 0.5f);
    this->state_.command(now, f.command, f.target, current);
    this->publish_cover_();
  }

  void loop() {
    const uint32_t now = esphome::millis();
    if (this->state_.tick(now))
      this->publish_cover_();
    const bool online = this->state_.online(now);
    if (this->was_online_ && !online)
      this->publish_last_seen_();  // exact time of the last reply
    this->was_online_ = online;
    if (this->state_.poll_due(now)) {
      this->poller_->update();  // queues the cover's command_update
      this->state_.poll_queued(now);
    }
  }

 protected:
  void publish_cover_() {
    const float position = this->state_.has_position() ? this->state_.position() / 100.0f : this->cover_->position;
    const auto operation = to_operation(this->state_.motion());
    if (position == this->cover_->position && operation == this->cover_->current_operation)
      return;
    this->cover_->position = position;
    this->cover_->current_operation = operation;
    this->cover_->publish_state();
  }

  // Last seen: published when the motor comes back online, then at most
  // every 5 minutes, and with the exact time when it goes offline.
  void note_seen_(bool was_online) {
    if (this->clock_ == nullptr)
      return;
    const auto now = this->clock_->utcnow();
    if (!now.is_valid())
      return;
    this->last_seen_ts_ = now.timestamp;
    if (!was_online || now.timestamp - this->last_published_ts_ >= 300)
      this->publish_last_seen_();
  }
  void publish_last_seen_() {
    if (this->last_seen_ == nullptr || this->last_seen_ts_ == 0)
      return;
    this->last_published_ts_ = this->last_seen_ts_;
    this->last_seen_->publish_state(
        esphome::ESPTime::from_epoch_utc(this->last_seen_ts_).strftime("%Y-%m-%dT%H:%M:%SZ"));
  }

  uint8_t id_;
  uint32_t key_;
  MotorState state_;
  esphome::cover::Cover *cover_{nullptr};
  esphome::PollingComponent *poller_{nullptr};
  esphome::sensor::Sensor *voltage_{nullptr};
  esphome::text_sensor::TextSensor *last_seen_{nullptr};
  esphome::time::RealTimeClock *clock_{nullptr};
  bool was_online_{false};
  time_t last_seen_ts_{0};
  time_t last_published_ts_{0};
};

// The broadcast cover: average position of the online motors with a known
// position; opening/closing while more of them open/close than the other
// way.  Keeps its last state while no motor is online.
class Group {
 public:
  explicit Group(esphome::cover::Cover *cover) : cover_(cover) { groups().push_back(this); }

  void loop() {
    float sum = 0;
    int count = 0, opening = 0, closing = 0;
    for (const Motor *motor : motors()) {
      if (!motor->online() || !motor->has_position())
        continue;
      sum += motor->cover()->position;
      count++;
      opening += motor->motion() == Motion::OPENING;
      closing += motor->motion() == Motion::CLOSING;
    }
    if (count == 0)
      return;
    const float position = sum / count;
    const Motion motion = opening == 0 && closing == 0 ? Motion::IDLE
                          : opening >= closing          ? Motion::OPENING
                                                        : Motion::CLOSING;
    const auto operation = to_operation(motion);
    if (position == this->cover_->position && operation == this->cover_->current_operation)
      return;
    this->cover_->position = position;
    this->cover_->current_operation = operation;
    this->cover_->publish_state();
  }

 protected:
  esphome::cover::Cover *cover_;
};

inline Motor *find_motor(uint32_t key) {
  for (Motor *motor : motors())
    if (motor->key() == key)
      return motor;
  return nullptr;
}

// Position for the all-awnings cover.  The motors ignore position frames
// sent to ID 0 (tested with and without channel bytes; open, close and stop
// do work as broadcasts), so every motor that has answered since boot gets
// its own position command.  Returns the number of motors.
inline int set_position_all(float position) {
  int count = 0;
  for (Motor *motor : motors()) {
    if (!motor->seen())
      continue;
    motor->cover()->make_call().set_position(position).perform();
    count++;
  }
  if (count == 0)
    ESP_LOGW("awning", "All awnings: no motor has answered yet, position not sent");
  return count;
}

inline void loop_all() {
  for (Motor *motor : motors())
    motor->loop();
  for (Group *group : groups())
    group->loop();
}

}  // namespace awning
