#pragma once

#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/components/cover/cover.h"

#include "flexispot_e7.h"

namespace esphome::flexispot_e7 {

// Presents the desk as a cover: position 0.0 is min_height (fully "closed"),
// position 1.0 is max_height (fully "open"). Setting a position drives the desk
// up or down until the reported height reaches the target.
class FlexiSpotE7Cover : public cover::Cover, public Component, public Parented<FlexiSpotE7> {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  cover::CoverTraits get_traits() override;

  void set_min_height(float min_height) { this->min_height_ = min_height; }
  void set_max_height(float max_height) { this->max_height_ = max_height; }
  void set_stop_tolerance(float tolerance) { this->stop_tolerance_ = tolerance; }

 protected:
  void control(const cover::CoverCall &call) override;
  void on_height_(float height);
  float height_to_position_(float height) const;
  float position_to_height_(float position) const;
  void stop_movement_();

  float min_height_{0};
  float max_height_{0};
  float stop_tolerance_{1.0f};

  bool moving_{false};
  float target_height_{0};
  uint32_t last_move_command_{0};

  // Interval between the repeated up/down nudges that keep the desk moving.
  static const uint32_t MOVE_COMMAND_INTERVAL = 200;
};

}  // namespace esphome::flexispot_e7
