#include "gsl3680_touchscreen.h"

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome {
namespace gsl3680 {

static const char *const TAG = "gsl3680.touchscreen";

// GSL3680 I2C registers
static const uint8_t REG_DATA = 0x80;        // Touch data register
static const uint8_t REG_STATUS = 0xE0;      // Status register
static const uint8_t REG_PAGE = 0xF0;        // Page register for firmware
static const uint8_t REG_TOUCH_STATUS = 0xBC;  // Touch status register

// Status register values
static const uint8_t STATUS_FW = 0x80;     // Device needs firmware
static const uint8_t STATUS_TOUCH = 0x00;  // Normal touch mode

// Maximum number of touch points supported
static const size_t MAX_TOUCHES = 10;

// Touch data packet size: 4 bytes header + 4 bytes per touch point
static const size_t TOUCH_PACKET_SIZE = 4 + (MAX_TOUCHES * 4);

#define ERROR_CHECK(err) \
  if ((err) != i2c::ERROR_OK) { \
    this->status_set_warning(LOG_STR(ESP_LOG_MSG_COMM_FAIL)); \
    return; \
  }

void GSL3680Touchscreen::setup() {
  ESP_LOGCONFIG(TAG, "Setting up GSL3680 Touchscreen...");

  if (this->reset_pin_ != nullptr) {
    this->reset_pin_->setup();
    this->hard_reset_();
  }

  if (this->interrupt_pin_ != nullptr) {
    this->interrupt_pin_->setup();
    this->attach_interrupt_(this->interrupt_pin_, gpio::INTERRUPT_FALLING_EDGE);
  }

  // Check if the device is responding
  uint8_t status;
  i2c::ErrorCode err = this->read_register(REG_STATUS, &status, 1);
  if (err != i2c::ERROR_OK) {
    ESP_LOGE(TAG, "Failed to communicate with GSL3680");
    this->mark_failed();
    return;
  }

  ESP_LOGD(TAG, "GSL3680 status: 0x%02X", status);

  // The GSL3680 requires firmware to be loaded before it can work.
  // If status shows firmware mode (0x80), the device needs firmware.
  // Without firmware, the device won't report touch data properly.
  if (status == STATUS_FW) {
    ESP_LOGW(TAG, "GSL3680 is in firmware mode - firmware may need to be loaded");
    ESP_LOGW(TAG, "Touch functionality may be limited without proper firmware");
  }

  // Read calibration values if not set - GSL3680 uses 12-bit coordinates (0-4095)
  if (this->x_raw_max_ == 0) {
    this->x_raw_max_ = 4095;
  }
  if (this->y_raw_max_ == 0) {
    this->y_raw_max_ = 4095;
  }
}

void GSL3680Touchscreen::hard_reset_() {
  ESP_LOGD(TAG, "Performing hardware reset");
  this->reset_pin_->digital_write(false);
  delay(20);
  this->reset_pin_->digital_write(true);
  delay(50);
}

void GSL3680Touchscreen::update_touches() {
  uint8_t data[TOUCH_PACKET_SIZE];

  // Read touch data from register 0x80
  i2c::ErrorCode err = this->read_register(REG_DATA, data, TOUCH_PACKET_SIZE);
  if (err != i2c::ERROR_OK) {
    this->status_set_warning(LOG_STR(ESP_LOG_MSG_COMM_FAIL));
    this->skip_update_ = true;
    return;
  }

  // First byte contains the number of touches
  uint8_t num_touches = data[0];
  if (num_touches > MAX_TOUCHES) {
    num_touches = MAX_TOUCHES;
  }

  this->skip_update_ = false;

  // Each touch point is 4 bytes starting at offset 4
  // Format per touch: y_z (2 bytes LE), x_id (2 bytes LE)
  // Lower 12 bits = coordinate, upper 4 bits = id/pressure
  for (uint8_t i = 0; i < num_touches; i++) {
    uint8_t *touch_data = &data[4 + (i * 4)];

    uint16_t y_z = encode_uint16(touch_data[1], touch_data[0]);
    uint16_t x_id = encode_uint16(touch_data[3], touch_data[2]);

    uint8_t id = (x_id >> 12) & 0x0F;
    uint16_t x = x_id & 0x0FFF;
    uint16_t y = y_z & 0x0FFF;

    this->add_raw_touch_position_(id, x, y);
  }
}

void GSL3680Touchscreen::dump_config() {
  ESP_LOGCONFIG(TAG, "GSL3680 Touchscreen:");
  LOG_I2C_DEVICE(this);
  LOG_PIN("  Interrupt Pin: ", this->interrupt_pin_);
  LOG_PIN("  Reset Pin: ", this->reset_pin_);
}

}  // namespace gsl3680
}  // namespace esphome
