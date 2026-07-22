import esphome.codegen as cg
from esphome.components import sensor
import esphome.config_validation as cv
from esphome.const import CONF_ID, ENTITY_CATEGORY_DIAGNOSTIC, UNIT_DEGREES

from . import CONF_AQARA_FP2_ACCEL_ID, AqaraFP2Accel

DEPENDENCIES = ["aqara_fp2_accel"]

CONF_TILT_ANGLE = "tilt_angle"

ICON_ANGLE_ACUTE = "mdi:angle-acute"

CONFIG_SCHEMA = {
    cv.GenerateID(CONF_ID): cv.declare_id(cg.EntityBase),
    cv.GenerateID(CONF_AQARA_FP2_ACCEL_ID): cv.use_id(AqaraFP2Accel),
    cv.Optional(CONF_TILT_ANGLE): sensor.sensor_schema(
        unit_of_measurement=UNIT_DEGREES,
        accuracy_decimals=0,
        icon=ICON_ANGLE_ACUTE,
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    ),
}


async def to_code(config):
    parent = await cg.get_variable(config[CONF_AQARA_FP2_ACCEL_ID])
    if tilt_angle_config := config.get(CONF_TILT_ANGLE):
        sens = await sensor.new_sensor(tilt_angle_config)
        cg.add(parent.set_tilt_angle_sensor(sens))
