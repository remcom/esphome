#include <gtest/gtest.h>

#include <map>
#include <set>
#include "esphome/components/ade7953_base/ade7953_base.h"

namespace esphome::ade7953_base::testing {

static constexpr uint32_t GAIN_DEFAULT = 0x400000;

// A register map that behaves like the chip: writes are stored, energy registers clear on read.
// With `present` false every read returns `absent_value` without an error, like SPI with nothing on the bus.
class FakeADE7953 : public ADE7953 {
 public:
  FakeADE7953() { this->power_on_reset(); }

  void power_on_reset() {
    this->regs.clear();
    this->regs[CONFIG_16] = CONFIG_DEFAULT;
    for (uint16_t reg : {AVGAIN_32, BVGAIN_32, AIGAIN_32, BIGAIN_32, AWGAIN_32, BWGAIN_32})
      this->regs[reg] = GAIN_DEFAULT;
  }

  void finish_setup() { this->try_setup_(); }
  // Runs every setup attempt the scheduler would retry, stopping once one succeeds
  void finish_setup_all_attempts() {
    for (int i = 0; i < 3 && !this->is_failed(); i++)
      this->try_setup_();
  }
  uint16_t mismatch_reg() const { return this->mismatch_reg_; }

  std::map<uint16_t, uint32_t> regs;
  std::set<uint16_t> failing_reads;
  bool present{true};
  uint32_t absent_value{0};

 protected:
  bool ade_write_8(uint16_t reg, uint8_t value) override { return this->write_(reg, value); }
  bool ade_write_16(uint16_t reg, uint16_t value) override { return this->write_(reg, value); }
  bool ade_write_32(uint16_t reg, uint32_t value) override { return this->write_(reg, value); }
  bool ade_read_8(uint16_t reg, uint8_t *value) override { return this->read_(reg, value); }
  bool ade_read_16(uint16_t reg, uint16_t *value) override { return this->read_(reg, value); }
  bool ade_read_32(uint16_t reg, uint32_t *value) override { return this->read_(reg, value); }

  bool write_(uint16_t reg, uint32_t value) {
    if (this->present)
      this->regs[reg] = value;
    return false;
  }

  template<typename T> bool read_(uint16_t reg, T *value) {
    if (this->failing_reads.count(reg) != 0)
      return true;
    *value = static_cast<T>(this->present ? this->regs[reg] : this->absent_value);
    if (reg >= AENERGYA_32 && reg <= APENERGYA_32 + 1)
      this->regs[reg] = 0;
    return false;
  }
};

class ADE7953Test : public ::testing::Test {
 protected:
  void SetUp() override {
    this->chip_.set_pga_v(0);
    this->chip_.set_pga_ia(1);
    this->chip_.set_pga_ib(2);
    this->chip_.set_vgain(GAIN_DEFAULT);
    this->chip_.set_aigain(0x500000);
    this->chip_.set_bigain(0x300000);
    this->chip_.set_awgain(GAIN_DEFAULT);
    this->chip_.set_bwgain(GAIN_DEFAULT);
    this->chip_.set_voltage_sensor(&this->voltage_);
    this->chip_.set_active_power_a_sensor(&this->power_a_);
  }

