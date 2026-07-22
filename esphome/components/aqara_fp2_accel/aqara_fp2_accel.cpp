#include "aqara_fp2_accel.h"
#include "esphome/core/log.h"

#include <cmath>
#include <cstdlib>

namespace esphome::aqara_fp2_accel {

static const char *const TAG = "aqara_fp2_accel";

// Register map of the on-board accelerometer.
static constexpr uint8_t REG_DATA_START = 0x02;  // X/Y/Z, 2 bytes each, 12-bit left-justified
static constexpr uint8_t REG_CONFIG_A = 0x11;
static constexpr uint8_t REG_CONFIG_B = 0x0F;
static constexpr uint8_t VAL_CONFIG_A = 0x0E;
static constexpr uint8_t VAL_CONFIG_B = 0x40;

// Radians to degrees.
static constexpr float SCALE_FACTOR = 57.2957795f;

const char *orientation_to_string(Orientation orientation) {
  switch (orientation) {
    case Orientation::UP:
      return "up";
    case Orientation::UP_TILT:
      return "up_tilt";
    case Orientation::UP_TILT_REV:
      return "up_tilt_reverse";
    case Orientation::SIDE:
      return "side";
    case Orientation::SIDE_REV:
      return "side_reverse";
    case Orientation::DOWN:
      return "down";
    case Orientation::DOWN_TILT:
      return "down_tilt";
    case Orientation::DOWN_TILT_REV:
      return "down_tilt_reverse";
    default:
      return "invalid";
  }
}

void AqaraFP2Accel::setup() {
  ESP_LOGCONFIG(TAG, "Running setup");
  if (!this->write_byte(REG_CONFIG_A, VAL_CONFIG_A) || !this->write_byte(REG_CONFIG_B, VAL_CONFIG_B)) {
    ESP_LOGE(TAG, "Failed to configure accelerometer");
    this->mark_failed();
    return;
  }
}

void AqaraFP2Accel::dump_config() {
  ESP_LOGCONFIG(TAG, "Aqara FP2 accelerometer:");
  LOG_I2C_DEVICE(this);
  LOG_UPDATE_INTERVAL(this);
  if (this->is_failed()) {
    ESP_LOGE(TAG, "  Communication failed");
  }
#ifdef USE_TEXT_SENSOR
  LOG_TEXT_SENSOR("  ", "Orientation", this->orientation_text_sensor_);
#endif
#ifdef USE_BINARY_SENSOR
  LOG_BINARY_SENSOR("  ", "Vibration", this->vibration_binary_sensor_);
#endif
#ifdef USE_SENSOR
  LOG_SENSOR("  ", "Tilt angle", this->tilt_angle_sensor_);
#endif
}

bool AqaraFP2Accel::read_accel_xyz_(int16_t *x, int16_t *y, int16_t *z) {
  uint8_t data[6];
  if (!this->read_bytes(REG_DATA_START, data, sizeof(data))) {
    ESP_LOGW(TAG, "Failed to read accelerometer data");
    return false;
  }

  // Each axis is a 12-bit two's-complement value, left-justified across two bytes.
  int16_t *out[3] = {x, y, z};
  for (uint8_t axis = 0; axis < 3; axis++) {
    uint16_t raw = static_cast<uint16_t>(data[axis * 2] >> 4) | static_cast<uint16_t>(data[axis * 2 + 1] << 4);
    if ((raw & 0x800) != 0) {
      raw |= 0xF000;  // sign-extend
    }
    *out[axis] = static_cast<int16_t>(raw);
  }
  return true;
}

void AqaraFP2Accel::update() {
  if (!this->read_accel_xyz_(&this->acc_x_buf_[this->samples_read_], &this->acc_y_buf_[this->samples_read_],
                             &this->acc_z_buf_[this->samples_read_])) {
    return;
  }
  this->samples_read_++;
  if (this->samples_read_ < ACC_SAMPLE_COUNT) {
    return;
  }
  this->samples_read_ = 0;

  // Average the buffered samples.
  int32_t sum_x = 0, sum_y = 0, sum_z = 0;
  for (uint8_t i = 0; i < ACC_SAMPLE_COUNT; i++) {
    sum_x += this->acc_x_buf_[i];
    sum_y += this->acc_y_buf_[i];
    sum_z += this->acc_z_buf_[i];
  }
  int16_t avg_x = static_cast<int16_t>(sum_x / ACC_SAMPLE_COUNT);
  int16_t avg_y = static_cast<int16_t>(sum_y / ACC_SAMPLE_COUNT);
  int16_t avg_z = static_cast<int16_t>(sum_z / ACC_SAMPLE_COUNT);

  // Energy: sum of squared deviations from the mean, used for vibration detection.
  int32_t var_x = 0, var_y = 0, var_z = 0;
  for (uint8_t i = 0; i < ACC_SAMPLE_COUNT; i++) {
    int32_t d = this->acc_x_buf_[i] - avg_x;
    var_x += d * d;
    d = this->acc_y_buf_[i] - avg_y;
    var_y += d * d;
    d = this->acc_z_buf_[i] - avg_z;
    var_z += d * d;
  }
  int32_t energy_sum = (var_x / ACC_SAMPLE_COUNT) + (var_y / ACC_SAMPLE_COUNT) + (var_z / ACC_SAMPLE_COUNT);

  this->process_samples_(avg_x, avg_y, avg_z, energy_sum);
}

void AqaraFP2Accel::process_samples_(int32_t acc_x, int32_t acc_y, int32_t acc_z, int32_t energy_sum) {
  // Inclination angles of each axis relative to the horizontal plane (-90..+90 deg).
  float dx = static_cast<float>(acc_x);
  float dy = static_cast<float>(acc_y);
  float dz = static_cast<float>(acc_z);
  int16_t angle_x = static_cast<int16_t>(std::atan2(dy, std::hypot(dx, dz)) * SCALE_FACTOR);
  int16_t angle_y = static_cast<int16_t>(std::atan2(dx, std::hypot(dy, dz)) * SCALE_FACTOR);
  int16_t angle_z = static_cast<int16_t>(std::atan2(dz, std::hypot(dx, dy)) * SCALE_FACTOR);

  // Output angle: 0 when vertical (Z pointing up/down), 90 when horizontal.
  this->output_angle_z_ = 90 - std::abs(angle_z);

  // Orientation classification. The window checks mirror the stock firmware's logic:
  // (value + offset) < width is an unsigned range test for -offset <= value < width - offset.
  Orientation current = Orientation::INVALID;
  uint16_t z_window = static_cast<uint16_t>(angle_z);
  bool y_flat = static_cast<uint16_t>(angle_y + 19) < 39;
  bool x_flat = static_cast<uint16_t>(angle_x + 19) < 39;

  if (angle_z < -69) {
    if (y_flat && x_flat) {
      current = Orientation::UP;
    }
  } else if (static_cast<uint16_t>(z_window + 69) < 49) {
    if (y_flat && std::abs(angle_x) > 20) {
      current = angle_x < 0 ? Orientation::UP_TILT : Orientation::UP_TILT_REV;
    }
  } else if (angle_z > 70) {
    if (y_flat && x_flat) {
      current = Orientation::DOWN;
    }
  } else if (static_cast<uint16_t>(z_window + 20) < 40) {
    if (y_flat && std::abs(angle_x) > 50) {
      current = angle_x < 0 ? Orientation::SIDE : Orientation::SIDE_REV;
    }
  } else if (static_cast<uint16_t>(z_window - 20) < 51) {
    if (y_flat && static_cast<uint16_t>(angle_x + 69) < 139) {
      current = angle_x < 0 ? Orientation::DOWN_TILT : Orientation::DOWN_TILT_REV;
    }
  }

  // Debounce spurious INVALID readings by holding the last stable orientation.
  if (this->debounce_invalid_ < 30) {
    if (current == Orientation::INVALID) {
      this->debounce_invalid_++;
      current = this->stable_orientation_;
    } else {
      this->debounce_invalid_ = 0;
    }
  }

  Orientation stable = this->stable_orientation_;
  if (current != Orientation::INVALID) {
    if (current == Orientation::SIDE && this->stable_orientation_ == Orientation::DOWN_TILT &&
        this->debounce_side_ < 10) {
      this->debounce_side_++;
      current = this->stable_orientation_;
    } else {
      this->debounce_side_ = 0;
    }
    stable = current;

    if (current == Orientation::SIDE_REV && this->stable_orientation_ == Orientation::DOWN_TILT_REV &&
        this->debounce_side_rev_ < 10) {
      this->debounce_side_rev_++;
      current = this->stable_orientation_;
      stable = this->stable_orientation_;
    } else {
      this->debounce_side_rev_ = 0;
    }
  }

  Orientation previous_raw = this->raw_orientation_;
  this->stable_orientation_ = stable;
  this->raw_orientation_ = current;

  if (current != previous_raw) {
    ESP_LOGD(TAG, "Orientation %s -> %s (ax=%d ay=%d az=%d)", orientation_to_string(previous_raw),
             orientation_to_string(current), angle_x, angle_y, angle_z);
#ifdef USE_TEXT_SENSOR
    if (this->orientation_text_sensor_ != nullptr) {
      this->orientation_text_sensor_->publish_state(orientation_to_string(this->stable_orientation_));
    }
#endif
  }

  // Vibration detection based on the change in energy between windows.
  int32_t delta = std::abs(energy_sum - this->last_energy_sum_);
  if (delta < 1000) {
    this->vibration_high_count_ = 0;
    this->vibration_low_count_++;
  } else {
    this->vibration_high_count_++;
    this->vibration_low_count_ = 0;
  }
  if (delta > 5000 || this->vibration_high_count_ > 4) {
    this->vibrating_ = true;
    this->vibration_high_count_ = 0;
  }
  if (this->vibration_low_count_ > 9) {
    this->vibrating_ = false;
    this->vibration_low_count_ = 0;
  }
  this->last_energy_sum_ = energy_sum;

  if (this->vibrating_ != this->last_published_vibrating_) {
    ESP_LOGD(TAG, "Vibration %s", this->vibrating_ ? "true" : "false");
    this->last_published_vibrating_ = this->vibrating_;
#ifdef USE_BINARY_SENSOR
    if (this->vibration_binary_sensor_ != nullptr) {
      this->vibration_binary_sensor_->publish_state(this->vibrating_);
    }
#endif
  }

#ifdef USE_SENSOR
  if (this->tilt_angle_sensor_ != nullptr) {
    this->tilt_angle_sensor_->publish_state(this->output_angle_z_);
  }
#endif
}

}  // namespace esphome::aqara_fp2_accel
