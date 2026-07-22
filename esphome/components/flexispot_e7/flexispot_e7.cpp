#include "flexispot_e7.h"
#include "esphome/core/log.h"

namespace esphome::flexispot_e7 {

static const char *const TAG = "flexispot_e7";

// Frame markers on the wire.
static const uint8_t FRAME_START_A = 0x9b;
static const uint8_t FRAME_START_B = 0x98;
static const uint8_t FRAME_END = 0x9d;

// CRC-16/Modbus over the frame payload (type + length + command bytes).
static uint16_t crc16_modbus(const uint8_t *data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; bit++) {
      if (crc & 1) {
        crc = (crc >> 1) ^ 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }
  return crc;
}

void FlexiSpotE7::setup() {
  ESP_LOGCONFIG(TAG, "Running setup");
  if (this->screen_pin_ != nullptr) {
    this->screen_pin_->setup();
    this->screen_pin_->digital_write(true);  // Hold the display enable line high so the desk responds
  }
  this->boot_time_ = millis();
  this->poll_state_ = PollState::BOOT_WAIT;
}

void FlexiSpotE7::dump_config() {
  ESP_LOGCONFIG(TAG, "FlexiSpot E7:");
  ESP_LOGCONFIG(TAG, "  Wake interval: %" PRIu32 " ms", this->wake_interval_);
  LOG_PIN("  Screen Pin: ", this->screen_pin_);
  this->check_uart_settings(9600);
}

void FlexiSpotE7::loop() {
  // Scan for a valid start byte.
  while (this->buffer_index_ == 0 && this->available()) {
    uint8_t byte;
    this->read_byte(&byte);
    if (byte == FRAME_START_A || byte == FRAME_START_B) {
      this->buffer_[this->buffer_index_++] = byte;
    }
  }

  // Read the length byte.
  if (this->buffer_index_ == 1 && this->available()) {
    this->read_byte(&this->buffer_[this->buffer_index_++]);
  }

  // Once the length is known, batch-read the rest of the frame.
  if (this->buffer_index_ >= 2) {
    int expected_length = this->buffer_[1] + 2;  // payload length + start + end bytes
    if (expected_length > (int) sizeof(this->buffer_)) {
      ESP_LOGW(TAG, "Frame length %d exceeds buffer, resetting", expected_length);
      this->reset_buffer_();
    } else {
      int remaining = expected_length - (int) this->buffer_index_;
      if (remaining > 0 && this->available() >= remaining) {
        this->read_array(this->buffer_ + this->buffer_index_, remaining);
        this->buffer_index_ += remaining;
      }
      if ((int) this->buffer_index_ == expected_length) {
        if (this->buffer_[expected_length - 1] == FRAME_END) {
          this->process_packet_();
        } else {
          ESP_LOGW(TAG, "Invalid end byte: 0x%02X", this->buffer_[expected_length - 1]);
        }
        this->reset_buffer_();
      }
    }
  }

  // Re-read the clock: last_activity_time_ may have just been updated in
  // process_packet_(), and a stale value would underflow the timeout check.
  const uint32_t now = millis();

  switch (this->poll_state_) {
    case PollState::BOOT_WAIT:
      // Wake the display and read the initial height once after boot.
      if (now - this->boot_time_ >= BOOT_DELAY) {
        ESP_LOGI(TAG, "Requesting initial desk height");
        this->send_command(FLEXISPOT_CMD_MEMORY);
        this->poll_state_ = PollState::IDLE;
      }
      break;

    case PollState::IDLE:
      if (now - this->last_command_time_ >= this->wake_interval_) {
        this->send_command(FLEXISPOT_CMD_WAKE);
      }
      break;

    case PollState::ACTIVE:
      if (now - this->last_command_time_ >= ACTIVE_POLL_INTERVAL) {
        this->send_command(FLEXISPOT_CMD_WAKE);
      }
      if (now - this->last_activity_time_ >= ACTIVITY_TIMEOUT) {
        ESP_LOGI(TAG, "Desk stopped, slowing poll rate");
        this->poll_state_ = PollState::IDLE;
      }
      break;
  }
}

void FlexiSpotE7::reset_buffer_() {
  this->buffer_index_ = 0;
  memset(this->buffer_, 0, sizeof(this->buffer_));
}

int FlexiSpotE7::decode_7segment_(uint8_t byte) {
  uint8_t segments = byte & 0x7F;  // Strip the decimal point bit
  if (segments == 0x00) {
    return -2;  // Blank segment
  }

  // Standard 7-segment bit patterns for 0-9 and the minus sign.
  static const uint8_t PATTERNS[11] = {
      0b00111111,  // 0
      0b00000110,  // 1
      0b01011011,  // 2
      0b01001111,  // 3
      0b01100110,  // 4
      0b01101101,  // 5
      0b01111101,  // 6
      0b00000111,  // 7
      0b01111111,  // 8
      0b01101111,  // 9
      0b01000000,  // minus sign
  };
  for (int i = 0; i < 11; i++) {
    if (segments == PATTERNS[i]) {
      return i;
    }
  }
  ESP_LOGD(TAG, "Unknown 7-segment pattern: 0x%02X", segments);
  return -1;
}

void FlexiSpotE7::process_packet_() {
  const uint8_t msg_length = this->buffer_[1];
  const uint8_t msg_type = this->buffer_[2];

  // Message type 0x12 = height broadcast (three 7-segment display bytes).
  if (msg_type == 0x12 && msg_length == 7) {
    const uint8_t digit1 = this->buffer_[3];  // Hundreds
    const uint8_t digit2 = this->buffer_[4];  // Tens
    const uint8_t digit3 = this->buffer_[5];  // Ones

    int d1 = this->decode_7segment_(digit1);
    int d2 = this->decode_7segment_(digit2);
    int d3 = this->decode_7segment_(digit3);

    // Fully blank display: the desk is waking up.
    if (d1 == -2 && d2 == -2 && d3 == -2) {
      ESP_LOGD(TAG, "Display blank (waking up), ignoring");
      return;
    }
    // A blank leading digit is normal for heights below 100 cm.
    if (d1 == -2) {
      d1 = 0;
    }
    // Unknown patterns or unexpected blanks: not a height reading.
    if (d1 < 0 || d2 < 0 || d3 < 0) {
      ESP_LOGD(TAG, "Non-height data (d1=%d, d2=%d, d3=%d)", d1, d2, d3);
      return;
    }
    if (d1 == 0 && d2 == 0 && d3 == 0) {
      ESP_LOGD(TAG, "Blank display, ignoring");
      return;
    }
    if (d2 == 10) {
      ESP_LOGD(TAG, "Desk showing minus sign (resetting)");
      return;
    }

    int height_raw = (d1 * 100) + (d2 * 10) + d3;
    float new_height = this->has_decimal_(digit2) ? height_raw / 10.0f : static_cast<float>(height_raw);
    ESP_LOGD(TAG, "Height decoded: %.1f cm", new_height);

    // Track activity for the poll state machine only when the height actually
    // changes, so the idle countdown starts as soon as the desk stops moving.
    if (new_height != this->current_height_) {
      this->last_activity_time_ = millis();
      if (this->has_height() && this->poll_state_ == PollState::IDLE) {
        ESP_LOGI(TAG, "Height change detected, increasing poll rate");
        this->poll_state_ = PollState::ACTIVE;
      }
      this->publish_height_(new_height);
    }
    this->current_height_ = new_height;
  } else if (msg_type == 0x11) {
    // Heartbeat, ignore.
  } else {
    ESP_LOGV(TAG, "Unhandled message type: 0x%02X (length %d)", msg_type, msg_length);
  }
}

void FlexiSpotE7::publish_height_(float height) { this->height_callback_.call(height); }

void FlexiSpotE7::send_command(FlexiSpotCommand command) {
  const uint8_t b3 = command >> 8;
  const uint8_t b4 = command & 0xFF;
  const uint8_t payload[4] = {0x06, 0x02, b3, b4};
  const uint16_t crc = crc16_modbus(payload, sizeof(payload));
  const uint8_t frame[8] = {
      FRAME_START_A,
      payload[0],
      payload[1],
      payload[2],
      payload[3],
      static_cast<uint8_t>(crc >> 8),
      static_cast<uint8_t>(crc & 0xFF),
      FRAME_END,
  };
  this->write_array(frame, sizeof(frame));
  this->last_command_time_ = millis();
}

}  // namespace esphome::flexispot_e7
