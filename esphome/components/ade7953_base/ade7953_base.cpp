#include "ade7953_base.h"
#include "esphome/core/log.h"

#include <algorithm>
#include <cinttypes>
#include <cmath>

namespace esphome::ade7953_base {

ESPHOME_LOG_TAG(TAG, "ade7953");

constexpr uint16_t CONFIG_DEFAULT = 0x8004u;
constexpr uint16_t CONFIG_LOCK_BIT = 0x8000u;

static constexpr float ADE_POWER_FACTOR = 154.0f;
static constexpr float ADE_WATTSEC_POWER_FACTOR = ADE_POWER_FACTOR * ADE_POWER_FACTOR / 3600;
// Energy register counts per watt hour (var hour, VA hour)
static constexpr float ADE_COUNTS_PER_WATT_HOUR = ADE_WATTSEC_POWER_FACTOR * 3600;

static uint16_t power_reg(uint8_t kind) { return kind == POWER_KIND_APPARENT ? AVA_32 : AWATT_32 + 2 * kind; }
static uint16_t energy_reg(uint8_t kind) { return AENERGYA_32 + 2 * kind; }
// The chip orders the no-load flags active, apparent, reactive
static uint32_t no_load_mask(uint8_t channel, uint8_t kind) {
  const uint8_t offset = kind == POWER_KIND_ACTIVE ? 0 : (kind == POWER_KIND_APPARENT ? 1 : 2);
  return 1UL << (ACCMODE_NO_LOAD_BIT + 3 * channel + offset);
}

void ADE7953EnergySensor::load() {
  if (!this->restore_)
    return;
  this->pref_ = this->make_entity_preference<double>();
  double total;
  if (this->pref_.load(&total)) {
    this->total_ = total;
    this->publish_state(total);
  }
}

void ADE7953EnergySensor::add(double energy) {
  if (energy > 0) {
    this->total_ += energy;
    if (this->restore_)
      this->pref_.save(&this->total_);
  }
  this->publish_state(this->total_);
}

void ADE7953::setup() {
  if (this->irq_pin_ != nullptr) {
    this->irq_pin_->setup();
  }
  for (auto &channel : this->energy_sensors_) {
    for (auto *energy : channel) {
      if (energy != nullptr)
        energy->load();
    }
  }

  // The chip might take up to 100ms to initialise
  this->set_timeout(100, [this]() {
    // Lock communication interface (SPI or I2C)
    uint16_t config_v = CONFIG_DEFAULT;
    this->ade_read_16(CONFIG_16, &config_v);
    config_v &= static_cast<uint16_t>(~CONFIG_LOCK_BIT);  // Clear the lock bit
    this->ade_write_16(CONFIG_16, config_v);
    // Configure optimum settings according to datasheet
    this->ade_write_8(0x00FE, 0xAD);
    this->ade_write_16(0x0120, 0x0030);
    // Set gains
    this->ade_write_8(PGA_V_8, this->pga_v_);
    this->ade_write_8(PGA_IA_8, this->pga_ia_);
    this->ade_write_8(PGA_IB_8, this->pga_ib_);
    this->ade_write_32(AVGAIN_32, this->avgain_);
    this->ade_write_32(BVGAIN_32, this->bvgain_);
    this->ade_write_32(AIGAIN_32, this->aigain_);
    this->ade_write_32(BIGAIN_32, this->bigain_);
    this->ade_write_32(AWGAIN_32, this->awgain_);
    this->ade_write_32(BWGAIN_32, this->bwgain_);
    if (this->no_load_threshold_ != NO_LOAD_THRESHOLD_UNSET) {
      // The thresholds can only be changed while no-load detection is disabled
      this->ade_write_8(DISNOLOAD_8, DISNOLOAD_ALL);
      for (uint16_t reg : {AP_NOLOAD_32, VAR_NOLOAD_32, VA_NOLOAD_32})
        this->ade_write_32(reg, this->no_load_threshold_);
      this->ade_write_8(DISNOLOAD_8, 0);
    }
    // Read back gains for debugging
    this->ade_read_8(PGA_V_8, &this->pga_v_);
    this->ade_read_8(PGA_IA_8, &this->pga_ia_);
    this->ade_read_8(PGA_IB_8, &this->pga_ib_);
    this->ade_read_32(AVGAIN_32, &this->avgain_);
    this->ade_read_32(BVGAIN_32, &this->bvgain_);
    this->ade_read_32(AIGAIN_32, &this->aigain_);
    this->ade_read_32(BIGAIN_32, &this->bigain_);
    this->ade_read_32(AWGAIN_32, &this->awgain_);
    this->ade_read_32(BWGAIN_32, &this->bwgain_);
    // The chip keeps counting while the ESP restarts (OTA). That is real energy, but the first accumulated
    // power reading would show it as one large spike, so start the power interval from empty registers.
    float discard;
    for (uint8_t channel = 0; channel < CHANNEL_COUNT; channel++) {
      for (uint8_t kind = 0; kind < POWER_KIND_COUNT; kind++) {
        if (this->use_acc_energy_regs_ || this->energy_sensors_[channel][kind] != nullptr)
          this->read_energy_(channel, kind, &discard);
      }
    }
    this->last_update_ = millis();
    this->is_setup_ = true;
  });
}

void ADE7953::dump_config() {
  LOG_PIN("  IRQ Pin: ", this->irq_pin_);
  LOG_UPDATE_INTERVAL(this);
  LOG_SENSOR("  ", "Voltage Sensor", this->voltage_sensor_);
  LOG_SENSOR("  ", "Frequency Sensor", this->frequency_sensor_);
  LOG_SENSOR("  ", "Current A Sensor", this->current_sensors_[CHANNEL_A]);
  LOG_SENSOR("  ", "Current B Sensor", this->current_sensors_[CHANNEL_B]);
  LOG_SENSOR("  ", "Power Factor A Sensor", this->power_factor_sensors_[CHANNEL_A]);
  LOG_SENSOR("  ", "Power Factor B Sensor", this->power_factor_sensors_[CHANNEL_B]);
  LOG_SENSOR("  ", "Apparent Power A Sensor", this->power_sensors_[CHANNEL_A][POWER_KIND_APPARENT]);
  LOG_SENSOR("  ", "Apparent Power B Sensor", this->power_sensors_[CHANNEL_B][POWER_KIND_APPARENT]);
  LOG_SENSOR("  ", "Active Power A Sensor", this->power_sensors_[CHANNEL_A][POWER_KIND_ACTIVE]);
  LOG_SENSOR("  ", "Active Power B Sensor", this->power_sensors_[CHANNEL_B][POWER_KIND_ACTIVE]);
  LOG_SENSOR("  ", "Reactive Power A Sensor", this->power_sensors_[CHANNEL_A][POWER_KIND_REACTIVE]);
  LOG_SENSOR("  ", "Reactive Power B Sensor", this->power_sensors_[CHANNEL_B][POWER_KIND_REACTIVE]);
  LOG_SENSOR("  ", "Apparent Energy A Sensor", this->energy_sensors_[CHANNEL_A][POWER_KIND_APPARENT]);
  LOG_SENSOR("  ", "Apparent Energy B Sensor", this->energy_sensors_[CHANNEL_B][POWER_KIND_APPARENT]);
  LOG_SENSOR("  ", "Active Energy A Sensor", this->energy_sensors_[CHANNEL_A][POWER_KIND_ACTIVE]);
  LOG_SENSOR("  ", "Active Energy B Sensor", this->energy_sensors_[CHANNEL_B][POWER_KIND_ACTIVE]);
  LOG_SENSOR("  ", "Reactive Energy A Sensor", this->energy_sensors_[CHANNEL_A][POWER_KIND_REACTIVE]);
  LOG_SENSOR("  ", "Reactive Energy B Sensor", this->energy_sensors_[CHANNEL_B][POWER_KIND_REACTIVE]);
#ifdef USE_BINARY_SENSOR
  LOG_BINARY_SENSOR("  ", "No Load A", this->no_load_binary_sensors_[CHANNEL_A]);
  LOG_BINARY_SENSOR("  ", "No Load B", this->no_load_binary_sensors_[CHANNEL_B]);
  LOG_BINARY_SENSOR("  ", "Reverse Power A", this->reverse_power_binary_sensors_[CHANNEL_A]);
  LOG_BINARY_SENSOR("  ", "Reverse Power B", this->reverse_power_binary_sensors_[CHANNEL_B]);
#endif
  if (this->no_load_threshold_ != NO_LOAD_THRESHOLD_UNSET) {
    ESP_LOGCONFIG(TAG, "  No-load threshold: 0x%06" PRIX32, this->no_load_threshold_);
  }
  ESP_LOGCONFIG(TAG,
                "  Invert current A: %s\n"
                "  Invert current B: %s",
                YESNO(this->invert_current_[CHANNEL_A]), YESNO(this->invert_current_[CHANNEL_B]));
  ESP_LOGCONFIG(TAG,
                "  USE_ACC_ENERGY_REGS: %d\n"
                "  PGA_V_8: 0x%X\n"
                "  PGA_IA_8: 0x%X\n"
                "  PGA_IB_8: 0x%X\n"
                "  AVGAIN_32: 0x%08jX\n"
                "  BVGAIN_32: 0x%08jX\n"
                "  AIGAIN_32: 0x%08jX\n"
                "  BIGAIN_32: 0x%08jX\n"
                "  AWGAIN_32: 0x%08jX\n"
                "  BWGAIN_32: 0x%08jX",
                this->use_acc_energy_regs_, this->pga_v_, this->pga_ia_, this->pga_ib_, (uintmax_t) this->avgain_,
                (uintmax_t) this->bvgain_, (uintmax_t) this->aigain_, (uintmax_t) this->bigain_,
                (uintmax_t) this->awgain_, (uintmax_t) this->bwgain_);
}

bool ADE7953::needs_accmode_() const {
  if (this->no_load_threshold_ != NO_LOAD_THRESHOLD_UNSET)
    return true;
#ifdef USE_BINARY_SENSOR
  for (uint8_t channel = 0; channel < CHANNEL_COUNT; channel++) {
    if (this->no_load_binary_sensors_[channel] != nullptr || this->reverse_power_binary_sensors_[channel] != nullptr)
      return true;
  }
#endif
  return false;
}

void ADE7953::update() {
  if (!this->is_setup_)
    return;

  uint32_t interrupts_a = 0;
  uint32_t interrupts_b = 0;
  if (this->irq_pin_ != nullptr) {
    // Read and reset interrupts
    this->ade_read_32(0x032E, &interrupts_a);
    this->ade_read_32(0x0331, &interrupts_b);
  }

  uint32_t accmode = 0;
  const bool accmode_valid = this->needs_accmode_() && !this->ade_read_32(ACCMODE_32, &accmode);

  float power_divider = ADE_POWER_FACTOR;
  if (this->use_acc_energy_regs_) {
    const uint32_t now = millis();
    const auto diff = now - this->last_update_;
    this->last_update_ = now;
    // prevent DIV/0
    power_divider = ADE_WATTSEC_POWER_FACTOR * (diff < 10 ? 10 : diff) / 1000;
    ESP_LOGVV(TAG, "ADE7953::update() diff=%" PRIu32 " power_divider=%f", diff, power_divider);
  }

  for (uint8_t channel = 0; channel < CHANNEL_COUNT; channel++)
    this->update_channel_(channel, accmode, accmode_valid, power_divider);

  uint32_t val;
  uint16_t val_16;
  if (this->voltage_sensor_ != nullptr && !this->ade_read_32(VRMS_32, &val))
    this->voltage_sensor_->publish_state(val / 26000.0f);
  if (this->frequency_sensor_ != nullptr && !this->ade_read_16(PERIOD_16, &val_16))
    this->frequency_sensor_->publish_state(223750.0f / (1 + val_16));
}

bool ADE7953::read_energy_(uint8_t channel, uint8_t kind, float *counts) {
  uint32_t val;
  if (this->ade_read_32(energy_reg(kind) + channel, &val))
    return false;
  const bool flip = kind != POWER_KIND_APPARENT && this->invert_current_[channel];
  *counts = (flip ? -1.0f : 1.0f) * static_cast<int32_t>(val);
  if (ADE7953EnergySensor *energy = this->energy_sensors_[channel][kind]) {
    // Active energy counts consumption only; reactive and apparent energy count both directions
    const float delta = *counts / ADE_COUNTS_PER_WATT_HOUR;
    energy->add(kind == POWER_KIND_ACTIVE ? std::max(delta, 0.0f) : std::fabs(delta));
  }
  return true;
}

void ADE7953::update_channel_(uint8_t channel, uint32_t accmode, bool accmode_valid, float power_divider) {
  const float sign = this->invert_current_[channel] ? -1.0f : 1.0f;
  const bool zero_at_no_load = accmode_valid && this->no_load_threshold_ != NO_LOAD_THRESHOLD_UNSET;
  uint32_t val;
  uint16_t val_16;

  for (uint8_t kind = 0; kind < POWER_KIND_COUNT; kind++) {
    sensor::Sensor *power = this->power_sensors_[channel][kind];
    ADE7953EnergySensor *energy = this->energy_sensors_[channel][kind];
    // Apparent power has no direction
    const float kind_sign = kind == POWER_KIND_APPARENT ? 1.0f : sign;
    // The energy register resets when read, so one read serves both the energy and the accumulated power
    float energy_counts = 0.0f;
    const bool energy_valid = (energy != nullptr || (power != nullptr && this->use_acc_energy_regs_)) &&
                              this->read_energy_(channel, kind, &energy_counts);

    if (power == nullptr)
      continue;
    float value;
    if (this->use_acc_energy_regs_) {
      if (!energy_valid)
        continue;
      value = energy_counts / power_divider;
    } else {
      if (this->ade_read_32(power_reg(kind) + channel, &val))
        continue;
      value = kind_sign * static_cast<int32_t>(val) / power_divider;
    }
    if (zero_at_no_load && (accmode & no_load_mask(channel, kind)) != 0)
      value = 0.0f;
    power->publish_state(value);
  }

  sensor::Sensor *power_factor = this->power_factor_sensors_[channel];
  if (power_factor != nullptr && !this->ade_read_16(PFA_16 + channel, &val_16))
    power_factor->publish_state(sign * static_cast<int16_t>(val_16) / (0x7FFF / 100.0f));

  sensor::Sensor *current = this->current_sensors_[channel];
  if (current != nullptr && !this->ade_read_32(IRMSA_32 + channel, &val)) {
    const bool no_load = zero_at_no_load && (accmode & no_load_mask(channel, POWER_KIND_APPARENT)) != 0;
    current->publish_state(no_load ? 0.0f : val / 100000.0f);
  }

#ifdef USE_BINARY_SENSOR
  if (!accmode_valid)
    return;
  if (auto *no_load = this->no_load_binary_sensors_[channel])
    no_load->publish_state((accmode & no_load_mask(channel, POWER_KIND_ACTIVE)) != 0);
  if (auto *reverse_power = this->reverse_power_binary_sensors_[channel]) {
    const bool negative = (accmode & (1UL << (ACCMODE_APSIGN_BIT + channel))) != 0;
    reverse_power->publish_state(negative != this->invert_current_[channel]);
  }
#endif
}

}  // namespace esphome::ade7953_base
