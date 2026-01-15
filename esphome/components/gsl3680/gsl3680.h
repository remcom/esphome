#pragma once

#include "esphome/components/i2c/i2c.h"
#include "esphome/components/touchscreen/touchscreen.h"
#include "esphome/core/component.h"
#include "esphome/core/hal.h"

#include "gsl3680_firmware.h"
#include "gsl_point_id.h"

namespace esphome {
namespace gsl3680 {

static const uint8_t MAX_TOUCHES = 5;

class GSL3680Touchscreen : public touchscreen::Touchscreen, public i2c::I2CDevice {
 public:
  void setup() override;
  void dump_config() override;
  void update_touches() override;

  void set_interrupt_pin(InternalGPIOPin *pin) { this->interrupt_pin_ = pin; }
  void set_reset_pin(InternalGPIOPin *pin) { this->reset_pin_ = pin; }

 protected:
  i2c::ErrorCode reset_();
  i2c::ErrorCode init_();
  i2c::ErrorCode read_config_();
  i2c::ErrorCode clear_reg_();
  i2c::ErrorCode load_fw_();
  i2c::ErrorCode startup_();
  i2c::ErrorCode check_mem_();

  InternalGPIOPin *interrupt_pin_{nullptr};
  InternalGPIOPin *reset_pin_{nullptr};
};

}  // namespace gsl3680
}  // namespace esphome
