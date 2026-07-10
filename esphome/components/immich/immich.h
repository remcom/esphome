#pragma once

#include "esphome/components/http_request/http_request.h"
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
  Immich(online_image::OnlineImage *image, const std::string &url, const std::string &api_key,
         const std::string &album_id);

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
  std::string base_url_;
  std::string api_key_;
  std::string album_id_;
  std::string search_body_;
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

template<typename... Ts> class IsRunningCondition final : public Condition<Ts...> {
 public:
  explicit IsRunningCondition(Immich *parent) : parent_(parent) {}
  bool check(const Ts &...x) override { return this->parent_->is_running(); }

 protected:
  Immich *parent_;
};

}  // namespace esphome::immich
