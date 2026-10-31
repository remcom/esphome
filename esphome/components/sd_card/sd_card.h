#pragma once

#include "esphome/core/defines.h"

#ifdef USE_ESP32

#include "esphome/components/storage/storage.h"
#include "esphome/core/component.h"
#include "esphome/core/hal.h"

#include <array>
#include <initializer_list>

#include <sdmmc_cmd.h>
#ifdef USE_SD_CARD_POWER_LDO
#include <sd_pwr_ctrl.h>
#endif

namespace esphome::sd_card {

/// An SD card on the SD/MMC host, with a FAT filesystem mounted into the VFS.
class SDCard : public Component, public storage::Storage {
 public:
  /// data_pins holds D0 only for a 1-bit bus, or D0 to D3 for a 4-bit bus.
  SDCard(const char *mount_point, uint8_t slot, uint8_t clk_pin, uint8_t cmd_pin,
         std::initializer_list<uint8_t> data_pins);

  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }

  /// Scans the FAT the first time it is called after mounting, which can take seconds on a large card.
  bool get_space(uint64_t &total_bytes, uint64_t &free_bytes) override;

  /// Reads true while a card is in the socket.
  void set_card_detect_pin(GPIOPin *pin) { this->card_detect_pin_ = pin; }
  void set_frequency_khz(uint32_t frequency_khz) { this->frequency_khz_ = frequency_khz; }
  void set_max_open_files(uint8_t max_open_files) { this->max_open_files_ = max_open_files; }
  void set_format_if_mount_failed(bool format) { this->format_if_mount_failed_ = format; }
#ifdef USE_SD_CARD_POWER_LDO
  void set_power_ldo_channel(uint8_t channel) { this->power_ldo_channel_ = channel; }
#endif

 protected:
  enum class CardDetectState : uint8_t {
    CARD_DETECT_STATE_EMPTY,
    CARD_DETECT_STATE_SETTLING,
    CARD_DETECT_STATE_HANDLED,
  };

  bool mount_();
  void unmount_();
  void check_card_detect_();
  void retry_mount_();
  void watch_card_();

  sdmmc_card_t *card_{nullptr};
  GPIOPin *card_detect_pin_{nullptr};
  uint32_t frequency_khz_{SDMMC_FREQ_DEFAULT};
  std::array<uint8_t, 4> data_pins_{};
  uint8_t slot_;
  uint8_t clk_pin_;
  uint8_t cmd_pin_;
  uint8_t bus_width_;
  uint8_t max_open_files_{5};
  bool format_if_mount_failed_{false};
  CardDetectState card_detect_state_{CardDetectState::CARD_DETECT_STATE_EMPTY};
#ifdef USE_SD_CARD_POWER_LDO
  sd_pwr_ctrl_handle_t pwr_ctrl_{nullptr};
  uint8_t power_ldo_channel_{0};
#endif
};

}  // namespace esphome::sd_card

#endif  // USE_ESP32
