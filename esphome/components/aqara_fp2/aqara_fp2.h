#pragma once

#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/core/gpio.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/ld24xx/ld24xx.h"
#ifdef USE_BINARY_SENSOR
#include "esphome/components/binary_sensor/binary_sensor.h"
#endif
#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif
#ifdef USE_TEXT_SENSOR
#include "esphome/components/text_sensor/text_sensor.h"
#endif
#ifdef USE_SWITCH
#include "esphome/components/switch/switch.h"
#endif

#include "esphome/components/aqara_fp2_accel/aqara_fp2_accel.h"

#include "esphome/core/helpers.h"

#include <array>
#include <cstdint>

namespace esphome::aqara_fp2 {

using ld24xx::SensorWithDedup;

// The Aqara FP2 tracks up to five simultaneous targets.
static constexpr uint8_t MAX_TARGETS = 5;

// Detection maps are 320-bit (20 rows x 16 columns) blobs.
static constexpr uint8_t GRID_SIZE = 40;
using GridMap = std::array<uint8_t, GRID_SIZE>;

// Largest data payload we ever build (zone map = subid + type + len + zone id + 40-byte grid).
static constexpr uint8_t MAX_CMD_DATA = 48;
// Depth of the outgoing command ring buffer. The initialization burst enqueues a fixed set of
// attribute writes plus roughly three commands per configured zone, so this bounds usable zones.
static constexpr uint8_t CMD_QUEUE_SIZE = 48;
// Largest report payload we accept from the radar (target list of MAX_TARGETS + framing).
static constexpr uint8_t MAX_RX_PAYLOAD = 96;

static constexpr uint32_t ACK_TIMEOUT_MS = 500;
static constexpr uint8_t MAX_RETRIES = 3;

// UART transport opcodes.
enum class OpCode : uint8_t {
  RESPONSE = 0x01,  // device -> host response, or (2-byte payload) a reverse-read request
  WRITE = 0x02,     // host -> device attribute write
  ACK = 0x03,       // acknowledgement
  READ = 0x04,      // host -> device read request, or reverse-read response
  REPORT = 0x05,    // device -> host asynchronous report
};

// Radar attribute identifiers exchanged over the transport.
enum class AttrId : uint16_t {
  RADAR_SW_VERSION = 0x0102,             // heartbeat / firmware version
  MONITOR_MODE = 0x0105,                 // detection direction
  CLOSING_SETTING = 0x0106,              // proximity setting
  ENTRY_EXIT_MAP = 0x0109,               // enter/exit label map (40B)
  INTERFERENCE_MAP = 0x0110,             // interference source map (40B)
  PRESENCE_DETECT_SENSITIVITY = 0x0111,  // global presence sensitivity (1-3)
  LOCATION_REPORT_ENABLE = 0x0112,       // enable/disable target location reports
  EDGE_MAP = 0x0107,                     // detection boundary map (40B)
  ZONE_MAP = 0x0114,                     // zone area map (1B id + 40B)
  DETECT_ZONE_MOTION = 0x0115,           // per-zone motion report
  WORK_MODE = 0x0116,
  LOCATION_TRACKING_DATA = 0x0117,  // per-target location report
  ANGLE_SENSOR_DATA = 0x0120,       // reverse-read: mounting tilt angle
  LEFT_RIGHT_REVERSE = 0x0122,      // left/right swap
  FALL_SENSITIVITY = 0x0123,
  TEMPERATURE = 0x0128,  // radar temperature report
  THERMO_EN = 0x0138,
  THERMO_DATA = 0x0141,
  ZONE_PRESENCE = 0x0142,     // per-zone presence report
  DEVICE_DIRECTION = 0x0143,  // reverse-read: mounting orientation
  ZONE_SENSITIVITY = 0x0151,  // per-zone sensitivity
  ZONE_CLOSE_AWAY_ENABLE = 0x0153,
  PEOPLE_COUNT_REPORT_ENABLE = 0x0158,
  PEOPLE_NUMBER_ENABLE = 0x0162,
  TARGET_TYPE_ENABLE = 0x0163,  // AI person detection
  MOTION_DETECT = 0x0103,       // global motion report
  PRESENCE_DETECT = 0x0104,     // global presence report
  WALL_CORNER_POS = 0x0170,     // mounting position
  DWELL_TIME_ENABLE = 0x0172,
  WALK_DISTANCE_ENABLE = 0x0173,
  ZONE_ACTIVATION_LIST = 0x0202,  // active zone list (32B)
  INVALID = 0xFFFF,
};

// Attribute data-type tags used inside a payload.
enum class DataType : uint8_t {
  UINT8 = 0x00,
  UINT16 = 0x01,
  UINT32 = 0x02,
  VOID = 0x03,
  BOOL = 0x04,
  STRING = 0x05,
  BINARY = 0x06,
};

// A queued outgoing command. The transport frame (sync, sequence, checksums) is built at send time;
// only the attribute payload is stored here.
struct FP2Command {
  OpCode type;
  AttrId attr_id;
  uint8_t data[MAX_CMD_DATA];
  uint8_t data_len;
  uint8_t retry_count;
};

// A user-defined detection zone with its own presence/motion entities.
struct FP2Zone {
  uint8_t id;
  uint8_t sensitivity;  // 1=low, 2=medium, 3=high
  GridMap grid;
#ifdef USE_BINARY_SENSOR
  binary_sensor::BinarySensor *presence{nullptr};
  binary_sensor::BinarySensor *motion{nullptr};
#endif
};

class FP2Component;

#ifdef USE_SWITCH
// Switch that enables or disables live target-location reporting on the radar.
class LocationReportSwitch : public switch_::Switch, public Parented<FP2Component> {
 protected:
  void write_state(bool state) override;
};
#endif

class FP2Component : public Component, public uart::UARTDevice {
#ifdef USE_BINARY_SENSOR
  SUB_BINARY_SENSOR(presence)
  SUB_BINARY_SENSOR(motion)
  SUB_BINARY_SENSOR(target)
#endif
#ifdef USE_SENSOR
  SUB_SENSOR_WITH_DEDUP(target_count, uint8_t)
  SUB_SENSOR_WITH_DEDUP(radar_temperature, int16_t)
#endif
#ifdef USE_TEXT_SENSOR
  SUB_TEXT_SENSOR(radar_version)
#endif
#ifdef USE_SWITCH
  SUB_SWITCH(location_report)
#endif

