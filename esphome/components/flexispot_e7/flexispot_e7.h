#pragma once

#include "esphome/core/component.h"
#include "esphome/core/gpio.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/components/uart/uart.h"

namespace esphome::flexispot_e7 {

// Command words sent to the desk controller. The value is the two payload
// bytes of the frame (high byte first). The full frame is assembled in
// send_command() together with the CRC-16/Modbus checksum.
enum FlexiSpotCommand : uint16_t {
  FLEXISPOT_CMD_WAKE = 0x0000,    // Silently query height (does not light the display)
  FLEXISPOT_CMD_UP = 0x0100,      // Nudge up
  FLEXISPOT_CMD_DOWN = 0x0200,    // Nudge down
  FLEXISPOT_CMD_MEMORY = 0x2000,  // "M" button - wakes display and reports height
  FLEXISPOT_CMD_PRESET_1 = 0x0400,
  FLEXISPOT_CMD_PRESET_2 = 0x0800,
  FLEXISPOT_CMD_PRESET_3 = 0x1000,  // Factory "stand" position on many E7 units
  FLEXISPOT_CMD_PRESET_4 = 0x0001,  // Factory "sit" position on many E7 units
};

// Central hub that owns the UART link to the desk controller. Sensor, cover and
// button platforms attach to it to read the height and to send commands.
class FlexiSpotE7 : public Component, public uart::UARTDevice {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_screen_pin(GPIOPin *pin) { this->screen_pin_ = pin; }
  void set_wake_interval(uint32_t interval) { this->wake_interval_ = interval; }

  // Register a listener that is called with the decoded height (in cm) whenever
  // it changes. Templated so it accepts both std::function and lightweight
  // forwarder structs without forcing a heap allocation.
  template<typename F> void add_on_height_callback(F &&callback) {
    this->height_callback_.add(std::forward<F>(callback));
  }

  // Last decoded height in cm, or NAN if nothing has been read yet.
  float get_height() const { return this->current_height_; }
  bool has_height() const { return !std::isnan(this->current_height_); }

  // Send a single command frame to the desk.
  void send_command(FlexiSpotCommand command);
  void move_up() { this->send_command(FLEXISPOT_CMD_UP); }
  void move_down() { this->send_command(FLEXISPOT_CMD_DOWN); }

 protected:
  // Polling state machine used to keep the height reading fresh without lighting
  // up the desk's display.
  enum class PollState {
    BOOT_WAIT,  // Waiting after boot before the first command
    IDLE,       // Slow polling to detect changes
    ACTIVE,     // Fast polling while the desk is moving
  };

  void reset_buffer_();
  void process_packet_();
  int decode_7segment_(uint8_t byte);
  bool has_decimal_(uint8_t byte) { return (byte & 0x80) == 0x80; }
  void publish_height_(float height);

  GPIOPin *screen_pin_{nullptr};
  CallbackManager<void(float)> height_callback_;

  // Incoming UART frame buffer. The largest frames observed are well under 32
  // bytes; oversized length fields are rejected in loop().
  uint8_t buffer_[32];
  size_t buffer_index_{0};

  float current_height_{NAN};

  PollState poll_state_{PollState::BOOT_WAIT};
  uint32_t boot_time_{0};
  uint32_t last_activity_time_{0};
  uint32_t last_command_time_{0};
  uint32_t wake_interval_{3000};

  // Timing constants (milliseconds).
  static const uint32_t BOOT_DELAY = 10000;          // Wait before the first command
  static const uint32_t ACTIVE_POLL_INTERVAL = 330;  // Fast poll while moving
  static const uint32_t ACTIVITY_TIMEOUT = 5000;     // No change for this long -> idle
};

}  // namespace esphome::flexispot_e7
