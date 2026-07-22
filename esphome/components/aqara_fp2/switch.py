import esphome.codegen as cg
from esphome.components import switch
import esphome.config_validation as cv
from esphome.const import CONF_ID, ENTITY_CATEGORY_CONFIG

from . import CONF_AQARA_FP2_ID, FP2Component, LocationReportSwitch

DEPENDENCIES = ["aqara_fp2"]

CONF_LOCATION_REPORT = "location_report"

ICON_CROSSHAIRS_GPS = "mdi:crosshairs-gps"

CONFIG_SCHEMA = {
    cv.GenerateID(CONF_ID): cv.declare_id(cg.EntityBase),
    cv.GenerateID(CONF_AQARA_FP2_ID): cv.use_id(FP2Component),
    cv.Optional(CONF_LOCATION_REPORT): switch.switch_schema(
        LocationReportSwitch,
        entity_category=ENTITY_CATEGORY_CONFIG,
        icon=ICON_CROSSHAIRS_GPS,
    ),
}


async def to_code(config):
    parent = await cg.get_variable(config[CONF_AQARA_FP2_ID])
    if location_report_config := config.get(CONF_LOCATION_REPORT):
        s = await switch.new_switch(location_report_config)
        await cg.register_parented(s, config[CONF_AQARA_FP2_ID])
        cg.add(parent.set_location_report_switch(s))
