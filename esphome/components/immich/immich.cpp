#include "immich.h"

#include "esphome/components/json/json_util.h"
#include "esphome/core/alloc_helpers.h"
#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome::immich {

static const char *const TAG = "immich";

// One asset object from the search API is well under this size; the response
// is only read to extract the asset id, so anything beyond it is not needed.
static constexpr size_t MAX_RESPONSE_SIZE = 4096;

Immich::Immich(online_image::OnlineImage *image, const std::string &url, const std::string &api_key,
               const std::string &album_id)
    : image_(image), base_url_(url), api_key_(api_key), album_id_(album_id) {
  while (!this->base_url_.empty() && this->base_url_.back() == '/')
    this->base_url_.pop_back();
  // Ask for one random image asset from the album; videos are excluded.
  this->search_body_ = "{\"albumIds\":[\"" + this->album_id_ + "\"],\"size\":1,\"type\":\"IMAGE\"}";
}

void Immich::setup() {
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

void Immich::show_next_() {
  std::vector<http_request::Header> headers;
  headers.reserve(2);
  headers.push_back({"x-api-key", this->api_key_});
  headers.push_back({"Content-Type", "application/json"});

  auto container = this->parent_->post(this->base_url_ + "/api/search/random", this->search_body_, headers);
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
  RAMAllocator<uint8_t> allocator;
  uint8_t *buf = allocator.allocate(capacity);
  if (buf == nullptr) {
    ESP_LOGW(TAG, "Failed to allocate response buffer");
    container->end();
    this->status_set_warning();
    return;
  }

  size_t read_index = 0;
  uint32_t last_data_time = millis();
  const uint32_t timeout = this->parent_->get_timeout();
  while (read_index < capacity) {
    int read_or_error = container->read(buf + read_index, std::min<size_t>(capacity - read_index, 512));
    App.feed_wdt();
    yield();
    auto result =
        http_request::http_read_loop_result(read_or_error, last_data_time, timeout, container->is_read_complete());
    if (result == http_request::HttpReadLoopResult::RETRY)
      continue;
    if (result != http_request::HttpReadLoopResult::DATA)
      break;  // COMPLETE, ERROR, or TIMEOUT
    read_index += read_or_error;
  }
  container->end();

  JsonDocument doc = json::parse_json(buf, read_index);
  allocator.deallocate(buf, capacity);
  JsonArray assets = doc.as<JsonArray>();
  const char *asset_id = assets.isNull() || assets.size() == 0 ? nullptr : assets[0]["id"].as<const char *>();
  if (asset_id == nullptr) {
    ESP_LOGW(TAG, "No image asset found in album; check the album id and API key permissions");
    this->status_set_warning();
    return;
  }
  this->status_clear_warning();

  ESP_LOGD(TAG, "Showing asset %s", asset_id);
  std::string url = this->base_url_ + "/api/assets/" + asset_id + "/thumbnail?size=preview";
  this->image_->set_url(url);
  this->image_->update();
}

void Immich::dump_config() {
  ESP_LOGCONFIG(TAG,
                "Immich:\n"
                "  Server: %s\n"
                "  Album: %s\n"
                "  Slide interval: %" PRIu32 " ms",
                this->base_url_.c_str(), this->album_id_.c_str(), this->get_update_interval());
}

}  // namespace esphome::immich
