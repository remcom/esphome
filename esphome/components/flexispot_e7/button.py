import esphome.codegen as cg
from esphome.components import button
import esphome.config_validation as cv
from esphome.const import CONF_COMMAND

from . import COMMANDS, CONF_FLEXISPOT_E7_ID, FlexiSpotE7, flexispot_e7_ns

DEPENDENCIES = ["flexispot_e7"]

FlexiSpotE7Button = flexispot_e7_ns.class_(
    "FlexiSpotE7Button", button.Button, cg.Parented.template(FlexiSpotE7)
)

CONFIG_SCHEMA = button.button_schema(FlexiSpotE7Button).extend(
    {
        cv.GenerateID(CONF_FLEXISPOT_E7_ID): cv.use_id(FlexiSpotE7),
        cv.Required(CONF_COMMAND): cv.enum(COMMANDS, lower=True, space="_"),
    }
)


async def to_code(config):
    var = await button.new_button(config, config[CONF_COMMAND])
    await cg.register_parented(var, config[CONF_FLEXISPOT_E7_ID])
