#include "sd_card.h"

#ifdef USE_ESP32

#include "esphome/core/log.h"

#include <algorithm>

#include <driver/sdmmc_host.h>
#include <esp_vfs_fat.h>
#ifdef USE_SD_CARD_POWER_LDO
#include <sd_pwr_ctrl_by_on_chip_ldo.h>
#endif

namespace esphome::sd_card {

ESPHOME_LOG_TAG(TAG, "sd_card");

static constexpr uint32_t CARD_POLL_ID = 0;

static constexpr uint32_t CARD_DETECT_INTERVAL_MS = 500;
// Without a card detect pin every retry is a full card init, which takes seconds when the card does not answer
static constexpr uint32_t MOUNT_RETRY_INTERVAL_MS = 10000;
// A status request to a mounted card is a single short command
static constexpr uint32_t CARD_STATUS_INTERVAL_MS = 1000;
// Cluster size for a card formatted by the component, the usual size for FAT32 cards of 32 GB and up
static constexpr size_t FORMAT_ALLOCATION_UNIT_SIZE = 32 * 1024;
// The mount task mostly waits on the card; the buffers for formatting come from the heap
static constexpr uint32_t MOUNT_TASK_STACK_SIZE = 4096;
static constexpr UBaseType_t MOUNT_TASK_PRIORITY = 1;

SDCard::SDCard(const char *mount_point, uint8_t slot, uint8_t clk_pin, uint8_t cmd_pin,
               std::initializer_list<uint8_t> data_pins)
    : Storage(mount_point),
      slot_(slot),
      clk_pin_(clk_pin),
      cmd_pin_(cmd_pin),
      bus_width_(static_cast<uint8_t>(std::min(data_pins.size(), this->data_pins_.size()))) {
  std::copy_n(data_pins.begin(), this->bus_width_, this->data_pins_.begin());
}

void SDCard::setup() {
#ifdef USE_SD_CARD_POWER_LDO
  if (this->power_ldo_channel_ != 0) {
    sd_pwr_ctrl_ldo_config_t ldo_config{};
    ldo_config.ldo_chan_id = this->power_ldo_channel_;
    esp_err_t err = sd_pwr_ctrl_new_on_chip_ldo(&ldo_config, &this->pwr_ctrl_);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "LDO channel %u: %s", this->power_ldo_channel_, esp_err_to_name(err));
      this->mark_failed(LOG_STR("Could not power the card"));
      return;
    }
  }
#endif
  // Card init and formatting block for seconds, so they run on a task of their own instead of the main loop
  if (xTaskCreate(SDCard::mount_task_, "sd_card", MOUNT_TASK_STACK_SIZE, this, MOUNT_TASK_PRIORITY,
                  &this->mount_task_handle_) != pdPASS) {
    this->mark_failed(LOG_STR("Could not start the mount task"));
    return;
  }
  if (this->card_detect_pin_ != nullptr) {
    this->card_detect_pin_->setup();
    if (this->card_detect_pin_->digital_read()) {
      this->card_detect_state_ = CardDetectState::CARD_DETECT_STATE_HANDLED;
      this->request_mount_();
    }
    this->set_interval(CARD_DETECT_INTERVAL_MS, [this]() { this->check_card_detect_(); });
    return;
  }
  this->request_mount_();
  this->retry_mount_();
}

void SDCard::loop() {
  // Only the mount task enables the loop, to hand over a finished attempt
  this->disable_loop();
  if (!this->mount_done_.exchange(false, std::memory_order_acquire))
    return;
  ESP_LOGV(TAG, "Mount task stack left: %u bytes",
           static_cast<unsigned>(uxTaskGetStackHighWaterMark(this->mount_task_handle_)));
  this->mount_busy_ = false;
  // A card swapped during the attempt makes its result stale
  const bool stale = this->mount_again_;
  this->mount_again_ = false;
  this->finish_mount_(stale);
  if (stale)
    this->request_mount_();
}

void SDCard::mount_task_(void *arg) {
  auto *sd_card = static_cast<SDCard *>(arg);
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    sd_card->mount_result_ = sd_card->try_mount_();
    sd_card->mount_done_.store(true, std::memory_order_release);
    sd_card->enable_loop_soon_any_context();
  }
}

void SDCard::request_mount_() {
  if (this->is_mounted())
    return;
  if (this->mount_busy_) {
    // With a card detect pin, a new request during an attempt means the card was taken out and put back
    if (this->card_detect_pin_ != nullptr)
      this->mount_again_ = true;
    return;
  }
  this->mount_busy_ = true;
  xTaskNotifyGive(this->mount_task_handle_);
}

void SDCard::finish_mount_(bool stale) {
  // With a card detect pin, the card may have been taken out while it was being mounted
  const bool removed =
      this->card_detect_pin_ != nullptr && this->card_detect_state_ != CardDetectState::CARD_DETECT_STATE_HANDLED;
  if (this->mount_result_ != ESP_OK) {
    if (stale || removed)
      return;
    if (!this->status_has_warning()) {
      ESP_LOGW(TAG, "Mounting failed: %s", esp_err_to_name(this->mount_result_));
    }
    this->status_set_warning(LOG_STR("No usable card"));
    return;
  }
  sdmmc_card_t *card = this->pending_card_;
  this->pending_card_ = nullptr;
  if (stale || removed) {
    esp_vfs_fat_sdcard_unmount(this->mount_point_, card);
    return;
  }
  this->card_ = card;
  this->status_clear_warning();
  ESP_LOGI(TAG, "Mounted %s at %s", this->card_->cid.name, this->mount_point_);
  this->set_mounted_(true);
  if (this->card_detect_pin_ == nullptr)
    this->watch_card_();
}

void SDCard::retry_mount_() {
  this->set_interval(CARD_POLL_ID, MOUNT_RETRY_INTERVAL_MS, [this]() { this->request_mount_(); });
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
      this->request_mount_();
      break;
    case CardDetectState::CARD_DETECT_STATE_HANDLED:
      break;
  }
}

esp_err_t SDCard::try_mount_() {
  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.slot = this->slot_;
  host.max_freq_khz = static_cast<int>(this->frequency_khz_);
#ifdef USE_SD_CARD_POWER_LDO
  host.pwr_ctrl_handle = this->pwr_ctrl_;
#endif

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
  // Only used when formatting. The default of one sector makes the FAT so large that formatting takes minutes.
  mount_config.allocation_unit_size = FORMAT_ALLOCATION_UNIT_SIZE;

  esp_err_t err = esp_vfs_fat_sdmmc_mount(this->mount_point_, &host, &slot, &mount_config, &this->pending_card_);
  if (err != ESP_OK)
    this->pending_card_ = nullptr;
  return err;
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
                "  Slot: %u\n"
                "  Bus width: %u bit\n"
                "  Max frequency: %" PRIu32 " kHz\n"
                "  CLK pin: GPIO%u\n"
                "  CMD pin: GPIO%u",
                this->mount_point_, this->slot_, this->bus_width_, this->frequency_khz_, this->clk_pin_,
                this->cmd_pin_);
  for (uint8_t i = 0; i < this->bus_width_; i++) {
    ESP_LOGCONFIG(TAG, "  D%u pin: GPIO%u", i, this->data_pins_[i]);
  }
  LOG_PIN("  Card detect pin: ", this->card_detect_pin_);
#ifdef USE_SD_CARD_POWER_LDO
  if (this->power_ldo_channel_ != 0) {
    ESP_LOGCONFIG(TAG, "  Power LDO channel: %u", this->power_ldo_channel_);
  }
#endif
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
