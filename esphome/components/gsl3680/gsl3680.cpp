#include "gsl3680.h"

#include "esphome/core/log.h"

namespace esphome {
namespace gsl3680 {

static const char *const TAG = "gsl3680.touchscreen";

#define CHECK_I2C(err, op) \
  (err) = (op); \
  if ((err) != i2c::ERROR_OK) { \
    ESP_LOGE(TAG, "I2C error: %d", (err)); \
    return (err); \
  }

void GSL3680Touchscreen::setup() {
  ESP_LOGCONFIG(TAG, "Setting up GSL3680 Touchscreen...");

  // Get display dimensions for calibration
  this->x_raw_max_ = this->swap_x_y_ ? this->display_->get_native_height() : this->display_->get_native_width();
  this->y_raw_max_ = this->swap_x_y_ ? this->display_->get_native_width() : this->display_->get_native_height();

  // Configure reset pin
  this->reset_pin_->pin_mode(gpio::FLAG_OUTPUT);
  this->reset_pin_->setup();
  this->reset_pin_->digital_write(false);
  this->reset_pin_->digital_write(true);

  // Initialize the chip
  i2c::ErrorCode err = this->init_();
  if (err != i2c::ERROR_OK) {
    ESP_LOGE(TAG, "Initialization failed with error %d", err);
    this->mark_failed(LOG_STR("I2C init error"));
    return;
  }

  // Configure interrupt pin
  this->interrupt_pin_->pin_mode(gpio::FLAG_INPUT | gpio::FLAG_PULLUP);
  this->interrupt_pin_->setup();
  this->attach_interrupt_(this->interrupt_pin_, gpio::INTERRUPT_FALLING_EDGE);

  // Initialize the point ID algorithm
  gsl_DataInit(gsl_config_data_id);

  ESP_LOGI(TAG, "GSL3680 setup complete");
}

i2c::ErrorCode GSL3680Touchscreen::init_() {
  i2c::ErrorCode err;

  CHECK_I2C(err, this->read_config_());
  CHECK_I2C(err, this->clear_reg_());
  CHECK_I2C(err, this->reset_());
  CHECK_I2C(err, this->load_fw_());
  CHECK_I2C(err, this->startup_());
  CHECK_I2C(err, this->check_mem_());

  return i2c::ERROR_OK;
}

i2c::ErrorCode GSL3680Touchscreen::reset_() {
  this->reset_pin_->digital_write(false);
  delay(20);
  this->reset_pin_->digital_write(true);
  delay(20);

  i2c::ErrorCode err;
  uint8_t buf[4];

  // Reset sequence
  buf[0] = 0x88;
  CHECK_I2C(err, this->write_register(0xE0, buf, 1));
  delay(10);

  buf[0] = 0x04;
  CHECK_I2C(err, this->write_register(0xE4, buf, 1));
  delay(10);

  buf[0] = 0x00;
  buf[1] = 0x00;
  buf[2] = 0x00;
  buf[3] = 0x00;
  CHECK_I2C(err, this->write_register(0xBC, buf, 4));
  delay(10);

  ESP_LOGD(TAG, "Reset complete");
  return i2c::ERROR_OK;
}

i2c::ErrorCode GSL3680Touchscreen::read_config_() {
  i2c::ErrorCode err;
  uint8_t buf[4];
  uint8_t test_data[4] = {0x12, 0x34, 0x56, 0x00};

  delay(50);
  CHECK_I2C(err, this->read_register(0xF0, buf, 4));
  ESP_LOGD(TAG, "Config read #1: 0x%02X 0x%02X 0x%02X 0x%02X", buf[0], buf[1], buf[2], buf[3]);

  delay(20);
  CHECK_I2C(err, this->write_register(0xF0, test_data, 4));

  delay(20);
  CHECK_I2C(err, this->read_register(0xF0, buf, 4));
  ESP_LOGD(TAG, "Config read #2: 0x%02X 0x%02X 0x%02X 0x%02X", buf[0], buf[1], buf[2], buf[3]);

  if (buf[0] != test_data[0]) {
    ESP_LOGE(TAG, "Config verification failed: expected 0x12, got 0x%02X", buf[0]);
    return i2c::ERROR_UNKNOWN;
  }

  return i2c::ERROR_OK;
}

i2c::ErrorCode GSL3680Touchscreen::clear_reg_() {
  static const uint8_t regs[] = {0xE0, 0x88, 0xE4, 0xE0};
  static const uint8_t data[] = {0x88, 0x01, 0x04, 0x00};

  i2c::ErrorCode err;

  for (int i = 0; i < 4; i++) {
    uint8_t val = data[i];
    CHECK_I2C(err, this->write_register(regs[i], &val, 1));
    delay(20);
  }

  ESP_LOGD(TAG, "Registers cleared");
  return i2c::ERROR_OK;
}

i2c::ErrorCode GSL3680Touchscreen::load_fw_() {
  ESP_LOGD(TAG, "Loading firmware...");

  i2c::ErrorCode err;
  uint16_t fw_len = sizeof(GSLX680_FW) / sizeof(struct fw_data);

  for (uint16_t i = 0; i < fw_len; i++) {
    uint8_t addr = GSLX680_FW[i].offset;
    uint32_t val = GSLX680_FW[i].val;

    uint8_t buf[4];
    buf[0] = (val >> 0) & 0xFF;
    buf[1] = (val >> 8) & 0xFF;
    buf[2] = (val >> 16) & 0xFF;
    buf[3] = (val >> 24) & 0xFF;

    // Page register (0xF0) only needs 1 byte
    uint8_t len = (addr == 0xF0) ? 1 : 4;
    CHECK_I2C(err, this->write_register(addr, buf, len));
  }

  ESP_LOGD(TAG, "Firmware loaded (%d entries)", fw_len);
  return i2c::ERROR_OK;
}

i2c::ErrorCode GSL3680Touchscreen::startup_() {
  i2c::ErrorCode err;
  uint8_t buf[1] = {0x00};

  CHECK_I2C(err, this->write_register(0xE0, buf, 1));
  delay(10);

  ESP_LOGD(TAG, "Chip started");
  return i2c::ERROR_OK;
}

i2c::ErrorCode GSL3680Touchscreen::check_mem_() {
  i2c::ErrorCode err;
  uint8_t buf[4];

  delay(30);
  CHECK_I2C(err, this->read_register(0xB0, buf, 4));
  ESP_LOGD(TAG, "Memory check: 0x%02X 0x%02X 0x%02X 0x%02X", buf[0], buf[1], buf[2], buf[3]);

  for (int i = 0; i < 4; i++) {
    if (buf[i] != 0x5A) {
      ESP_LOGE(TAG, "Memory check failed at byte %d: expected 0x5A, got 0x%02X", i, buf[i]);
      return i2c::ERROR_UNKNOWN;
    }
  }

  ESP_LOGD(TAG, "Memory check passed");
  return i2c::ERROR_OK;
}

void GSL3680Touchscreen::update_touches() {
  uint8_t touch_data[24];
  struct gsl_touch_info cinfo = {};

  i2c::ErrorCode err = this->read_register(0x80, touch_data, 24);
  if (err != i2c::ERROR_OK) {
    ESP_LOGW(TAG, "Failed to read touch data");
    return;
  }

  ESP_LOGV(TAG, "Raw: %02X %02X %02X %02X %02X %02X %02X %02X", touch_data[0], touch_data[1], touch_data[2],
           touch_data[3], touch_data[4], touch_data[5], touch_data[6], touch_data[7]);

  // Parse touch data
  // First touch point
  uint16_t x1 = ((touch_data[7] & 0x0F) << 8) | touch_data[6];
  uint16_t y1 = (touch_data[5] << 8) | touch_data[4];
  uint8_t id1 = (touch_data[7] & 0xF0) >> 4;

  // Second touch point
  uint16_t x2 = ((touch_data[11] & 0x0F) << 8) | touch_data[10];
  uint16_t y2 = (touch_data[9] << 8) | touch_data[8];
  uint8_t id2 = (touch_data[11] & 0xF0) >> 4;

  cinfo.x[0] = x1;
  cinfo.y[0] = y1;
  cinfo.id[0] = id1;
  cinfo.x[1] = x2;
  cinfo.y[1] = y2;
  cinfo.id[1] = id2;
  cinfo.finger_num = (touch_data[3] << 24) | (touch_data[2] << 16) | (touch_data[1] << 8) | touch_data[0];

  // Process through point ID algorithm
  gsl_alg_id_main(&cinfo);
  unsigned int mask = gsl_mask_tiaoping();

  if ((mask > 0) && (mask < 0xFFFFFFFF)) {
    uint8_t buf[4] = {0x0A, 0x00, 0x00, 0x00};
    this->write_register(0xF0, buf, 4);
    buf[0] = (mask >> 0) & 0xFF;
    buf[1] = (mask >> 8) & 0xFF;
    buf[2] = (mask >> 16) & 0xFF;
    buf[3] = (mask >> 24) & 0xFF;
    this->write_register(0x08, buf, 4);
  }

  ESP_LOGV(TAG, "Touch: fingers=%d x=%d y=%d mask=%u", cinfo.finger_num, cinfo.x[0], cinfo.y[0], mask);

  if (cinfo.finger_num >= 1) {
    this->add_raw_touch_position_(0, cinfo.x[0], cinfo.y[0]);
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
