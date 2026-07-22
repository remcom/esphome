#include "sensor.h"
#include "esphome/core/log.h"

namespace esphome::flexispot_e7 {

static const char *const TAG = "flexispot_e7.sensor";

void FlexiSpotE7HeightSensor::setup() {
  this->parent_->add_on_height_callback([this](float height) { this->publish_state(height); });
  if (this->parent_->has_height()) {
    this->publish_state(this->parent_->get_height());
  }
}

void FlexiSpotE7HeightSensor::dump_config() { LOG_SENSOR("", "FlexiSpot E7 Height", this); }

}  // namespace esphome::flexispot_e7
