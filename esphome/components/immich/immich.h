#pragma once

#include "esphome/components/http_request/http_request.h"
#include "esphome/components/json/json_util.h"
#include "esphome/components/online_image/online_image.h"
#include "esphome/core/automation.h"
#include "esphome/core/component.h"

namespace esphome::immich {

/**
 * @brief Slideshow of photos from an Immich album.
 *
 * Every update interval, one random image asset is picked from the configured
 * album via the Immich search API, and the linked online_image is pointed at
 * that asset's preview and updated. The slideshow is idle until started with
 * the immich.start action, so it can be used as a screensaver.
 */
class Immich : public PollingComponent, public Parented<http_request::HttpRequestComponent> {
 public:
  Immich(online_image::OnlineImage *image, const char *base_url, const char *search_url, const char *api_key,
         const char *album_id, const char *search_body)
      : image_(image),
        base_url_(base_url),
        search_url_(search_url),
        api_key_(api_key),
        album_id_(album_id),
        search_body_(search_body) {}

  void setup() override;
  void update() override;
  void dump_config() override;

  /// Start the slideshow: show an image now and keep rotating every update interval.
  void start();
  /// Stop the slideshow. The last image stays on the linked online_image.
  void stop();
  /// Show the next image once, without changing the running state.
  void next_image() { this->show_next_(); }

  bool is_running() const { return this->running_; }

 protected:
  void show_next_();

  online_image::OnlineImage *image_;
  const char *base_url_;
  const char *search_url_;
  const char *api_key_;
  const char *album_id_;
  // post() takes the body as std::string, so keep one copy instead of building it per request
  std::string search_body_;
  std::vector<http_request::Header> headers_;
  // Reserved in setup() for the full thumbnail URL, so building it does not reallocate
  std::string asset_url_;
  size_t asset_url_prefix_len_{0};
  uint8_t *response_buf_{nullptr};
  JsonDocument filter_;
  bool running_{false};
};

template<typename... Ts> class StartAction final : public Action<Ts...> {
 public:
  explicit StartAction(Immich *parent) : parent_(parent) {}
  void play(const Ts &...x) override { this->parent_->start(); }

 protected:
  Immich *parent_;
};

template<typename... Ts> class StopAction final : public Action<Ts...> {
 public:
  explicit StopAction(Immich *parent) : parent_(parent) {}
  void play(const Ts &...x) override { this->parent_->stop(); }

 protected:
  Immich *parent_;
};

template<typename... Ts> class NextImageAction final : public Action<Ts...> {
 public:
  explicit NextImageAction(Immich *parent) : parent_(parent) {}
  void play(const Ts &...x) override { this->parent_->next_image(); }

 protected:
  Immich *parent_;
};

}  // namespace esphome::immich
