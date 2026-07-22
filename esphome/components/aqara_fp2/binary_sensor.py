import esphome.codegen as cg
from esphome.components import binary_sensor
import esphome.config_validation as cv
from esphome.const import (
    CONF_HAS_TARGET,
    CONF_ID,
    CONF_MOTION,
    DEVICE_CLASS_MOTION,
    DEVICE_CLASS_OCCUPANCY,
)

from . import CONF_AQARA_FP2_ID, CONF_PRESENCE, FP2Component

DEPENDENCIES = ["aqara_fp2"]

CONFIG_SCHEMA = {
    cv.GenerateID(CONF_ID): cv.declare_id(cg.EntityBase),
    cv.GenerateID(CONF_AQARA_FP2_ID): cv.use_id(FP2Component),
    cv.Optional(CONF_PRESENCE): binary_sensor.binary_sensor_schema(
        device_class=DEVICE_CLASS_OCCUPANCY,
    ),
    cv.Optional(CONF_MOTION): binary_sensor.binary_sensor_schema(
        device_class=DEVICE_CLASS_MOTION,
    ),
    cv.Optional(CONF_HAS_TARGET): binary_sensor.binary_sensor_schema(
        device_class=DEVICE_CLASS_OCCUPANCY,
    ),
}


async def to_code(config):
    parent = await cg.get_variable(config[CONF_AQARA_FP2_ID])
    if presence_config := config.get(CONF_PRESENCE):
        sens = await binary_sensor.new_binary_sensor(presence_config)
        cg.add(parent.set_presence_binary_sensor(sens))
    if motion_config := config.get(CONF_MOTION):
        sens = await binary_sensor.new_binary_sensor(motion_config)
        cg.add(parent.set_motion_binary_sensor(sens))
    if has_target_config := config.get(CONF_HAS_TARGET):
        sens = await binary_sensor.new_binary_sensor(has_target_config)
        cg.add(parent.set_target_binary_sensor(sens))
