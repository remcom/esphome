from esphome.components import ade7953_base
from esphome.types import ConfigType

CONFIG_SCHEMA = ade7953_base.ADE7953_BINARY_SENSOR_SCHEMA


async def to_code(config: ConfigType) -> None:
    await ade7953_base.register_ade7953_binary_sensors(config)
