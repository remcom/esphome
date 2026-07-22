from esphome import pins
import esphome.codegen as cg
from esphome.components import uart
import esphome.config_validation as cv
from esphome.const import CONF_ID

CODEOWNERS = ["@NelsonBrandao"]
DEPENDENCIES = ["uart"]
MULTI_CONF = True

CONF_FLEXISPOT_E7_ID = "flexispot_e7_id"
CONF_SCREEN_PIN = "screen_pin"
CONF_WAKE_INTERVAL = "wake_interval"

flexispot_e7_ns = cg.esphome_ns.namespace("flexispot_e7")
FlexiSpotE7 = flexispot_e7_ns.class_("FlexiSpotE7", cg.Component, uart.UARTDevice)

FlexiSpotCommand = flexispot_e7_ns.enum("FlexiSpotCommand")
COMMANDS = {
    "up": FlexiSpotCommand.FLEXISPOT_CMD_UP,
    "down": FlexiSpotCommand.FLEXISPOT_CMD_DOWN,
    "wake": FlexiSpotCommand.FLEXISPOT_CMD_WAKE,
    "memory": FlexiSpotCommand.FLEXISPOT_CMD_MEMORY,
    "preset_1": FlexiSpotCommand.FLEXISPOT_CMD_PRESET_1,
    "preset_2": FlexiSpotCommand.FLEXISPOT_CMD_PRESET_2,
    "preset_3": FlexiSpotCommand.FLEXISPOT_CMD_PRESET_3,
    "preset_4": FlexiSpotCommand.FLEXISPOT_CMD_PRESET_4,
}

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(FlexiSpotE7),
            cv.Optional(CONF_SCREEN_PIN): pins.gpio_output_pin_schema,
            cv.Optional(
                CONF_WAKE_INTERVAL, default="3s"
            ): cv.positive_time_period_milliseconds,
        }
    )
    .extend(uart.UART_DEVICE_SCHEMA)
    .extend(cv.COMPONENT_SCHEMA)
)

FINAL_VALIDATE_SCHEMA = uart.final_validate_device_schema(
    "flexispot_e7", baud_rate=9600, require_tx=True, require_rx=True
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    cg.add(var.set_wake_interval(config[CONF_WAKE_INTERVAL]))
    if screen_pin_config := config.get(CONF_SCREEN_PIN):
        screen_pin = await cg.gpio_pin_expression(screen_pin_config)
        cg.add(var.set_screen_pin(screen_pin))
