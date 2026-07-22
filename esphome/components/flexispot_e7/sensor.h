#pragma once

#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/components/sensor/sensor.h"

#include "flexispot_e7.h"

namespace esphome::flexispot_e7 {

class FlexiSpotE7HeightSensor : public sensor::Sensor, public Component, public Parented<FlexiSpotE7> {
 public:
  void setup() override;
  void dump_config() override;
};

}  // namespace esphome::flexispot_e7
