#include "ade7953_base.h"
#include "esphome/core/log.h"

#include <cinttypes>

namespace esphome::ade7953_base {

ESPHOME_LOG_TAG(TAG, "ade7953");

static constexpr uint32_t STARTUP_DELAY_MS = 100;
// PGA registers use bits [2:0], gain registers bits [23:0]
static constexpr uint8_t PGA_MASK = 0x07;
static constexpr uint32_t GAIN_MASK = 0x00FFFFFF;

static constexpr float ADE_POWER_FACTOR = 154.0f;
static constexpr float ADE_WATTSEC_POWER_FACTOR = ADE_POWER_FACTOR * ADE_POWER_FACTOR / 3600;

void ADE7953::setup() {
  if (this->irq_pin_ != nullptr) {
    this->irq_pin_->setup();
  }

  // The chip might take up to 100ms to initialise
  this->set_timeout(STARTUP_DELAY_MS, [this]() { this->finish_setup_(); });
}

void ADE7953::finish_setup_() {
  // DIAGNOSTIC, remove before the PR: the very first transfer on the bus is expected to be shifted by one bit
  this->ade_read_16(CONFIG_16, &this->diag_first_config_);
  this->ade_read_16(CONFIG_16, &this->diag_second_config_);
  ESP_LOGW(TAG, "DIAG first CONFIG read 0x%04X, second 0x%04X", this->diag_first_config_, this->diag_second_config_);
  if (!this->configure_()) {
    this->mark_failed(LOG_STR(ESP_LOG_MSG_COMM_FAIL));
    return;
  }
  this->is_setup_ = true;
}

bool ADE7953::read_back_mismatch_(uint16_t reg, uint32_t value) {
  this->mismatch_reg_ = reg;
  this->mismatch_value_ = value;
  ESP_LOGW(TAG, "Register 0x%03X read back 0x%08" PRIX32 ", not the value written", reg, value);
  return false;
}

bool ADE7953::configure_() {
  // Clearing COMM_LOCK locks the interface (SPI or I2C). Written as a fixed value rather than read, modified and
  // written back, so a garbled first transfer after boot cannot put stray bits such as the software reset in CONFIG.
  this->ade_write_16(CONFIG_16, CONFIG_DEFAULT & ~CONFIG_LOCK_BIT);
  // Configure optimum settings according to datasheet
  this->ade_write_8(UNLOCK_120_8, UNLOCK_120_KEY);
  this->ade_write_16(OPTIMUM_SETTING_16, OPTIMUM_SETTING_VALUE);
  // Set gains
  const struct {
    uint16_t reg;
    uint8_t value;
  } pgas[] = {{PGA_V_8, this->pga_v_}, {PGA_IA_8, this->pga_ia_}, {PGA_IB_8, this->pga_ib_}};
  const struct {
    uint16_t reg;
    uint32_t value;
  } gains[] = {{AVGAIN_32, this->avgain_}, {BVGAIN_32, this->bvgain_}, {AIGAIN_32, this->aigain_},
               {BIGAIN_32, this->bigain_}, {AWGAIN_32, this->awgain_}, {BWGAIN_32, this->bwgain_}};
  for (const auto &pga : pgas)
    this->ade_write_8(pga.reg, pga.value);
  for (const auto &gain : gains)
    this->ade_write_32(gain.reg, gain.value);

  // SPI cannot report a missing chip, so read the settings back. A missing chip reads all zeros or all
  // ones, which never matches: COMM_LOCK is cleared and every gain is at least 0x100000.
  uint16_t config_read = 0;
  if (this->ade_read_16(CONFIG_16, &config_read) || (config_read & CONFIG_LOCK_BIT) != 0)
    return this->read_back_mismatch_(CONFIG_16, config_read);
  for (const auto &pga : pgas) {
    uint8_t value = 0;
    if (this->ade_read_8(pga.reg, &value) || (value & PGA_MASK) != pga.value)
      return this->read_back_mismatch_(pga.reg, value);
  }
  for (const auto &gain : gains) {
    uint32_t value = 0;
    if (this->ade_read_32(gain.reg, &value) || (value & GAIN_MASK) != gain.value)
      return this->read_back_mismatch_(gain.reg, value);
  }

  if (this->use_acc_energy_regs_) {
    // The chip keeps accumulating while the ESP restarts; clear that so the first update covers one interval
    uint32_t discard;
    for (uint16_t reg : {AENERGYA_32, RENERGYA_32, APENERGYA_32}) {
      this->ade_read_32(reg, &discard);
      this->ade_read_32(reg + 1, &discard);
    }
  }
  this->last_update_ = millis();
  return true;
}

void ADE7953::dump_config() {
  LOG_PIN("  IRQ Pin: ", this->irq_pin_);
  if (this->is_failed()) {
    ESP_LOGE(TAG, ESP_LOG_MSG_COMM_FAIL "\n  Register 0x%03X read back 0x%08" PRIX32, this->mismatch_reg_,
             this->mismatch_value_);
  }
  ESP_LOGW(TAG, "  DIAG first CONFIG read 0x%04X, second 0x%04X", this->diag_first_config_, this->diag_second_config_);
  LOG_UPDATE_INTERVAL(this);
  LOG_SENSOR("  ", "Voltage Sensor", this->voltage_sensor_);
  LOG_SENSOR("  ", "Current A Sensor", this->current_a_sensor_);
  LOG_SENSOR("  ", "Current B Sensor", this->current_b_sensor_);
  LOG_SENSOR("  ", "Power Factor A Sensor", this->power_factor_a_sensor_);
  LOG_SENSOR("  ", "Power Factor B Sensor", this->power_factor_b_sensor_);
  LOG_SENSOR("  ", "Apparent Power A Sensor", this->apparent_power_a_sensor_);
  LOG_SENSOR("  ", "Apparent Power B Sensor", this->apparent_power_b_sensor_);
  LOG_SENSOR("  ", "Active Power A Sensor", this->active_power_a_sensor_);
  LOG_SENSOR("  ", "Active Power B Sensor", this->active_power_b_sensor_);
  LOG_SENSOR("  ", "Reactive Power A Sensor", this->reactive_power_a_sensor_);
  LOG_SENSOR("  ", "Reactive Power B Sensor", this->reactive_power_b_sensor_);
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

#define ADE_PUBLISH_(name, val, factor) \
  if (err) { \
    failed = true; \
  } else if (this->name##_sensor_) { \
    float value = (val) / (factor); \
    this->name##_sensor_->publish_state(value); \
  }
#define ADE_PUBLISH(name, val, factor) ADE_PUBLISH_(name, val, factor)

void ADE7953::update() {
  if (!this->is_setup_)
    return;

  bool err;
  bool failed = false;

  uint16_t config_v;
  if (this->ade_read_16(CONFIG_16, &config_v)) {
    this->status_set_warning(LOG_STR(ESP_LOG_MSG_COMM_FAIL));
    return;
  }
  if (config_v & CONFIG_LOCK_BIT) {
    // A reset restores the default gains; the next update publishes with the configuration applied again
    ESP_LOGW(TAG, "Chip reset detected, configuring again");
    if (!this->configure_()) {
      this->status_set_warning(LOG_STR(ESP_LOG_MSG_COMM_FAIL));
    }
    return;
  }

  uint32_t interrupts_a = 0;
  uint32_t interrupts_b = 0;
  if (this->irq_pin_ != nullptr) {
    // Read and reset interrupts
    this->ade_read_32(RSTIRQSTATA_32, &interrupts_a);
    this->ade_read_32(RSTIRQSTATB_32, &interrupts_b);
  }

  uint32_t val;
  uint16_t val_16;
  uint16_t reg;

  // Power factor
  err = this->ade_read_16(PFA_16, &val_16);
  ADE_PUBLISH(power_factor_a, (int16_t) val_16, (0x7FFF / 100.0f));
  err = this->ade_read_16(PFB_16, &val_16);
  ADE_PUBLISH(power_factor_b, (int16_t) val_16, (0x7FFF / 100.0f));

  float pf = ADE_POWER_FACTOR;
  if (this->use_acc_energy_regs_) {
    const uint32_t now = millis();
    const auto diff = now - this->last_update_;
    this->last_update_ = now;
    // prevent DIV/0
    pf = ADE_WATTSEC_POWER_FACTOR * (diff < 10 ? 10 : diff) / 1000;
    ESP_LOGVV(TAG, "ADE7953::update() diff=%" PRIu32 " pf=%f", diff, pf);
  }

  // Apparent power
  reg = this->use_acc_energy_regs_ ? APENERGYA_32 : AVA_32;
  err = this->ade_read_32(reg, &val);
  ADE_PUBLISH(apparent_power_a, (int32_t) val, pf);
  err = this->ade_read_32(reg + 1, &val);
  ADE_PUBLISH(apparent_power_b, (int32_t) val, pf);

  // Active power
  reg = this->use_acc_energy_regs_ ? AENERGYA_32 : AWATT_32;
  err = this->ade_read_32(reg, &val);
  ADE_PUBLISH(active_power_a, (int32_t) val, pf);
  err = this->ade_read_32(reg + 1, &val);
  ADE_PUBLISH(active_power_b, (int32_t) val, pf);

  // Reactive power
  reg = this->use_acc_energy_regs_ ? RENERGYA_32 : AVAR_32;
  err = this->ade_read_32(reg, &val);
  ADE_PUBLISH(reactive_power_a, (int32_t) val, pf);
  err = this->ade_read_32(reg + 1, &val);
  ADE_PUBLISH(reactive_power_b, (int32_t) val, pf);

  // Current
  err = this->ade_read_32(IRMSA_32, &val);
  ADE_PUBLISH(current_a, (uint32_t) val, 100000.0f);
  err = this->ade_read_32(IRMSB_32, &val);
  ADE_PUBLISH(current_b, (uint32_t) val, 100000.0f);

  // Voltage
  err = this->ade_read_32(VRMS_32, &val);
  ADE_PUBLISH(voltage, (uint32_t) val, 26000.0f);

  // Frequency
  err = this->ade_read_16(PERIOD_16, &val_16);
  ADE_PUBLISH(frequency, 223750.0f, 1 + val_16);

  if (failed) {
    this->status_set_warning(LOG_STR(ESP_LOG_MSG_COMM_FAIL));
  } else {
    this->status_clear_warning();
  }
}

}  // namespace esphome::ade7953_base
