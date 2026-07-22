#include "cover.h"
#include "esphome/core/log.h"

namespace esphome::flexispot_e7 {

static const char *const TAG = "flexispot_e7.cover";

using namespace esphome::cover;

void FlexiSpotE7Cover::setup() {
  this->parent_->add_on_height_callback([this](float height) { this->on_height_(height); });
  if (this->parent_->has_height()) {
    this->on_height_(this->parent_->get_height());
  }
}

void FlexiSpotE7Cover::dump_config() {
  LOG_COVER("", "FlexiSpot E7 Cover", this);
  ESP_LOGCONFIG(TAG, "  Min Height: %.1f cm", this->min_height_);
  ESP_LOGCONFIG(TAG, "  Max Height: %.1f cm", this->max_height_);
  ESP_LOGCONFIG(TAG, "  Stop Tolerance: %.1f cm", this->stop_tolerance_);
}

CoverTraits FlexiSpotE7Cover::get_traits() {
  CoverTraits traits;
  traits.set_supports_position(true);
  traits.set_supports_toggle(false);
  traits.set_is_assumed_state(false);
  return traits;
}

void FlexiSpotE7Cover::control(const CoverCall &call) {
  if (call.get_stop()) {
    this->stop_movement_();
    return;
  }
  if (call.get_position().has_value()) {
    this->target_height_ = this->position_to_height_(*call.get_position());
    this->moving_ = true;
    this->last_move_command_ = 0;  // Allow an immediate nudge on the next loop
  }
}

void FlexiSpotE7Cover::loop() {
  if (!this->moving_) {
    return;
  }
  if (!this->parent_->has_height()) {
    return;  // Wait until we have a real height before driving the desk
  }

  const float height = this->parent_->get_height();
  const float diff = this->target_height_ - height;
  if (std::abs(diff) <= this->stop_tolerance_) {
    this->stop_movement_();
    return;
  }

  const uint32_t now = millis();
  if (now - this->last_move_command_ < MOVE_COMMAND_INTERVAL) {
    return;
  }
  this->last_move_command_ = now;

  if (diff > 0) {
    this->parent_->move_up();
    this->current_operation = COVER_OPERATION_OPENING;
  } else {
    this->parent_->move_down();
    this->current_operation = COVER_OPERATION_CLOSING;
  }
}

void FlexiSpotE7Cover::stop_movement_() {
  this->moving_ = false;
  this->current_operation = COVER_OPERATION_IDLE;
  this->publish_state();
}

void FlexiSpotE7Cover::on_height_(float height) {
  this->position = this->height_to_position_(height);
  // Avoid a flash write on every reading while moving; save once we stop.
  this->publish_state(!this->moving_);
}

float FlexiSpotE7Cover::height_to_position_(float height) const {
  float position = (height - this->min_height_) / (this->max_height_ - this->min_height_);
  return clamp(position, 0.0f, 1.0f);
}

float FlexiSpotE7Cover::position_to_height_(float position) const {
  return this->min_height_ + position * (this->max_height_ - this->min_height_);
}

}  // namespace esphome::flexispot_e7
