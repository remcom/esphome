import esphome.codegen as cg
from esphome.components import binary_sensor
import esphome.config_validation as cv
from esphome.const import CONF_ID, DEVICE_CLASS_VIBRATION, ENTITY_CATEGORY_DIAGNOSTIC

from . import CONF_AQARA_FP2_ACCEL_ID, AqaraFP2Accel

DEPENDENCIES = ["aqara_fp2_accel"]

CONF_VIBRATION = "vibration"

CONFIG_SCHEMA = {
    cv.GenerateID(CONF_ID): cv.declare_id(cg.EntityBase),
    cv.GenerateID(CONF_AQARA_FP2_ACCEL_ID): cv.use_id(AqaraFP2Accel),
    cv.Optional(CONF_VIBRATION): binary_sensor.binary_sensor_schema(
        device_class=DEVICE_CLASS_VIBRATION,
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    ),
}


async def to_code(config):
    parent = await cg.get_variable(config[CONF_AQARA_FP2_ACCEL_ID])
    if vibration_config := config.get(CONF_VIBRATION):
        sens = await binary_sensor.new_binary_sensor(vibration_config)
        cg.add(parent.set_vibration_binary_sensor(sens))
