#pragma once

#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/core/hal.h"
#include "esphome/core/preferences.h"
#include "esphome/components/sensor/sensor.h"
#ifdef USE_BINARY_SENSOR
#include "esphome/components/binary_sensor/binary_sensor.h"
#endif

#include <cstdint>

namespace esphome::ade7953_base {

// No-load detection disable: bit 0 active, bit 1 reactive, bit 2 apparent
static constexpr uint16_t DISNOLOAD_8 = 0x001;
static constexpr uint8_t DISNOLOAD_ALL = 0x07;

static constexpr uint8_t PGA_V_8 =
    0x007;  // PGA_V,  (R/W) Default: 0x00, Unsigned, Voltage channel gain configuration (Bits[2:0])
static constexpr uint8_t PGA_IA_8 =
    0x008;  // PGA_IA, (R/W) Default: 0x00, Unsigned, Current Channel A gain configuration (Bits[2:0])
static constexpr uint8_t PGA_IB_8 =
    0x009;  // PGA_IB, (R/W) Default: 0x00, Unsigned, Current Channel B gain configuration (Bits[2:0])

static constexpr uint16_t CONFIG_16 = 0x102;  // CONFIG, (R/W) Default: 0x8004, Unsigned, Configuration register

static constexpr uint16_t AIGAIN_32 =
    0x380;  // AIGAIN, (R/W)   Default: 0x400000, Unsigned,Current channel gain (Current Channel A)(32 bit)
static constexpr uint16_t AVGAIN_32 =
    0x381;  // AVGAIN, (R/W)   Default: 0x400000, Unsigned,Voltage channel gain(32 bit)
static constexpr uint16_t AWGAIN_32 =
    0x382;  // AWGAIN, (R/W)   Default: 0x400000, Unsigned,Active power gain (Current Channel A)(32 bit)
static constexpr uint16_t AVARGAIN_32 =
    0x383;  // AVARGAIN, (R/W) Default: 0x400000, Unsigned, Reactive power gain (Current Channel A)(32 bit)
static constexpr uint16_t AVAGAIN_32 =
    0x384;  // AVAGAIN, (R/W)  Default: 0x400000, Unsigned,Apparent power gain (Current Channel A)(32 bit)

static constexpr uint16_t BIGAIN_32 =
    0x38C;  // BIGAIN, (R/W)   Default: 0x400000, Unsigned,Current channel gain (Current Channel B)(32 bit)
static constexpr uint16_t BVGAIN_32 =
    0x38D;  // BVGAIN, (R/W)   Default: 0x400000, Unsigned,Voltage channel gain(32 bit)
static constexpr uint16_t BWGAIN_32 =
    0x38E;  // BWGAIN, (R/W)   Default: 0x400000, Unsigned,Active power gain (Current Channel B)(32 bit)
static constexpr uint16_t BVARGAIN_32 =
    0x38F;  // BVARGAIN, (R/W) Default: 0x400000, Unsigned, Reactive power gain (Current Channel B)(32 bit)
static constexpr uint16_t BVAGAIN_32 =
    0x390;  // BVAGAIN, (R/W)  Default: 0x400000, Unsigned,Apparent power gain (Current Channel B)(32 bit)

static constexpr uint16_t PFA_16 = 0x10A;     // Power factor (Current Channel A); channel B is at the next address
static constexpr uint16_t PERIOD_16 = 0x10E;  // Voltage channel period

// Accumulation mode. Per channel (B adds 1): APSIGN at bit 10, VARSIGN at bit 12.
// No-load flags per channel (B adds 3): ACTNLOAD at bit 16, VANLOAD at bit 17, VARNLOAD at bit 18.
static constexpr uint16_t ACCMODE_32 = 0x301;
static constexpr uint8_t ACCMODE_APSIGN_BIT = 10;
static constexpr uint8_t ACCMODE_NO_LOAD_BIT = 16;
// No-load thresholds for active, reactive and apparent power
static constexpr uint16_t AP_NOLOAD_32 = 0x303;
static constexpr uint16_t VAR_NOLOAD_32 = 0x304;
static constexpr uint16_t VA_NOLOAD_32 = 0x305;

// Instantaneous power (Current Channel A); channel B is at the next address
static constexpr uint16_t AVA_32 = 0x310;
static constexpr uint16_t AWATT_32 = 0x312;
static constexpr uint16_t AVAR_32 = 0x314;
static constexpr uint16_t IRMSA_32 = 0x31A;  // IRMS (Current Channel A); channel B is at the next address
static constexpr uint16_t VRMS_32 = 0x31C;

// Energy (Current Channel A), 24 bit signed; channel B is at the next address.
// They reset when read (LCYCMODE RSTREAD, on by default).
static constexpr uint16_t AENERGYA_32 = 0x31E;
static constexpr uint16_t RENERGYA_32 = 0x320;
static constexpr uint16_t APENERGYA_32 = 0x322;

static constexpr uint32_t NO_LOAD_THRESHOLD_UNSET = UINT32_MAX;

enum Channel : uint8_t { CHANNEL_A = 0, CHANNEL_B = 1, CHANNEL_COUNT = 2 };
enum PowerKind : uint8_t {
  POWER_KIND_ACTIVE = 0,
  POWER_KIND_REACTIVE = 1,
  POWER_KIND_APPARENT = 2,
  POWER_KIND_COUNT = 3
};

// Running energy total fed from the chip's energy registers, optionally kept across restarts
class ADE7953EnergySensor : public sensor::Sensor {
 public:
  explicit ADE7953EnergySensor(bool restore) : restore_(restore) {}
  // Loads the saved total when restore is on
  void load();
  // Adds energy in the sensor's unit and publishes the new total
  void add(double energy);

