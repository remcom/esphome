#pragma once

#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/components/button/button.h"

#include "flexispot_e7.h"

namespace esphome::flexispot_e7 {

class FlexiSpotE7Button : public button::Button, public Parented<FlexiSpotE7> {
 public:
  explicit FlexiSpotE7Button(FlexiSpotCommand command) : command_(command) {}

 protected:
  void press_action() override { this->parent_->send_command(this->command_); }

  FlexiSpotCommand command_;
};

}  // namespace esphome::flexispot_e7
