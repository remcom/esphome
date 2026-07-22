#pragma once

#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/components/i2c/i2c.h"
#ifdef USE_TEXT_SENSOR
#include "esphome/components/text_sensor/text_sensor.h"
#endif
#ifdef USE_BINARY_SENSOR
#include "esphome/components/binary_sensor/binary_sensor.h"
#endif
#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif

#include <array>
#include <cstdint>

namespace esphome::aqara_fp2_accel {

// Number of raw samples averaged before an orientation/vibration decision is made.
static constexpr uint8_t ACC_SAMPLE_COUNT = 10;

// Mounting orientation reported to the radar. The values match the codes the stock
// Aqara firmware sends back to the radar coprocessor in a "device direction" query.
enum class Orientation : uint8_t {
  UP = 0,
  UP_TILT = 1,
  UP_TILT_REV = 2,
  SIDE = 3,
  SIDE_REV = 4,
  DOWN = 5,
  DOWN_TILT = 6,
  DOWN_TILT_REV = 7,
  INVALID = 8,
};

const char *orientation_to_string(Orientation orientation);

class AqaraFP2Accel : public PollingComponent, public i2c::I2CDevice {
#ifdef USE_TEXT_SENSOR
  // Human-readable mounting orientation.
  SUB_TEXT_SENSOR(orientation)
#endif
#ifdef USE_BINARY_SENSOR
  // True while the device is being moved/tapped.
  SUB_BINARY_SENSOR(vibration)
#endif
#ifdef USE_SENSOR
  // Tilt angle of the Z axis relative to vertical, in degrees.
  SUB_SENSOR(tilt_angle)
#endif

 public:
  void setup() override;
  void update() override;
  void dump_config() override;

  float get_setup_priority() const override { return setup_priority::DATA; }

  // Accessors consumed by the aqara_fp2 radar component when the radar asks for its
  // mounting orientation. Safe to call from the main loop (single-threaded).
  Orientation get_orientation() const { return this->stable_orientation_; }
  uint8_t get_output_angle_z() const { return static_cast<uint8_t>(this->output_angle_z_); }
  bool is_vibrating() const { return this->vibrating_; }

 protected:
  bool read_accel_xyz_(int16_t *x, int16_t *y, int16_t *z);
  void process_samples_(int32_t acc_x, int32_t acc_y, int32_t acc_z, int32_t energy_sum);

  // Rolling sample buffers.
  std::array<int16_t, ACC_SAMPLE_COUNT> acc_x_buf_{};
  std::array<int16_t, ACC_SAMPLE_COUNT> acc_y_buf_{};
  std::array<int16_t, ACC_SAMPLE_COUNT> acc_z_buf_{};
  uint8_t samples_read_{0};

  // Orientation state machine.
  Orientation stable_orientation_{Orientation::INVALID};
  Orientation raw_orientation_{Orientation::INVALID};
  int16_t output_angle_z_{0};
  uint8_t debounce_invalid_{0};
  uint8_t debounce_side_{0};
  uint8_t debounce_side_rev_{0};

  // Vibration detection.
  int32_t last_energy_sum_{0};
  uint8_t vibration_high_count_{0};
  uint8_t vibration_low_count_{0};
  bool vibrating_{false};
  bool last_published_vibrating_{false};
};

}  // namespace esphome::aqara_fp2_accel
