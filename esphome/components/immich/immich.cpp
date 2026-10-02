#include "immich.h"

#include <cstring>

#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome::immich {

static const char *const TAG = "immich";

// One asset object from the search API, without people and EXIF data, is well
// under this size; the response is only read to extract the asset id.
static constexpr size_t MAX_RESPONSE_SIZE = 4096;
static constexpr size_t ASSET_ID_LENGTH = 36;  // UUID
static constexpr const char *ASSET_PATH = "/api/assets/";
static constexpr const char *THUMBNAIL_QUERY = "/thumbnail?size=preview";

void Immich::setup() {
  RAMAllocator<uint8_t> allocator;
  this->response_buf_ = allocator.allocate(MAX_RESPONSE_SIZE);
  if (this->response_buf_ == nullptr) {
    ESP_LOGE(TAG, "Failed to allocate response buffer");
    this->mark_failed();
    return;
  }

  this->headers_.reserve(2);
  this->headers_.push_back({"x-api-key", this->api_key_});
  this->headers_.push_back({"Content-Type", "application/json"});

  this->asset_url_.reserve(strlen(this->base_url_) + strlen(ASSET_PATH) + ASSET_ID_LENGTH + strlen(THUMBNAIL_QUERY));
  this->asset_url_.append(this->base_url_).append(ASSET_PATH);
  this->asset_url_prefix_len_ = this->asset_url_.size();

  // Keep only the id of each asset in the parsed document
  this->filter_[0]["id"] = true;

  this->image_->add_request_header("x-api-key", this->api_key_);
  // Idle until the immich.start action is called.
  this->stop_poller();
}

void Immich::update() { this->show_next_(); }

void Immich::start() {
  if (this->running_)
    return;
  this->running_ = true;
  this->start_poller();
  this->show_next_();
}

void Immich::stop() {
  if (!this->running_)
    return;
  this->running_ = false;
  this->stop_poller();
}

// The search request blocks the main loop until it completes, for at most the
// http_request timeout. The image itself is downloaded by online_image in its loop().
void Immich::show_next_() {
  if (this->is_failed())
    return;
  if (this->image_->is_decoding()) {
    ESP_LOGD(TAG, "Previous image still loading, skipping");
    return;
  }

  ESP_LOGD(TAG, "POST %s body %s", this->search_url_, this->search_body_.c_str());
  auto container = this->parent_->post(this->search_url_, this->search_body_, this->headers_);
  if (container == nullptr) {
    ESP_LOGW(TAG, "Request to Immich server failed");
    this->status_set_warning();
    return;
  }
  if (!http_request::is_success(container->status_code)) {
    ESP_LOGW(TAG, "Immich server returned HTTP status %d", container->status_code);
    container->end();
    this->status_set_warning();
    return;
  }

  size_t capacity = container->content_length;
  if (capacity == 0 || capacity > MAX_RESPONSE_SIZE)
    capacity = MAX_RESPONSE_SIZE;

  size_t read_index = 0;
  bool complete = false;
  uint32_t last_data_time = millis();
  const uint32_t timeout = this->parent_->get_timeout();
  while (read_index < capacity) {
    int read_or_error =
        container->read(this->response_buf_ + read_index, std::min<size_t>(capacity - read_index, 512));
    App.feed_wdt();
    yield();
    auto result =
        http_request::http_read_loop_result(read_or_error, last_data_time, timeout, container->is_read_complete());
    if (result == http_request::HttpReadLoopResult::RETRY)
      continue;
    if (result == http_request::HttpReadLoopResult::COMPLETE)
      complete = true;
    if (result != http_request::HttpReadLoopResult::DATA)
      break;  // COMPLETE, ERROR, or TIMEOUT
    read_index += read_or_error;
  }
  complete = complete || container->is_read_complete();
  container->end();

  if (!complete) {
    if (read_index >= MAX_RESPONSE_SIZE) {
      ESP_LOGW(TAG, "Search response is larger than %zu bytes", MAX_RESPONSE_SIZE);
    } else {
      ESP_LOGW(TAG, "Search response incomplete (%zu bytes read)", read_index);
    }
    this->status_set_warning();
    return;
  }

  JsonDocument doc(json::heap_json_allocator());
  DeserializationError err = deserializeJson(doc, this->response_buf_, read_index,
                                             DeserializationOption::Filter(this->filter_));
  if (err) {
    ESP_LOGW(TAG, "Failed to parse search response: %s", err.c_str());
    this->status_set_warning();
    return;
  }
  JsonArray assets = doc.as<JsonArray>();
  const char *asset_id = assets.isNull() || assets.size() == 0 ? nullptr : assets[0]["id"].as<const char *>();
  if (asset_id == nullptr) {
    ESP_LOGW(TAG, "No image asset found in album; check the album id and API key permissions");
    this->status_set_warning();
    return;
  }
  if (strlen(asset_id) != ASSET_ID_LENGTH) {
    ESP_LOGW(TAG, "Unexpected asset id '%s'", asset_id);
    this->status_set_warning();
    return;
  }
  this->status_clear_warning();

  this->asset_url_.resize(this->asset_url_prefix_len_);
  this->asset_url_.append(asset_id).append(THUMBNAIL_QUERY);
  ESP_LOGD(TAG, "Showing asset %s from %s", asset_id, this->asset_url_.c_str());
  this->image_->set_url(this->asset_url_);
  this->image_->update();
}

void Immich::dump_config() {
  ESP_LOGCONFIG(TAG,
                "Immich:\n"
                "  Server: %s\n"
                "  Album: %s\n"
                "  Slide interval: %" PRIu32 " ms",
                this->base_url_, this->album_id_, this->get_update_interval());
}

}  // namespace esphome::immich