 protected:
  ESPPreferenceObject pref_;
  double total_{0.0};
  bool restore_;
};

class ADE7953 : public PollingComponent {
 public:
  void set_irq_pin(InternalGPIOPin *irq_pin) { this->irq_pin_ = irq_pin; }

  // Set PGA input gains: 0 1x, 1 2x, 0b10 4x
  void set_pga_v(uint8_t pga_v) { this->pga_v_ = pga_v; }
  void set_pga_ia(uint8_t pga_ia) { this->pga_ia_ = pga_ia; }
  void set_pga_ib(uint8_t pga_ib) { this->pga_ib_ = pga_ib; }

  // Set input gains
  void set_vgain(uint32_t vgain) {
    // Datasheet says: "to avoid discrepancies in other registers,
    // if AVGAIN is set then BVGAIN should be set to the same value."
    this->avgain_ = vgain;
    this->bvgain_ = vgain;
  }
  void set_aigain(uint32_t aigain) { this->aigain_ = aigain; }
  void set_bigain(uint32_t bigain) { this->bigain_ = bigain; }
  void set_awgain(uint32_t awgain) { this->awgain_ = awgain; }
  void set_bwgain(uint32_t bwgain) { this->bwgain_ = bwgain; }

  void set_use_acc_energy_regs(bool use_acc_energy_regs) { this->use_acc_energy_regs_ = use_acc_energy_regs; }
  // Sets the no-load threshold of active, reactive and apparent power; below it they read 0
  void set_no_load_threshold(uint32_t threshold) { this->no_load_threshold_ = threshold; }
  // For a current input wired in reverse: flips the sign of active power, reactive power and power factor
  void set_invert_current_a(bool invert) { this->invert_current_[CHANNEL_A] = invert; }
  void set_invert_current_b(bool invert) { this->invert_current_[CHANNEL_B] = invert; }

  void set_voltage_sensor(sensor::Sensor *voltage_sensor) { this->voltage_sensor_ = voltage_sensor; }
  void set_frequency_sensor(sensor::Sensor *frequency_sensor) { this->frequency_sensor_ = frequency_sensor; }

  void set_power_factor_a_sensor(sensor::Sensor *s) { this->power_factor_sensors_[CHANNEL_A] = s; }
  void set_power_factor_b_sensor(sensor::Sensor *s) { this->power_factor_sensors_[CHANNEL_B] = s; }
  void set_current_a_sensor(sensor::Sensor *s) { this->current_sensors_[CHANNEL_A] = s; }
  void set_current_b_sensor(sensor::Sensor *s) { this->current_sensors_[CHANNEL_B] = s; }

  void set_active_power_a_sensor(sensor::Sensor *s) { this->power_sensors_[CHANNEL_A][POWER_KIND_ACTIVE] = s; }
  void set_active_power_b_sensor(sensor::Sensor *s) { this->power_sensors_[CHANNEL_B][POWER_KIND_ACTIVE] = s; }
  void set_reactive_power_a_sensor(sensor::Sensor *s) { this->power_sensors_[CHANNEL_A][POWER_KIND_REACTIVE] = s; }
  void set_reactive_power_b_sensor(sensor::Sensor *s) { this->power_sensors_[CHANNEL_B][POWER_KIND_REACTIVE] = s; }
  void set_apparent_power_a_sensor(sensor::Sensor *s) { this->power_sensors_[CHANNEL_A][POWER_KIND_APPARENT] = s; }
  void set_apparent_power_b_sensor(sensor::Sensor *s) { this->power_sensors_[CHANNEL_B][POWER_KIND_APPARENT] = s; }