 public:
  void setup() override;
  void loop() override;
  void dump_config() override;

  float get_setup_priority() const override { return setup_priority::LATE; }

  void set_reset_pin(GPIOPin *pin) { this->reset_pin_ = pin; }
  void set_mounting_position(uint8_t position) { this->mounting_position_ = position; }
  void set_left_right_reverse(bool reverse) { this->left_right_reverse_ = reverse; }
  void set_presence_sensitivity(uint8_t sensitivity) { this->presence_sensitivity_ = sensitivity; }
  void set_accel(aqara_fp2_accel::AqaraFP2Accel *accel) { this->accel_ = accel; }

  void set_interference_grid(const GridMap &grid) {
    this->interference_grid_ = grid;
    this->has_interference_grid_ = true;
  }
  void set_exit_grid(const GridMap &grid) {
    this->exit_grid_ = grid;
    this->has_exit_grid_ = true;
  }
  void set_edge_grid(const GridMap &grid) {
    this->edge_grid_ = grid;
    this->has_edge_grid_ = true;
  }

  void init_zones(uint8_t count) { this->zones_.init(count); }
  void add_zone(uint8_t id, const GridMap &grid, uint8_t sensitivity, binary_sensor::BinarySensor *presence,
                binary_sensor::BinarySensor *motion);

#ifdef USE_SENSOR
  void set_target_x_sensor(uint8_t target, sensor::Sensor *s) { this->target_x_sensors_[target].set_sensor(s); }
  void set_target_y_sensor(uint8_t target, sensor::Sensor *s) { this->target_y_sensors_[target].set_sensor(s); }
  void set_target_z_sensor(uint8_t target, sensor::Sensor *s) { this->target_z_sensors_[target].set_sensor(s); }
  void set_target_speed_sensor(uint8_t target, sensor::Sensor *s) { this->target_speed_sensors_[target].set_sensor(s); }
#endif

