#include "tmc2209.h"
#include "esphome/core/log.h"

namespace esphome::tmc2209 {

static const char *const TAG = "tmc2209";

void TMC2209Stepper::dump_config() {
  ESP_LOGCONFIG(TAG, "TMC2209:");
  TMC22XXStepper::dump_config();
}

uint32_t *TMC2209Stepper::shadow_register_(uint8_t reg) {
  switch (reg) {
    case REG_TCOOLTHRS:
      return &this->tcoolthrs_;
    case REG_SGTHRS:
      return &this->sgthrs_;
    case REG_COOLCONF:
      return &this->coolconf_;
    default:
      return TMC22XXStepper::shadow_register_(reg);
  }
}

void TMC2209Stepper::configure_driver_() {
  TMC22XXStepper::configure_driver_();
  this->write_register(REG_TCOOLTHRS, this->tcoolthrs_);
  this->write_register(REG_SGTHRS, this->sgthrs_);
  this->write_register(REG_COOLCONF, this->coolconf_);
}

}  // namespace esphome::tmc2209
