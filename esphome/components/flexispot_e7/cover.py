import esphome.codegen as cg
from esphome.components import cover
import esphome.config_validation as cv

from . import CONF_FLEXISPOT_E7_ID, FlexiSpotE7, flexispot_e7_ns

DEPENDENCIES = ["flexispot_e7"]

CONF_MIN_HEIGHT = "min_height"
CONF_MAX_HEIGHT = "max_height"
CONF_STOP_TOLERANCE = "stop_tolerance"

FlexiSpotE7Cover = flexispot_e7_ns.class_("FlexiSpotE7Cover", cover.Cover, cg.Component)


def _validate_heights(config):
    if config[CONF_MIN_HEIGHT] >= config[CONF_MAX_HEIGHT]:
        raise cv.Invalid(f"'{CONF_MIN_HEIGHT}' must be lower than '{CONF_MAX_HEIGHT}'")
    return config


CONFIG_SCHEMA = cv.All(
    cover.cover_schema(FlexiSpotE7Cover, icon="mdi:desk")
    .extend(
        {
            cv.GenerateID(CONF_FLEXISPOT_E7_ID): cv.use_id(FlexiSpotE7),
            cv.Required(CONF_MIN_HEIGHT): cv.float_range(min=1.0),
            cv.Required(CONF_MAX_HEIGHT): cv.float_range(min=1.0),
            cv.Optional(CONF_STOP_TOLERANCE, default=1.0): cv.positive_float,
        }
    )
    .extend(cv.COMPONENT_SCHEMA),
    _validate_heights,
)


async def to_code(config):
    var = await cover.new_cover(config)
    await cg.register_component(var, config)
    parent = await cg.get_variable(config[CONF_FLEXISPOT_E7_ID])
    cg.add(var.set_parent(parent))
    cg.add(var.set_min_height(config[CONF_MIN_HEIGHT]))
    cg.add(var.set_max_height(config[CONF_MAX_HEIGHT]))
    cg.add(var.set_stop_tolerance(config[CONF_STOP_TOLERANCE]))
