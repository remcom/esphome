import esphome.codegen as cg
from esphome.components import text_sensor
import esphome.config_validation as cv
from esphome.const import CONF_ID, ENTITY_CATEGORY_DIAGNOSTIC

from . import CONF_AQARA_FP2_ID, FP2Component

DEPENDENCIES = ["aqara_fp2"]

CONF_RADAR_VERSION = "radar_version"

ICON_CHIP = "mdi:chip"

CONFIG_SCHEMA = {
    cv.GenerateID(CONF_ID): cv.declare_id(cg.EntityBase),
    cv.GenerateID(CONF_AQARA_FP2_ID): cv.use_id(FP2Component),
    cv.Optional(CONF_RADAR_VERSION): text_sensor.text_sensor_schema(
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        icon=ICON_CHIP,
    ),
}


async def to_code(config):
    parent = await cg.get_variable(config[CONF_AQARA_FP2_ID])
    if radar_version_config := config.get(CONF_RADAR_VERSION):
        sens = await text_sensor.new_text_sensor(radar_version_config)
        cg.add(parent.set_radar_version_text_sensor(sens))
