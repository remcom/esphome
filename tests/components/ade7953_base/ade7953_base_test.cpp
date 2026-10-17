#ifdef USE_HOST

#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <utility>
#include <vector>
#include "esphome/components/ade7953_base/ade7953_base.h"
#include "esphome/components/host/preferences.h"
#include "esphome/core/application.h"
#include "esphome/core/hal.h"

namespace esphome::ade7953_base::testing {

// Energy register counts for 1 Wh with the fixed Shelly factor (154 counts per watt, squared)
static constexpr int32_t COUNTS_PER_WH = 154 * 154;

class EntityIdentity : public EntityBase {
 public:
  static void configure(EntityBase &entity, const char *name, uint32_t hash) {
    auto configure = &EntityIdentity::configure_entity_;
    (entity.*configure)(name, hash, 0);
  }
};

// A register map that behaves like the chip: writes are stored, energy registers clear on read
class FakeADE7953 : public ADE7953 {
 public:
  void set(uint16_t reg, int32_t value) { this->regs[reg] = static_cast<uint32_t>(value); }

  std::map<uint16_t, uint32_t> regs;
  std::map<uint16_t, unsigned> reads;
  std::vector<std::pair<uint16_t, uint32_t>> writes;

 protected:
  bool ade_write_8(uint16_t reg, uint8_t value) override { return this->write_(reg, value); }
  bool ade_write_16(uint16_t reg, uint16_t value) override { return this->write_(reg, value); }
  bool ade_write_32(uint16_t reg, uint32_t value) override { return this->write_(reg, value); }
  bool ade_read_8(uint16_t reg, uint8_t *value) override { return this->read_(reg, value); }
  bool ade_read_16(uint16_t reg, uint16_t *value) override { return this->read_(reg, value); }
  bool ade_read_32(uint16_t reg, uint32_t *value) override { return this->read_(reg, value); }

  bool write_(uint16_t reg, uint32_t value) {
    this->regs[reg] = value;
    this->writes.emplace_back(reg, value);
    return false;
  }

  template<typename T> bool read_(uint16_t reg, T *value) {
    this->reads[reg]++;
    *value = static_cast<T>(this->regs[reg]);
    if (reg >= AENERGYA_32 && reg <= APENERGYA_32 + 1)
      this->regs[reg] = 0;
    return false;
  }
};

class ADE7953Test : public ::testing::Test {
 protected:
  void SetUp() override {
    new (&App) Application;
    App.pre_setup("ade7953-tests", 13, "ADE7953 tests", 13);
    host::setup_preferences();
    // The first access reads any saved file; clear what it loaded
    uint8_t unused;
    host::host_preferences->load(0, &unused, sizeof(unused));
    host::host_preferences->reset();
    this->chip_.set_pga_v(0);
    this->chip_.set_pga_ia(0);
    this->chip_.set_pga_ib(0);
    this->chip_.set_vgain(0x400000);
    this->chip_.set_aigain(0x400000);
    this->chip_.set_bigain(0x400000);
    this->chip_.set_awgain(0x400000);
    this->chip_.set_bwgain(0x400000);
    EntityIdentity::configure(this->active_energy_a_, "Active Energy A", 0x7953A1);
    EntityIdentity::configure(this->reactive_energy_a_, "Reactive Energy A", 0x7953A2);
  }

  void TearDown() override {
    App.scheduler.call(millis());
    host::host_preferences->reset();
    App.~Application();
  }

  // Runs setup and the configuration that follows once the chip has started
  void start() {
    this->chip_.setup();
    App.scheduler.call(millis());
    delay(110);  // NOLINT
    App.scheduler.call(millis());
  }

