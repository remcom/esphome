import esphome.codegen as cg
from esphome.components import sensor
import esphome.config_validation as cv
from esphome.const import (
    DEVICE_CLASS_DISTANCE,
    STATE_CLASS_MEASUREMENT,
    UNIT_CENTIMETER,
)

from . import CONF_FLEXISPOT_E7_ID, FlexiSpotE7, flexispot_e7_ns

DEPENDENCIES = ["flexispot_e7"]

FlexiSpotE7HeightSensor = flexispot_e7_ns.class_(
    "FlexiSpotE7HeightSensor", sensor.Sensor, cg.Component
)

CONFIG_SCHEMA = sensor.sensor_schema(
    FlexiSpotE7HeightSensor,
    unit_of_measurement=UNIT_CENTIMETER,
    accuracy_decimals=1,
    device_class=DEVICE_CLASS_DISTANCE,
    state_class=STATE_CLASS_MEASUREMENT,
    icon="mdi:desk",
).extend(
    {
        cv.GenerateID(CONF_FLEXISPOT_E7_ID): cv.use_id(FlexiSpotE7),
    }
)


async def to_code(config):
    var = await sensor.new_sensor(config)
    await cg.register_component(var, config)
    parent = await cg.get_variable(config[CONF_FLEXISPOT_E7_ID])
    cg.add(var.set_parent(parent))
