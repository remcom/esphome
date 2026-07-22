import esphome.codegen as cg
from esphome.components import text_sensor
import esphome.config_validation as cv
from esphome.const import CONF_ID, ENTITY_CATEGORY_DIAGNOSTIC

from . import CONF_AQARA_FP2_ACCEL_ID, AqaraFP2Accel

DEPENDENCIES = ["aqara_fp2_accel"]

CONF_ORIENTATION = "orientation"

ICON_SCREEN_ROTATION = "mdi:screen-rotation"

CONFIG_SCHEMA = {
    cv.GenerateID(CONF_ID): cv.declare_id(cg.EntityBase),
    cv.GenerateID(CONF_AQARA_FP2_ACCEL_ID): cv.use_id(AqaraFP2Accel),
    cv.Optional(CONF_ORIENTATION): text_sensor.text_sensor_schema(
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        icon=ICON_SCREEN_ROTATION,
    ),
}


async def to_code(config):
    parent = await cg.get_variable(config[CONF_AQARA_FP2_ACCEL_ID])
    if orientation_config := config.get(CONF_ORIENTATION):
        sens = await text_sensor.new_text_sensor(orientation_config)
        cg.add(parent.set_orientation_text_sensor(sens))