  FakeADE7953 chip_;
  sensor::Sensor voltage_;
  sensor::Sensor power_a_;
};

TEST_F(ADE7953Test, SetupWritesAndVerifiesConfiguration) {
  this->chip_.finish_setup();

  EXPECT_FALSE(this->chip_.is_failed());
  EXPECT_EQ(this->chip_.regs[CONFIG_16], CONFIG_DEFAULT & ~CONFIG_LOCK_BIT);
  EXPECT_EQ(this->chip_.regs[UNLOCK_120_8], UNLOCK_120_KEY);
  EXPECT_EQ(this->chip_.regs[OPTIMUM_SETTING_16], OPTIMUM_SETTING_VALUE);
  EXPECT_EQ(this->chip_.regs[PGA_IA_8], 1u);
  EXPECT_EQ(this->chip_.regs[PGA_IB_8], 2u);
  EXPECT_EQ(this->chip_.regs[AIGAIN_32], 0x500000u);
  EXPECT_EQ(this->chip_.regs[BIGAIN_32], 0x300000u);
}

TEST_F(ADE7953Test, MissingChipFailsSetupAfterRetries) {
  this->chip_.present = false;
  this->chip_.finish_setup();
  this->chip_.finish_setup();
  EXPECT_FALSE(this->chip_.is_failed());

  this->chip_.finish_setup();
  EXPECT_TRUE(this->chip_.is_failed());
  EXPECT_EQ(this->chip_.mismatch_reg(), PGA_IA_8);
}

TEST_F(ADE7953Test, FailedFirstAttemptIsRetried) {
  // The chip misses the first configuration, for example a garbled first transfer after boot
  this->chip_.present = false;
  this->chip_.finish_setup();
  this->chip_.present = true;
  this->chip_.finish_setup();

  EXPECT_FALSE(this->chip_.is_failed());
  this->chip_.regs[VRMS_32] = 230 * 26000;
  this->chip_.update();
  EXPECT_FLOAT_EQ(this->voltage_.get_state(), 230.0f);
}

TEST_F(ADE7953Test, ConfigIsWrittenWithoutReadingIt) {
  // Stray bits read from CONFIG must not be written back
  this->chip_.regs[CONFIG_16] = 0xFFFF;
  this->chip_.finish_setup();

  EXPECT_EQ(this->chip_.regs[CONFIG_16], CONFIG_DEFAULT & ~CONFIG_LOCK_BIT);
}

TEST_F(ADE7953Test, PublishesReadings) {
  this->chip_.finish_setup();
  this->chip_.regs[VRMS_32] = 230 * 26000;
  this->chip_.regs[AWATT_32] = 100 * 154;

  this->chip_.update();

  EXPECT_FLOAT_EQ(this->voltage_.get_state(), 230.0f);
  EXPECT_FLOAT_EQ(this->power_a_.get_state(), 100.0f);
  EXPECT_FALSE(this->chip_.status_has_warning());
}

TEST_F(ADE7953Test, ChipResetIsConfiguredAgain) {
  this->chip_.finish_setup();
  this->chip_.power_on_reset();
  this->chip_.regs[VRMS_32] = 230 * 26000;

  this->chip_.update();

  EXPECT_EQ(this->chip_.regs[CONFIG_16], CONFIG_DEFAULT & ~CONFIG_LOCK_BIT);
  EXPECT_EQ(this->chip_.regs[AIGAIN_32], 0x500000u);
  EXPECT_EQ(this->chip_.regs[OPTIMUM_SETTING_16], OPTIMUM_SETTING_VALUE);
  // Nothing is published until the next update reads with the configuration applied
  EXPECT_FALSE(this->voltage_.has_state());
  EXPECT_FALSE(this->chip_.status_has_warning());

  this->chip_.update();
  EXPECT_FLOAT_EQ(this->voltage_.get_state(), 230.0f);
}

TEST_F(ADE7953Test, MissingChipReadingAllOnesFailsSetup) {
  this->chip_.present = false;
  this->chip_.absent_value = 0xFFFFFFFF;
  this->chip_.finish_setup_all_attempts();

  EXPECT_TRUE(this->chip_.is_failed());
  EXPECT_EQ(this->chip_.mismatch_reg(), CONFIG_16);
}

TEST_F(ADE7953Test, ChipLostAfterSetupSetsWarning) {
  this->chip_.finish_setup();
  // SPI reports no error; the bus reads all ones, which looks like a reset that cannot be configured
  this->chip_.present = false;
  this->chip_.absent_value = 0xFFFFFFFF;

  this->chip_.update();
  EXPECT_TRUE(this->chip_.status_has_warning());
  EXPECT_FALSE(this->chip_.is_failed());
  EXPECT_FALSE(this->voltage_.has_state());
}

TEST_F(ADE7953Test, ConfigReadErrorSetsWarning) {
  this->chip_.finish_setup();
  this->chip_.failing_reads.insert(CONFIG_16);

  this->chip_.update();

  EXPECT_TRUE(this->chip_.status_has_warning());
  EXPECT_FALSE(this->voltage_.has_state());
}

TEST_F(ADE7953Test, ReadErrorSetsWarningUntilNextGoodUpdate) {
  this->chip_.finish_setup();
  this->chip_.failing_reads.insert(VRMS_32);

  this->chip_.update();
  EXPECT_TRUE(this->chip_.status_has_warning());
  EXPECT_FALSE(this->voltage_.has_state());

  this->chip_.failing_reads.clear();
  this->chip_.update();
  EXPECT_FALSE(this->chip_.status_has_warning());
}

TEST_F(ADE7953Test, EnergyRegistersAreClearedDuringSetup) {
  this->chip_.set_use_acc_energy_regs(true);
  // Energy the chip counted while the ESP was restarting
  this->chip_.regs[AENERGYA_32] = 5000000;
  this->chip_.regs[AENERGYA_32 + 1] = 5000000;

  this->chip_.finish_setup();

  EXPECT_EQ(this->chip_.regs[AENERGYA_32], 0u);
  EXPECT_EQ(this->chip_.regs[AENERGYA_32 + 1], 0u);
}

}  // namespace esphome::ade7953_base::testing
