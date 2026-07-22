#include "aqara_fp2.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include <cstdio>

namespace esphome::aqara_fp2 {

static const char *const TAG = "aqara_fp2";

// Transport framing constants.
static constexpr uint8_t FRAME_SYNC = 0x55;
static constexpr uint8_t FRAME_VER_H = 0x00;
static constexpr uint8_t FRAME_VER_L = 0x01;
static constexpr uint8_t FRAME_HEADER_LEN = 7;  // sync, ver_h, ver_l, seq, opcode, len_h, len_l
// Each target record in a location report is a fixed 14 bytes.
static constexpr uint8_t TARGET_RECORD_LEN = 14;

// CRC16/MODBUS over the given buffer.
static uint16_t crc16(const uint8_t *data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; bit++) {
      if (crc & 0x0001) {
        crc = (crc >> 1) ^ 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }
  return crc;
}

// Header checksum used by the transport: the low byte of ~(sum - 1) over the 7 header bytes.
static uint8_t header_checksum(uint16_t header_sum) { return static_cast<uint8_t>(~(header_sum - 1)); }

void FP2Component::add_zone(uint8_t id, const GridMap &grid, uint8_t sensitivity, binary_sensor::BinarySensor *presence,
                            binary_sensor::BinarySensor *motion) {
  FP2Zone zone;
  zone.id = id;
  zone.grid = grid;
  zone.sensitivity = sensitivity;
#ifdef USE_BINARY_SENSOR
  zone.presence = presence;
  zone.motion = motion;
#endif
  this->zones_.push_back(zone);
}

void FP2Component::setup() {
  ESP_LOGCONFIG(TAG, "Running setup");
  this->perform_reset_();
}

void FP2Component::perform_reset_() {
  if (this->reset_pin_ != nullptr) {
    this->reset_pin_->setup();
    this->reset_pin_->digital_write(false);
    delay(100);  // NOLINT: hardware reset pulse, runs once at boot
    this->reset_pin_->digital_write(true);
    ESP_LOGD(TAG, "Radar reset, waiting for heartbeat");
  }
}

void FP2Component::loop() {
  while (this->available()) {
    uint8_t byte;
    this->read_byte(&byte);
    this->handle_incoming_byte_(byte);
  }

  if (!this->init_done_ && this->last_heartbeat_millis_ != 0) {
    this->run_initialization_();
  }

  this->process_command_queue_();
}

void FP2Component::run_initialization_() {
  ESP_LOGD(TAG, "Heartbeat received, sending configuration");
  this->init_done_ = true;

  this->enqueue_uint8_(AttrId::MONITOR_MODE, 0);
  this->enqueue_uint8_(AttrId::LEFT_RIGHT_REVERSE, this->left_right_reverse_ ? 2 : 0);
  this->enqueue_uint8_(AttrId::PRESENCE_DETECT_SENSITIVITY, this->presence_sensitivity_);
  this->enqueue_uint8_(AttrId::CLOSING_SETTING, 1);
  this->enqueue_uint16_(AttrId::ZONE_CLOSE_AWAY_ENABLE, 0x0001);
  this->enqueue_bool_(AttrId::PEOPLE_COUNT_REPORT_ENABLE, true);
  this->enqueue_bool_(AttrId::PEOPLE_NUMBER_ENABLE, true);
  this->enqueue_bool_(AttrId::TARGET_TYPE_ENABLE, true);
  this->enqueue_uint8_(AttrId::WALL_CORNER_POS, this->mounting_position_);
  this->enqueue_uint8_(AttrId::DWELL_TIME_ENABLE, 0);
  this->enqueue_uint8_(AttrId::WALK_DISTANCE_ENABLE, 0);
  this->enqueue_bool_(AttrId::THERMO_EN, true);
  this->enqueue_uint8_(AttrId::THERMO_DATA, 1);

  if (this->has_interference_grid_) {
    this->enqueue_blob_(AttrId::INTERFERENCE_MAP, this->interference_grid_.data(), GRID_SIZE);
  }
  if (this->has_exit_grid_) {
    this->enqueue_blob_(AttrId::ENTRY_EXIT_MAP, this->exit_grid_.data(), GRID_SIZE);
  }
  if (this->has_edge_grid_) {
    this->enqueue_blob_(AttrId::EDGE_MAP, this->edge_grid_.data(), GRID_SIZE);
  }

  uint8_t activations[32] = {0};
  for (const auto &zone : this->zones_) {
    uint8_t map_payload[1 + GRID_SIZE];
    map_payload[0] = zone.id;
    std::copy(zone.grid.begin(), zone.grid.end(), map_payload + 1);
    this->enqueue_blob_(AttrId::ZONE_MAP, map_payload, sizeof(map_payload));
    this->enqueue_uint16_(AttrId::ZONE_SENSITIVITY, (zone.id << 8) | (zone.sensitivity & 0xFF));
    if (zone.id < sizeof(activations)) {
      activations[zone.id] = zone.id;
    }
  }
  this->enqueue_blob_(AttrId::ZONE_ACTIVATION_LIST, activations, sizeof(activations));
  for (const auto &zone : this->zones_) {
    this->enqueue_uint16_(AttrId::ZONE_CLOSE_AWAY_ENABLE, (zone.id << 8) | 1);
  }

  // Publish known post-reset states.
#ifdef USE_BINARY_SENSOR
  if (this->presence_binary_sensor_ != nullptr) {
    this->presence_binary_sensor_->publish_state(false);
  }
  if (this->motion_binary_sensor_ != nullptr) {
    this->motion_binary_sensor_->publish_state(false);
  }
  if (this->target_binary_sensor_ != nullptr) {
    this->target_binary_sensor_->publish_state(false);
  }
  for (const auto &zone : this->zones_) {
    if (zone.presence != nullptr) {
      zone.presence->publish_state(false);
    }
    if (zone.motion != nullptr) {
      zone.motion->publish_state(false);
    }
  }
#endif
}

// --- Outgoing command ring buffer -----------------------------------------------------------

void FP2Component::enqueue_command_(OpCode type, AttrId attr_id, DataType data_type, const uint8_t *value,
                                    uint8_t value_len, bool front) {
  if (this->queue_count_ >= CMD_QUEUE_SIZE) {
    ESP_LOGE(TAG, "Command queue full, dropping 0x%04X", static_cast<uint16_t>(attr_id));
    return;
  }

  uint8_t index;
  if (front) {
    this->queue_head_ = (this->queue_head_ + CMD_QUEUE_SIZE - 1) % CMD_QUEUE_SIZE;
    index = this->queue_head_;
  } else {
    index = (this->queue_head_ + this->queue_count_) % CMD_QUEUE_SIZE;
  }
  this->queue_count_++;

  FP2Command &cmd = this->queue_[index];
  cmd.type = type;
  cmd.attr_id = attr_id;
  cmd.retry_count = 0;

  uint8_t len = 0;
  cmd.data[len++] = static_cast<uint8_t>(static_cast<uint16_t>(attr_id) >> 8);
  cmd.data[len++] = static_cast<uint8_t>(static_cast<uint16_t>(attr_id) & 0xFF);
  cmd.data[len++] = static_cast<uint8_t>(data_type);
  for (uint8_t i = 0; i < value_len && len < MAX_CMD_DATA; i++) {
    cmd.data[len++] = value[i];
  }
  cmd.data_len = len;
}

void FP2Component::enqueue_blob_(AttrId attr_id, const uint8_t *content, uint8_t content_len) {
  if (this->queue_count_ >= CMD_QUEUE_SIZE) {
    ESP_LOGE(TAG, "Command queue full, dropping 0x%04X", static_cast<uint16_t>(attr_id));
    return;
  }
  uint8_t index = (this->queue_head_ + this->queue_count_) % CMD_QUEUE_SIZE;
  this->queue_count_++;

  FP2Command &cmd = this->queue_[index];
  cmd.type = OpCode::WRITE;
  cmd.attr_id = attr_id;
  cmd.retry_count = 0;

  uint8_t len = 0;
  cmd.data[len++] = static_cast<uint8_t>(static_cast<uint16_t>(attr_id) >> 8);
  cmd.data[len++] = static_cast<uint8_t>(static_cast<uint16_t>(attr_id) & 0xFF);
  cmd.data[len++] = static_cast<uint8_t>(DataType::BINARY);
  cmd.data[len++] = static_cast<uint8_t>(content_len >> 8);
  cmd.data[len++] = static_cast<uint8_t>(content_len & 0xFF);
  for (uint8_t i = 0; i < content_len && len < MAX_CMD_DATA; i++) {
    cmd.data[len++] = content[i];
  }
  cmd.data_len = len;
}

void FP2Component::process_command_queue_() {
  uint32_t now = millis();

  // A write is in flight: wait for its ACK, retrying on timeout. The in-flight command lives in
  // inflight_, separate from the queue, so ACKs/responses can still be front-inserted safely.
  if (this->waiting_for_ack_ != AttrId::INVALID) {
    if (now - this->last_command_sent_millis_ <= ACK_TIMEOUT_MS) {
      return;  // still waiting
    }
    if (++this->inflight_.retry_count >= MAX_RETRIES) {
      ESP_LOGW(TAG, "Command 0x%04X timed out, dropping", static_cast<uint16_t>(this->inflight_.attr_id));
      this->waiting_for_ack_ = AttrId::INVALID;
      return;
    }
    ESP_LOGW(TAG, "Command 0x%04X timed out, retry %u", static_cast<uint16_t>(this->inflight_.attr_id),
             this->inflight_.retry_count);
    this->send_command_(this->inflight_);
    return;
  }

  if (this->queue_count_ == 0) {
    return;
  }

  // Pop the head command into the in-flight slot and send it.
  this->inflight_ = this->queue_[this->queue_head_];
  this->inflight_.retry_count = 0;
  this->queue_head_ = (this->queue_head_ + 1) % CMD_QUEUE_SIZE;
  this->queue_count_--;
  this->send_command_(this->inflight_);
}

void FP2Component::send_command_(const FP2Command &cmd) {
  uint8_t frame[FRAME_HEADER_LEN + 1 + MAX_CMD_DATA + 2];
  uint8_t pos = 0;
  frame[pos++] = FRAME_SYNC;
  frame[pos++] = FRAME_VER_H;
  frame[pos++] = FRAME_VER_L;
  frame[pos++] = this->tx_seq_++;
  frame[pos++] = static_cast<uint8_t>(cmd.type);
  frame[pos++] = static_cast<uint8_t>(cmd.data_len >> 8);
  frame[pos++] = static_cast<uint8_t>(cmd.data_len & 0xFF);

  uint16_t sum = 0;
  for (uint8_t i = 0; i < FRAME_HEADER_LEN; i++) {
    sum += frame[i];
  }
  frame[pos++] = header_checksum(sum);

  for (uint8_t i = 0; i < cmd.data_len; i++) {
    frame[pos++] = cmd.data[i];
  }

  uint16_t crc = crc16(frame, pos);
  frame[pos++] = static_cast<uint8_t>(crc & 0xFF);
  frame[pos++] = static_cast<uint8_t>(crc >> 8);

  this->write_array(frame, pos);
  this->last_command_sent_millis_ = millis();

  // Only attribute writes are acknowledged; everything else is fire-and-forget.
  this->waiting_for_ack_ = (cmd.type == OpCode::WRITE) ? cmd.attr_id : AttrId::INVALID;
}

void FP2Component::send_ack_(AttrId attr_id) {
  uint8_t value[1] = {0};  // unused; ACK carries a VOID type tag
  // ACKs jump the queue so the radar sees them promptly.
  this->enqueue_command_(OpCode::ACK, attr_id, DataType::VOID, value, 0, /*front=*/true);
}

void FP2Component::send_reverse_response_(AttrId attr_id, uint8_t value) {
  this->enqueue_command_(OpCode::READ, attr_id, DataType::UINT8, &value, 1);
}

// --- Incoming frame decoder ------------------------------------------------------------------

void FP2Component::handle_incoming_byte_(uint8_t byte) {
  switch (this->rx_state_) {
    case RxState::SYNC:
      if (byte == FRAME_SYNC) {
        this->header_sum_ = byte;
        this->rx_state_ = RxState::VER_H;
      }
      return;
    case RxState::VER_H:
      this->header_sum_ += byte;
      this->rx_state_ = (byte == FRAME_VER_H) ? RxState::VER_L : RxState::SYNC;
      return;
    case RxState::VER_L:
      this->header_sum_ += byte;
      this->rx_state_ = (byte == FRAME_VER_L) ? RxState::SEQ : RxState::SYNC;
      return;
    case RxState::SEQ:
      this->header_sum_ += byte;
      this->rx_seq_ = byte;
      this->rx_state_ = RxState::OPCODE;
      return;
    case RxState::OPCODE:
      this->header_sum_ += byte;
      this->rx_opcode_ = byte;
      this->rx_state_ = RxState::LEN_H;
      return;
    case RxState::LEN_H:
      this->header_sum_ += byte;
      this->rx_len_ = static_cast<uint16_t>(byte) << 8;
      this->rx_state_ = RxState::LEN_L;
      return;
    case RxState::LEN_L:
      this->header_sum_ += byte;
      this->rx_len_ |= byte;
      this->rx_state_ = RxState::HEADER_CHECK;
      return;
    case RxState::HEADER_CHECK:
      if (byte != header_checksum(this->header_sum_)) {
        ESP_LOGW(TAG, "Header checksum mismatch");
        this->rx_state_ = RxState::SYNC;
      } else if (this->rx_len_ > MAX_RX_PAYLOAD) {
        ESP_LOGW(TAG, "Payload too large (%u bytes), dropping", this->rx_len_);
        this->rx_state_ = RxState::SYNC;
      } else {
        this->rx_pos_ = 0;
        this->rx_state_ = (this->rx_len_ == 0) ? RxState::CRC_L : RxState::PAYLOAD;
      }
      return;
    case RxState::PAYLOAD:
      this->rx_payload_[this->rx_pos_++] = byte;
      if (this->rx_pos_ >= this->rx_len_) {
        this->rx_state_ = RxState::CRC_L;
      }
      return;
    case RxState::CRC_L:
      this->rx_crc_ = byte;
      this->rx_state_ = RxState::CRC_H;
      return;
    case RxState::CRC_H: {
      this->rx_crc_ |= static_cast<uint16_t>(byte) << 8;
      this->rx_state_ = RxState::SYNC;

      // The CRC covers the 8 header bytes followed by the payload. Rebuild that contiguous
      // buffer so it can be checksummed in one pass.
      uint8_t check_buf[FRAME_HEADER_LEN + 1 + MAX_RX_PAYLOAD] = {
          FRAME_SYNC,
          FRAME_VER_H,
          FRAME_VER_L,
          this->rx_seq_,
          this->rx_opcode_,
          static_cast<uint8_t>(this->rx_len_ >> 8),
          static_cast<uint8_t>(this->rx_len_ & 0xFF),
          header_checksum(this->header_sum_),
      };
      std::copy(this->rx_payload_, this->rx_payload_ + this->rx_len_, check_buf + FRAME_HEADER_LEN + 1);
      uint16_t calc = crc16(check_buf, FRAME_HEADER_LEN + 1 + this->rx_len_);
      if (calc != this->rx_crc_) {
        ESP_LOGW(TAG, "Frame CRC mismatch");
        return;
      }
      if (this->rx_len_ >= 2) {
        AttrId attr_id = static_cast<AttrId>((static_cast<uint16_t>(this->rx_payload_[0]) << 8) | this->rx_payload_[1]);
        this->handle_frame_(static_cast<OpCode>(this->rx_opcode_), attr_id);
      }
      return;
    }
  }
}

void FP2Component::handle_frame_(OpCode op, AttrId attr_id) {
  switch (op) {
    case OpCode::ACK:
      // The acknowledged write already left the queue when it was sent; just clear the wait.
      if (this->waiting_for_ack_ == attr_id) {
        this->waiting_for_ack_ = AttrId::INVALID;
      }
      return;
    case OpCode::REPORT:
      this->handle_report_(attr_id);
      return;
    case OpCode::RESPONSE:
      // A 2-byte response (attribute id only) is the radar asking us to report a value.
      if (this->rx_len_ == 2) {
        this->handle_reverse_read_(attr_id);
      }
      return;
    default:
      return;
  }
}

void FP2Component::handle_report_(AttrId attr_id) {
  // Every report except the heartbeat must be acknowledged.
  if (attr_id != AttrId::RADAR_SW_VERSION) {
    this->send_ack_(attr_id);
  }

  const uint8_t *payload = this->rx_payload_;
  uint16_t len = this->rx_len_;

  switch (attr_id) {
    case AttrId::RADAR_SW_VERSION:
      this->last_heartbeat_millis_ = millis();
#ifdef USE_TEXT_SENSOR
      if (this->radar_version_text_sensor_ != nullptr && len == 4 && payload[2] == 0x00) {
        char buf[4];
        snprintf(buf, sizeof(buf), "%u", payload[3]);
        if (this->radar_version_text_sensor_->state != buf) {
          this->radar_version_text_sensor_->publish_state(buf);
        }
      }
#endif
      return;

#ifdef USE_BINARY_SENSOR
    case AttrId::PRESENCE_DETECT:
      if (this->presence_binary_sensor_ != nullptr && len == 4 && payload[2] == 0x00) {
        this->presence_binary_sensor_->publish_state(payload[3] != 0);
      }
      return;
    case AttrId::MOTION_DETECT:
      if (this->motion_binary_sensor_ != nullptr && len == 4 && payload[2] == 0x00) {
        this->motion_binary_sensor_->publish_state(payload[3] != 0);
      }
      return;
    case AttrId::ZONE_PRESENCE:
      if (len >= 5 && payload[2] == 0x01) {
        for (auto &zone : this->zones_) {
          if (zone.id == payload[3] && zone.presence != nullptr) {
            zone.presence->publish_state(payload[4] != 0);
            break;
          }
        }
      }
      return;
    case AttrId::DETECT_ZONE_MOTION:
      if (len >= 5 && payload[2] == 0x01) {
        for (auto &zone : this->zones_) {
          if (zone.id == payload[3] && zone.motion != nullptr) {
            zone.motion->publish_state(payload[4] != 0);
            break;
          }
        }
      }
      return;
#endif

    case AttrId::TEMPERATURE:
#ifdef USE_SENSOR
      if (len == 5 && payload[2] == 0x01) {
        int16_t temp = static_cast<int16_t>((payload[3] << 8) | payload[4]);
        SAFE_PUBLISH_SENSOR(this->radar_temperature_sensor_, temp);
      }
#endif
      return;

    case AttrId::LOCATION_TRACKING_DATA:
      this->handle_location_report_();
      return;

    default:
      return;
  }
}

void FP2Component::handle_location_report_() {
  if (!this->location_reporting_active_) {
    return;
  }
  const uint8_t *payload = this->rx_payload_;
  // Payload: [attr 2][type 0x06][blob len 2][count 1][target record 14]...
  if (this->rx_len_ < 6 || payload[2] != static_cast<uint8_t>(DataType::BINARY)) {
    return;
  }
  uint8_t count = payload[5];
  if (count > MAX_TARGETS) {
    count = MAX_TARGETS;
  }

#ifdef USE_SENSOR
  for (uint8_t i = 0; i < MAX_TARGETS; i++) {
    uint16_t offset = 6 + static_cast<uint16_t>(i) * TARGET_RECORD_LEN;
    if (i < count && offset + TARGET_RECORD_LEN <= this->rx_len_) {
      // Record layout: id(1) x(2) y(2) z(2) velocity(2) snr(2) classifier(1) posture(1) active(1).
      int16_t x = static_cast<int16_t>((payload[offset + 1] << 8) | payload[offset + 2]);
      int16_t y = static_cast<int16_t>((payload[offset + 3] << 8) | payload[offset + 4]);
      int16_t z = static_cast<int16_t>((payload[offset + 5] << 8) | payload[offset + 6]);
      int16_t speed = static_cast<int16_t>((payload[offset + 7] << 8) | payload[offset + 8]);
      SAFE_PUBLISH_SENSOR(this->target_x_sensors_[i], x);
      SAFE_PUBLISH_SENSOR(this->target_y_sensors_[i], y);
      SAFE_PUBLISH_SENSOR(this->target_z_sensors_[i], z);
      SAFE_PUBLISH_SENSOR(this->target_speed_sensors_[i], speed);
    } else {
      SAFE_PUBLISH_SENSOR_UNKNOWN(this->target_x_sensors_[i]);
      SAFE_PUBLISH_SENSOR_UNKNOWN(this->target_y_sensors_[i]);
      SAFE_PUBLISH_SENSOR_UNKNOWN(this->target_z_sensors_[i]);
      SAFE_PUBLISH_SENSOR_UNKNOWN(this->target_speed_sensors_[i]);
    }
  }
  SAFE_PUBLISH_SENSOR(this->target_count_sensor_, count);
#endif
#ifdef USE_BINARY_SENSOR
  if (this->target_binary_sensor_ != nullptr) {
    this->target_binary_sensor_->publish_state(count > 0);
  }
#endif
}

void FP2Component::handle_reverse_read_(AttrId attr_id) {
  switch (attr_id) {
    case AttrId::DEVICE_DIRECTION: {
      uint8_t orientation = this->accel_ != nullptr ? static_cast<uint8_t>(this->accel_->get_orientation())
                                                    : static_cast<uint8_t>(aqara_fp2_accel::Orientation::UP);
      this->send_reverse_response_(attr_id, orientation);
      return;
    }
    case AttrId::ANGLE_SENSOR_DATA: {
      uint8_t angle = this->accel_ != nullptr ? this->accel_->get_output_angle_z() : 0;
      this->send_reverse_response_(attr_id, angle);
      return;
    }
    default:
      ESP_LOGD(TAG, "Unhandled reverse query 0x%04X", static_cast<uint16_t>(attr_id));
      return;
  }
}

void FP2Component::set_location_reporting_enabled(bool enabled) {
  this->location_reporting_active_ = enabled;
  this->enqueue_uint8_(AttrId::LOCATION_REPORT_ENABLE, enabled ? 1 : 0);
#ifdef USE_SENSOR
  if (!enabled) {
    for (uint8_t i = 0; i < MAX_TARGETS; i++) {
      SAFE_PUBLISH_SENSOR_UNKNOWN(this->target_x_sensors_[i]);
      SAFE_PUBLISH_SENSOR_UNKNOWN(this->target_y_sensors_[i]);
      SAFE_PUBLISH_SENSOR_UNKNOWN(this->target_z_sensors_[i]);
      SAFE_PUBLISH_SENSOR_UNKNOWN(this->target_speed_sensors_[i]);
    }
  }
#endif
}

void FP2Component::dump_config() {
  ESP_LOGCONFIG(TAG,
                "Aqara FP2:\n"
                "  Mounting position: %u\n"
                "  Left/right reverse: %s\n"
                "  Presence sensitivity: %u\n"
                "  Zones: %u",
                this->mounting_position_, YESNO(this->left_right_reverse_), this->presence_sensitivity_,
                this->zones_.size());
  LOG_PIN("  Reset pin: ", this->reset_pin_);
  this->check_uart_settings(890000);
#ifdef USE_BINARY_SENSOR
  LOG_BINARY_SENSOR("  ", "Presence", this->presence_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "Motion", this->motion_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "Target", this->target_binary_sensor_);
#endif
#ifdef USE_TEXT_SENSOR
  LOG_TEXT_SENSOR("  ", "Radar version", this->radar_version_text_sensor_);
#endif
}

#ifdef USE_SWITCH
void LocationReportSwitch::write_state(bool state) {
  this->parent_->set_location_reporting_enabled(state);
  this->publish_state(state);
}
#endif

}  // namespace esphome::aqara_fp2