  void set_active_energy_a_sensor(ADE7953EnergySensor *s) { this->energy_sensors_[CHANNEL_A][POWER_KIND_ACTIVE] = s; }
  void set_active_energy_b_sensor(ADE7953EnergySensor *s) { this->energy_sensors_[CHANNEL_B][POWER_KIND_ACTIVE] = s; }
  void set_reactive_energy_a_sensor(ADE7953EnergySensor *s) {
    this->energy_sensors_[CHANNEL_A][POWER_KIND_REACTIVE] = s;
  }
  void set_reactive_energy_b_sensor(ADE7953EnergySensor *s) {
    this->energy_sensors_[CHANNEL_B][POWER_KIND_REACTIVE] = s;
  }
  void set_apparent_energy_a_sensor(ADE7953EnergySensor *s) {
    this->energy_sensors_[CHANNEL_A][POWER_KIND_APPARENT] = s;
  }
  void set_apparent_energy_b_sensor(ADE7953EnergySensor *s) {
    this->energy_sensors_[CHANNEL_B][POWER_KIND_APPARENT] = s;
  }

#ifdef USE_BINARY_SENSOR
  void set_no_load_a_binary_sensor(binary_sensor::BinarySensor *s) { this->no_load_binary_sensors_[CHANNEL_A] = s; }
  void set_no_load_b_binary_sensor(binary_sensor::BinarySensor *s) { this->no_load_binary_sensors_[CHANNEL_B] = s; }
  void set_reverse_power_a_binary_sensor(binary_sensor::BinarySensor *s) {
    this->reverse_power_binary_sensors_[CHANNEL_A] = s;
  }
  void set_reverse_power_b_binary_sensor(binary_sensor::BinarySensor *s) {
    this->reverse_power_binary_sensors_[CHANNEL_B] = s;
  }
#endif

  void setup() override;

  void dump_config() override;

  void update() override;

 protected:
  InternalGPIOPin *irq_pin_{nullptr};
  bool is_setup_{false};
  sensor::Sensor *voltage_sensor_{nullptr};
  sensor::Sensor *frequency_sensor_{nullptr};
  sensor::Sensor *current_sensors_[CHANNEL_COUNT]{};
  sensor::Sensor *power_factor_sensors_[CHANNEL_COUNT]{};
  sensor::Sensor *power_sensors_[CHANNEL_COUNT][POWER_KIND_COUNT]{};
  ADE7953EnergySensor *energy_sensors_[CHANNEL_COUNT][POWER_KIND_COUNT]{};
#ifdef USE_BINARY_SENSOR
  binary_sensor::BinarySensor *no_load_binary_sensors_[CHANNEL_COUNT]{};
  binary_sensor::BinarySensor *reverse_power_binary_sensors_[CHANNEL_COUNT]{};
#endif
  uint8_t pga_v_;
  uint8_t pga_ia_;
  uint8_t pga_ib_;
  uint32_t avgain_;
  uint32_t bvgain_;
  uint32_t aigain_;
  uint32_t bigain_;
  uint32_t awgain_;
  uint32_t bwgain_;
  uint32_t no_load_threshold_{NO_LOAD_THRESHOLD_UNSET};
  bool use_acc_energy_regs_{false};
  bool invert_current_[CHANNEL_COUNT]{};
  uint32_t last_update_;

  // Whether update() needs the ACCMODE register
  bool needs_accmode_() const;
  // Reads one energy register into counts (signed per the current direction) and adds it to its energy sensor
  bool read_energy_(uint8_t channel, uint8_t kind, float *counts);
  // Publishes power, energy, power factor and current of one channel
  void update_channel_(uint8_t channel, uint32_t accmode, bool accmode_valid, float power_divider);

  virtual bool ade_write_8(uint16_t reg, uint8_t value) = 0;

  virtual bool ade_write_16(uint16_t reg, uint16_t value) = 0;

  virtual bool ade_write_32(uint16_t reg, uint32_t value) = 0;

  virtual bool ade_read_8(uint16_t reg, uint8_t *value) = 0;

  virtual bool ade_read_16(uint16_t reg, uint16_t *value) = 0;

  virtual bool ade_read_32(uint16_t reg, uint32_t *value) = 0;
};

}  // namespace esphome::ade7953_base
