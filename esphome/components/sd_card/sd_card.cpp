#include "sd_card.h"

#ifdef USE_ESP32

#include "esphome/core/log.h"

#include <algorithm>

#include <driver/sdmmc_host.h>
#include <esp_vfs_fat.h>

namespace esphome::sd_card {

ESPHOME_LOG_TAG(TAG, "sd_card");

static constexpr uint32_t CARD_POLL_ID = 0;

static constexpr uint32_t CARD_DETECT_INTERVAL_MS = 500;
// Without a card detect pin every retry is a full card init that blocks until it times out
static constexpr uint32_t MOUNT_RETRY_INTERVAL_MS = 10000;
// A status request to a mounted card is a single short command
static constexpr uint32_t CARD_STATUS_INTERVAL_MS = 1000;

SDCard::SDCard(const char *mount_point, uint8_t clk_pin, uint8_t cmd_pin, std::initializer_list<uint8_t> data_pins)
    : Storage(mount_point),
      clk_pin_(clk_pin),
      cmd_pin_(cmd_pin),
      bus_width_(static_cast<uint8_t>(std::min(data_pins.size(), this->data_pins_.size()))) {
  std::copy_n(data_pins.begin(), this->bus_width_, this->data_pins_.begin());
}

void SDCard::setup() {
  if (this->card_detect_pin_ != nullptr) {
    this->card_detect_pin_->setup();
    if (this->card_detect_pin_->digital_read()) {
      this->card_detect_state_ = CardDetectState::CARD_DETECT_STATE_HANDLED;
      this->mount_();
    }
    this->set_interval(CARD_DETECT_INTERVAL_MS, [this]() { this->check_card_detect_(); });
    return;
  }
  if (this->mount_()) {
    this->watch_card_();
  } else {
    this->retry_mount_();
  }
}

void SDCard::retry_mount_() {
  this->set_interval(CARD_POLL_ID, MOUNT_RETRY_INTERVAL_MS, [this]() {
    if (this->mount_())
      this->watch_card_();
  });
}

void SDCard::watch_card_() {
  // Without a card detect pin, a card that stops answering is the only sign that it was removed
  this->set_interval(CARD_POLL_ID, CARD_STATUS_INTERVAL_MS, [this]() {
    if (sdmmc_get_status(this->card_) == ESP_OK)
      return;
    ESP_LOGW(TAG, "Card stopped responding");
    this->unmount_();
    this->retry_mount_();
  });
}

void SDCard::check_card_detect_() {
  if (!this->card_detect_pin_->digital_read()) {
    if (this->is_mounted()) {
      ESP_LOGI(TAG, "Card removed");
      this->unmount_();
    }
    this->status_clear_warning();
    this->card_detect_state_ = CardDetectState::CARD_DETECT_STATE_EMPTY;
    return;
  }
  // Wait one more interval after insertion so the contacts have settled, then try to mount once per insertion
  switch (this->card_detect_state_) {
    case CardDetectState::CARD_DETECT_STATE_EMPTY:
      this->card_detect_state_ = CardDetectState::CARD_DETECT_STATE_SETTLING;
      break;
    case CardDetectState::CARD_DETECT_STATE_SETTLING:
      this->card_detect_state_ = CardDetectState::CARD_DETECT_STATE_HANDLED;
      this->mount_();
      break;
    case CardDetectState::CARD_DETECT_STATE_HANDLED:
      break;
  }
}

bool SDCard::mount_() {
  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.max_freq_khz = static_cast<int>(this->frequency_khz_);

  sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
  slot.width = this->bus_width_;
  slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
#ifdef SOC_SDMMC_USE_GPIO_MATRIX
  slot.clk = static_cast<gpio_num_t>(this->clk_pin_);
  slot.cmd = static_cast<gpio_num_t>(this->cmd_pin_);
  slot.d0 = static_cast<gpio_num_t>(this->data_pins_[0]);
  if (this->bus_width_ == 4) {
    slot.d1 = static_cast<gpio_num_t>(this->data_pins_[1]);
    slot.d2 = static_cast<gpio_num_t>(this->data_pins_[2]);
    slot.d3 = static_cast<gpio_num_t>(this->data_pins_[3]);
  }
#endif

  esp_vfs_fat_mount_config_t mount_config = VFS_FAT_MOUNT_DEFAULT_CONFIG();
  mount_config.format_if_mount_failed = this->format_if_mount_failed_;
  mount_config.max_files = this->max_open_files_;

  esp_err_t err = esp_vfs_fat_sdmmc_mount(this->mount_point_, &host, &slot, &mount_config, &this->card_);
  if (err != ESP_OK) {
    this->card_ = nullptr;
    if (!this->status_has_warning()) {
      ESP_LOGW(TAG, "Mounting failed: %s", esp_err_to_name(err));
    }
    this->status_set_warning(LOG_STR("No usable card"));
    return false;
  }
  this->status_clear_warning();
  ESP_LOGI(TAG, "Mounted %s at %s", this->card_->cid.name, this->mount_point_);
  this->set_mounted_(true);
  return true;
}

void SDCard::unmount_() {
  // Tell the users first, so they can close their files while the filesystem still exists
  this->set_mounted_(false);
  esp_vfs_fat_sdcard_unmount(this->mount_point_, this->card_);
  this->card_ = nullptr;
}

bool SDCard::get_space(uint64_t &total_bytes, uint64_t &free_bytes) {
  return this->is_mounted() && esp_vfs_fat_info(this->mount_point_, &total_bytes, &free_bytes) == ESP_OK;
}

void SDCard::dump_config() {
  ESP_LOGCONFIG(TAG,
                "SD card:\n"
                "  Mount point: %s\n"
                "  Bus width: %u bit\n"
                "  Max frequency: %" PRIu32 " kHz\n"
                "  CLK pin: GPIO%u\n"
                "  CMD pin: GPIO%u",
                this->mount_point_, this->bus_width_, this->frequency_khz_, this->clk_pin_, this->cmd_pin_);
  for (uint8_t i = 0; i < this->bus_width_; i++) {
    ESP_LOGCONFIG(TAG, "  D%u pin: GPIO%u", i, this->data_pins_[i]);
  }
  LOG_PIN("  Card detect pin: ", this->card_detect_pin_);
  if (this->card_ != nullptr) {
    uint64_t capacity = static_cast<uint64_t>(this->card_->csd.capacity) * this->card_->csd.sector_size;
    ESP_LOGCONFIG(TAG,
                  "  Card: %s\n"
                  "  Capacity: %" PRIu32 " MB\n"
                  "  Frequency: %d kHz",
                  this->card_->cid.name, static_cast<uint32_t>(capacity / (1024 * 1024)), this->card_->real_freq_khz);
  }
}

}  // namespace esphome::sd_card

#endif  // USE_ESP32