  void set_location_reporting_enabled(bool enabled);

 protected:
  // Command queue (ring buffer).
  void enqueue_command_(OpCode type, AttrId attr_id, DataType data_type, const uint8_t *value, uint8_t value_len,
                        bool front = false);
  void enqueue_uint8_(AttrId attr_id, uint8_t value) {
    this->enqueue_command_(OpCode::WRITE, attr_id, DataType::UINT8, &value, 1);
  }
  void enqueue_uint16_(AttrId attr_id, uint16_t value) {
    uint8_t v[2] = {static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value & 0xFF)};
    this->enqueue_command_(OpCode::WRITE, attr_id, DataType::UINT16, v, 2);
  }
  void enqueue_bool_(AttrId attr_id, bool value) {
    uint8_t v = value ? 1 : 0;
    this->enqueue_command_(OpCode::WRITE, attr_id, DataType::BOOL, &v, 1);
  }
  void enqueue_blob_(AttrId attr_id, const uint8_t *content, uint8_t content_len);
  void process_command_queue_();
  void send_command_(const FP2Command &cmd);

  // Initialization.
  void perform_reset_();
  void run_initialization_();

  // Frame decoding.
  void handle_incoming_byte_(uint8_t byte);
  void handle_frame_(OpCode op, AttrId attr_id);
  void handle_report_(AttrId attr_id);
  void handle_location_report_();
  void handle_reverse_read_(AttrId attr_id);
  void send_ack_(AttrId attr_id);
  void send_reverse_response_(AttrId attr_id, uint8_t value);

  GPIOPin *reset_pin_{nullptr};
  aqara_fp2_accel::AqaraFP2Accel *accel_{nullptr};

  uint8_t mounting_position_{0x01};
  bool left_right_reverse_{false};
  uint8_t presence_sensitivity_{2};

  GridMap interference_grid_{};
  GridMap exit_grid_{};
  GridMap edge_grid_{};
  bool has_interference_grid_{false};
  bool has_exit_grid_{false};
  bool has_edge_grid_{false};

  FixedVector<FP2Zone> zones_;

  bool init_done_{false};
  bool location_reporting_active_{false};
  uint32_t last_heartbeat_millis_{0};

  // Outgoing command ring buffer plus the single command currently awaiting an ACK, held
  // separately so the queue head is always "next to send" and front-inserts stay safe.
  std::array<FP2Command, CMD_QUEUE_SIZE> queue_{};
  FP2Command inflight_{};
  uint8_t queue_head_{0};
  uint8_t queue_count_{0};
  AttrId waiting_for_ack_{AttrId::INVALID};
  uint32_t last_command_sent_millis_{0};
  uint8_t tx_seq_{0};

  // Incoming frame decoder.
  enum class RxState : uint8_t {
    SYNC,
    VER_H,
    VER_L,
    SEQ,
    OPCODE,
    LEN_H,
    LEN_L,
    HEADER_CHECK,
    PAYLOAD,
    CRC_L,
    CRC_H,
  } rx_state_{RxState::SYNC};
  uint8_t rx_seq_{0};
  uint8_t rx_opcode_{0};
  uint16_t rx_len_{0};
  uint16_t rx_pos_{0};
  uint16_t rx_crc_{0};
  uint16_t header_sum_{0};
  uint8_t rx_payload_[MAX_RX_PAYLOAD];

#ifdef USE_SENSOR
  std::array<SensorWithDedup<int16_t>, MAX_TARGETS> target_x_sensors_{};
  std::array<SensorWithDedup<int16_t>, MAX_TARGETS> target_y_sensors_{};
  std::array<SensorWithDedup<int16_t>, MAX_TARGETS> target_z_sensors_{};
  std::array<SensorWithDedup<int16_t>, MAX_TARGETS> target_speed_sensors_{};
#endif
};

}  // namespace esphome::aqara_fp2
