import esphome.codegen as cg
from esphome.components import i2c
import esphome.config_validation as cv
from esphome.const import CONF_ID

CODEOWNERS = ["@remcom"]
DEPENDENCIES = ["i2c"]

aqara_fp2_accel_ns = cg.esphome_ns.namespace("aqara_fp2_accel")
AqaraFP2Accel = aqara_fp2_accel_ns.class_(
    "AqaraFP2Accel", cg.PollingComponent, i2c.I2CDevice
)

CONF_AQARA_FP2_ACCEL_ID = "aqara_fp2_accel_id"

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(AqaraFP2Accel),
        }
    )
    .extend(cv.polling_component_schema("100ms"))
    .extend(i2c.i2c_device_schema(0x27))
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await i2c.register_i2c_device(var, config)
