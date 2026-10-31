from esphome import final_validate as fv, pins
import esphome.codegen as cg
from esphome.components.const import CONF_SLOT
from esphome.components.esp32 import (
    VARIANT_ESP32,
    VARIANT_ESP32P4,
    VARIANT_ESP32S3,
    add_idf_sdkconfig_option,
    get_esp32_variant,
    include_builtin_idf_component,
    only_on_variant,
)
from esphome.components.storage import CONF_MOUNT_POINT, Storage, validate_mount_point
import esphome.config_validation as cv
from esphome.const import (
    CONF_CHANNEL,
    CONF_CLK_PIN,
    CONF_DATA_PINS,
    CONF_FREQUENCY,
    CONF_ID,
    CONF_TYPE,
)
from esphome.types import ConfigType

AUTO_LOAD = ["storage"]
CODEOWNERS = ["@remcom"]
DOMAIN = "sd_card"

CONF_CARD_DETECT_PIN = "card_detect_pin"
CONF_CMD_PIN = "cmd_pin"
CONF_FORMAT_IF_MOUNT_FAILED = "format_if_mount_failed"
CONF_MAX_OPEN_FILES = "max_open_files"
CONF_POWER_LDO_CHANNEL = "power_ldo_channel"

sd_card_ns = cg.esphome_ns.namespace("sd_card")
SDCard = sd_card_ns.class_("SDCard", cg.Component, Storage)

# On the original ESP32, slot 1 of the SD/MMC host only works on these pins
ESP32_FIXED_PINS = {
    CONF_CLK_PIN: [14],
    CONF_CMD_PIN: [15],
    CONF_DATA_PINS: [2, 4, 12, 13],
}
# On the ESP32-P4, slot 0 only works on these pins and slot 1 can use any pins
ESP32P4_SLOT0_PINS = {
    CONF_CLK_PIN: [43],
    CONF_CMD_PIN: [44],
    CONF_DATA_PINS: [39, 40, 41, 42],
}

IDF_COMPONENTS = (
    "fatfs",
    "sdmmc",
    "esp_driver_sdmmc",
    "esp_driver_sdspi",
    "wear_levelling",
)


def _validate_bus_width(value: list[int]) -> list[int]:
    if len(value) not in (1, 4):
        raise cv.Invalid(
            "Use 1 data pin for a 1-bit bus or 4 data pins for a 4-bit bus"
        )
    return value


def _first_mismatch(config: ConfigType, fixed: dict[str, list[int]]) -> str | None:
    """Return the first pin option that differs from the fixed pins, or None."""
    for key, fixed_pins in fixed.items():
        value = config[key]
        configured = value if isinstance(value, list) else [value]
        if configured != fixed_pins[: len(configured)]:
            return key
    return None


def get_slot(config: ConfigType) -> int:
    """The SD/MMC host slot the configured pins belong to."""
    if (
        get_esp32_variant() == VARIANT_ESP32P4
        and _first_mismatch(config, ESP32P4_SLOT0_PINS) is None
    ):
        return 0
    return 1


def _validate_fixed_pins(config: ConfigType) -> ConfigType:
    if get_esp32_variant() != VARIANT_ESP32:
        return config
    if (key := _first_mismatch(config, ESP32_FIXED_PINS)) is not None:
        value = config[key]
        count = len(value) if isinstance(value, list) else 1
        names = ", ".join(f"GPIO{pin}" for pin in ESP32_FIXED_PINS[key][:count])
        raise cv.Invalid(f"On the ESP32, {key} must be {names}", path=[key])
    return config


def _validate_power_ldo(config: ConfigType) -> ConfigType:
    if CONF_POWER_LDO_CHANNEL in config and get_esp32_variant() != VARIANT_ESP32P4:
        raise cv.Invalid(
            f"{CONF_POWER_LDO_CHANNEL} is only available on the ESP32-P4",
            path=[CONF_POWER_LDO_CHANNEL],
        )
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(SDCard),
            cv.Optional(CONF_MOUNT_POINT, default="/sdcard"): validate_mount_point,
            cv.Required(CONF_CLK_PIN): pins.internal_gpio_output_pin_number,
            cv.Required(CONF_CMD_PIN): pins.internal_gpio_pin_number,
            cv.Required(CONF_DATA_PINS): cv.All(
                cv.ensure_list(pins.internal_gpio_pin_number), _validate_bus_width
            ),
            cv.Optional(CONF_CARD_DETECT_PIN): pins.gpio_input_pin_schema,
            cv.Optional(CONF_FREQUENCY, default="20MHz"): cv.All(
                cv.frequency, cv.Range(min=400e3, max=40e6)
            ),
            cv.Optional(CONF_MAX_OPEN_FILES, default=5): cv.int_range(min=1, max=20),
            cv.Optional(CONF_FORMAT_IF_MOUNT_FAILED, default=False): cv.boolean,
            # The on-chip LDO channel that powers the card and its pins, 4 on most ESP32-P4 boards
            cv.Optional(CONF_POWER_LDO_CHANNEL): cv.one_of(3, 4, int=True),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    cv.only_on_esp32,
    only_on_variant(
        supported=[VARIANT_ESP32, VARIANT_ESP32S3, VARIANT_ESP32P4],
        msg_prefix="The SD card component",
    ),
    _validate_fixed_pins,
    _validate_power_ldo,
)


def _final_validate(config: ConfigType) -> None:
    full_config = fv.full_config.get()
    slot = get_slot(config)
    hosted = full_config.get("esp32_hosted")
    if (
        hosted is not None
        and hosted.get(CONF_TYPE) == "sdio"
        and hosted.get(CONF_SLOT) == slot
    ):
        raise cv.Invalid(
            f"These pins use SD/MMC slot {slot}, which esp32_hosted already uses"
        )
    if (channel := config.get(CONF_POWER_LDO_CHANNEL)) is not None:
        for ldo in full_config.get("esp_ldo", []):
            if ldo[CONF_CHANNEL] == channel:
                raise cv.Invalid(
                    f"LDO channel {channel} is already configured in esp_ldo",
                    path=[CONF_POWER_LDO_CHANNEL],
                )


FINAL_VALIDATE_SCHEMA = _final_validate


async def to_code(config: ConfigType) -> None:
    var = cg.new_Pvariable(
        config[CONF_ID],
        config[CONF_MOUNT_POINT],
        get_slot(config),
        config[CONF_CLK_PIN],
        config[CONF_CMD_PIN],
        config[CONF_DATA_PINS],
    )
    await cg.register_component(var, config)

    if (card_detect := config.get(CONF_CARD_DETECT_PIN)) is not None:
        cg.add(var.set_card_detect_pin(await cg.gpio_pin_expression(card_detect)))
    cg.add(var.set_frequency_khz(int(config[CONF_FREQUENCY] // 1000)))
    cg.add(var.set_max_open_files(config[CONF_MAX_OPEN_FILES]))
    cg.add(var.set_format_if_mount_failed(config[CONF_FORMAT_IF_MOUNT_FAILED]))
    if (channel := config.get(CONF_POWER_LDO_CHANNEL)) is not None:
        cg.add_define("USE_SD_CARD_POWER_LDO")
        cg.add(var.set_power_ldo_channel(channel))

    for component in IDF_COMPONENTS:
        include_builtin_idf_component(component)
    # Long file names, held on the stack instead of allocated on the heap for every file operation
    add_idf_sdkconfig_option("CONFIG_FATFS_LFN_STACK", True)
    add_idf_sdkconfig_option("CONFIG_FATFS_API_ENCODING_UTF_8", True)