  FakeADE7953 chip_;
  sensor::Sensor active_power_a_;
  sensor::Sensor active_power_b_;
  sensor::Sensor reactive_power_a_;
  sensor::Sensor current_a_;
  sensor::Sensor power_factor_b_;
  ADE7953EnergySensor active_energy_a_{false};
  ADE7953EnergySensor reactive_energy_a_{false};
  binary_sensor::BinarySensor no_load_a_;
  binary_sensor::BinarySensor reverse_power_a_;
  binary_sensor::BinarySensor reverse_power_b_;
};

TEST_F(ADE7953Test, EnergyKeepsARunningTotal) {
  this->chip_.set_active_energy_a_sensor(&this->active_energy_a_);
  this->start();

  this->chip_.set(AENERGYA_32, COUNTS_PER_WH);
  this->chip_.update();
  EXPECT_FLOAT_EQ(this->active_energy_a_.state, 1.0f);
  this->chip_.set(AENERGYA_32, 2 * COUNTS_PER_WH);
  this->chip_.update();
  EXPECT_FLOAT_EQ(this->active_energy_a_.state, 3.0f);
  // Nothing accumulated since the last read
  this->chip_.update();
  EXPECT_FLOAT_EQ(this->active_energy_a_.state, 3.0f);
}

TEST_F(ADE7953Test, ActiveEnergyCountsConsumptionOnly) {
  this->chip_.set_active_energy_a_sensor(&this->active_energy_a_);
  this->chip_.set_reactive_energy_a_sensor(&this->reactive_energy_a_);
  this->start();

  this->chip_.set(AENERGYA_32, -COUNTS_PER_WH);
  this->chip_.set(RENERGYA_32, -COUNTS_PER_WH);
  this->chip_.update();
  EXPECT_FLOAT_EQ(this->active_energy_a_.state, 0.0f);
  EXPECT_FLOAT_EQ(this->reactive_energy_a_.state, 1.0f);
}

TEST_F(ADE7953Test, InvertedCurrentFlipsSigns) {
  this->chip_.set_invert_current_b(true);
  this->chip_.set_active_power_a_sensor(&this->active_power_a_);
  this->chip_.set_active_power_b_sensor(&this->active_power_b_);
  this->chip_.set_power_factor_b_sensor(&this->power_factor_b_);
  this->chip_.set_reverse_power_a_binary_sensor(&this->reverse_power_a_);
  this->chip_.set_reverse_power_b_binary_sensor(&this->reverse_power_b_);
  this->start();

  this->chip_.set(AWATT_32, 154);
  this->chip_.set(AWATT_32 + 1, -154);
  this->chip_.set(PFA_16 + 1, static_cast<int16_t>(-0x7FFF));
  // APSIGN set for channel B only: negative before the inversion
  this->chip_.set(ACCMODE_32, 1 << (ACCMODE_APSIGN_BIT + 1));
  this->chip_.update();
  EXPECT_FLOAT_EQ(this->active_power_a_.state, 1.0f);
  EXPECT_FLOAT_EQ(this->active_power_b_.state, 1.0f);
  EXPECT_FLOAT_EQ(this->power_factor_b_.state, 100.0f);
  EXPECT_FALSE(this->reverse_power_a_.state);
  EXPECT_FALSE(this->reverse_power_b_.state);

  this->chip_.set(ACCMODE_32, 1 << ACCMODE_APSIGN_BIT);
  this->chip_.update();
  EXPECT_TRUE(this->reverse_power_a_.state);
  EXPECT_TRUE(this->reverse_power_b_.state);
}

TEST_F(ADE7953Test, AccumulatedPowerAndEnergyShareOneRead) {
  this->chip_.set_use_acc_energy_regs(true);
  this->chip_.set_active_power_a_sensor(&this->active_power_a_);
  this->chip_.set_active_energy_a_sensor(&this->active_energy_a_);
  this->start();

  this->chip_.reads.clear();
  this->chip_.set(AENERGYA_32, COUNTS_PER_WH);
  this->chip_.update();
  EXPECT_EQ(this->chip_.reads[AENERGYA_32], 1u);
  EXPECT_FLOAT_EQ(this->active_energy_a_.state, 1.0f);
  // 1 Wh in at most a few seconds since setup is well over a kilowatt
  EXPECT_GT(this->active_power_a_.state, 1000.0f);
}

TEST_F(ADE7953Test, EnergyCountedDuringRestartIsNoPowerSpike) {
  this->chip_.set_use_acc_energy_regs(true);
  this->chip_.set_active_power_a_sensor(&this->active_power_a_);
  this->chip_.set_active_energy_a_sensor(&this->active_energy_a_);
  // Counted by the chip while the ESP was restarting
  this->chip_.set(AENERGYA_32, 100 * COUNTS_PER_WH);
  this->start();
  EXPECT_FLOAT_EQ(this->active_energy_a_.state, 100.0f);

  this->chip_.update();
  EXPECT_FLOAT_EQ(this->active_power_a_.state, 0.0f);
  EXPECT_FLOAT_EQ(this->active_energy_a_.state, 100.0f);
}

TEST_F(ADE7953Test, NoLoadThresholdIsWrittenWhileDetectionIsDisabled) {
  this->chip_.set_no_load_threshold(0x720C);
  this->start();

  auto &writes = this->chip_.writes;
  auto disable = std::find(writes.begin(), writes.end(), std::make_pair(DISNOLOAD_8, uint32_t{DISNOLOAD_ALL}));
  ASSERT_NE(disable, writes.end());
  auto enable = std::find(disable, writes.end(), std::make_pair(DISNOLOAD_8, uint32_t{0}));
  ASSERT_NE(enable, writes.end());
  for (uint16_t reg : {AP_NOLOAD_32, VAR_NOLOAD_32, VA_NOLOAD_32}) {
    auto threshold = std::find(disable, enable, std::make_pair(reg, uint32_t{0x720C}));
    EXPECT_NE(threshold, enable) << "register 0x" << std::hex << reg;
  }
}

TEST_F(ADE7953Test, NoLoadPublishesZero) {
  this->chip_.set_no_load_threshold(0x720C);
  this->chip_.set_active_power_a_sensor(&this->active_power_a_);
  this->chip_.set_reactive_power_a_sensor(&this->reactive_power_a_);
  this->chip_.set_current_a_sensor(&this->current_a_);
  this->chip_.set_no_load_a_binary_sensor(&this->no_load_a_);
  this->start();

  this->chip_.set(AWATT_32, 300);
  this->chip_.set(AVAR_32, 300);
  this->chip_.set(IRMSA_32, 5000);
  // ACTNLOAD_A and VANLOAD_A set, VARNLOAD_A clear
  this->chip_.set(ACCMODE_32, 0b011 << ACCMODE_NO_LOAD_BIT);
  this->chip_.update();
  EXPECT_FLOAT_EQ(this->active_power_a_.state, 0.0f);
  EXPECT_FLOAT_EQ(this->reactive_power_a_.state, 300 / 154.0f);
  EXPECT_FLOAT_EQ(this->current_a_.state, 0.0f);
  EXPECT_TRUE(this->no_load_a_.state);

  this->chip_.set(ACCMODE_32, 0);
  this->chip_.update();
  EXPECT_FLOAT_EQ(this->active_power_a_.state, 300 / 154.0f);
  EXPECT_FLOAT_EQ(this->current_a_.state, 0.05f);
  EXPECT_FALSE(this->no_load_a_.state);
}

TEST_F(ADE7953Test, NoLoadWithoutThresholdKeepsReadings) {
  this->chip_.set_active_power_a_sensor(&this->active_power_a_);
  this->chip_.set_no_load_a_binary_sensor(&this->no_load_a_);
  this->start();

  this->chip_.set(AWATT_32, 300);
  this->chip_.set(ACCMODE_32, 1 << ACCMODE_NO_LOAD_BIT);
  this->chip_.update();
  EXPECT_FLOAT_EQ(this->active_power_a_.state, 300 / 154.0f);
  EXPECT_TRUE(this->no_load_a_.state);
}

TEST_F(ADE7953Test, EnergyIsRestored) {
  ADE7953EnergySensor before{true};
  EntityIdentity::configure(before, "Restored Energy", 0x7953B1);
  this->chip_.set_active_energy_a_sensor(&before);
  this->start();
  this->chip_.set(AENERGYA_32, 5 * COUNTS_PER_WH);
  this->chip_.update();

  ADE7953EnergySensor after{true};
  EntityIdentity::configure(after, "Restored Energy", 0x7953B1);
  after.load();
  EXPECT_FLOAT_EQ(after.state, 5.0f);
}

}  // namespace esphome::ade7953_base::testing

#endif  // USE_HOST
