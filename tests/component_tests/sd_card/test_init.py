"""Tests for the sd_card config validation."""

import pytest

from esphome import config_validation as cv
from esphome.components.const import CONF_SLOT
from esphome.components.esp32 import (
    KEY_VARIANT,
    VARIANT_ESP32,
    VARIANT_ESP32P4,
    VARIANT_ESP32S3,
)
from esphome.components.sd_card import (
    CONF_CMD_PIN,
    CONF_POWER_LDO_CHANNEL,
    _final_validate,
    _validate_bus_width,
    _validate_fixed_pins,
    _validate_power_ldo,
    get_slot,
)
from esphome.const import (
    CONF_CHANNEL,
    CONF_CLK_PIN,
    CONF_DATA_PINS,
    CONF_TYPE,
    PlatformFramework,
)
from esphome.types import ConfigType

from ..types import SetCoreConfigCallable


def _pins(clk: int, cmd: int, data: list[int]) -> ConfigType:
    return {CONF_CLK_PIN: clk, CONF_CMD_PIN: cmd, CONF_DATA_PINS: data}


@pytest.fixture
def esp32_core(set_core_config: SetCoreConfigCallable) -> None:
    set_core_config(
        PlatformFramework.ESP32_IDF, platform_data={KEY_VARIANT: VARIANT_ESP32}
    )


@pytest.mark.parametrize("data_pins", [[2], [2, 4, 12, 13]])
def test_bus_width_accepts_1_and_4_pins(data_pins: list[int]) -> None:
    assert _validate_bus_width(data_pins) == data_pins


@pytest.mark.parametrize("data_pins", [[2, 4], [2, 4, 12]])
def test_bus_width_rejects_2_and_3_pins(data_pins: list[int]) -> None:
    with pytest.raises(cv.Invalid, match="1 data pin for a 1-bit bus"):
        _validate_bus_width(data_pins)


@pytest.mark.usefixtures("esp32_core")
@pytest.mark.parametrize("data_pins", [[2], [2, 4, 12, 13]])
def test_fixed_pins_accepts_esp32_slot_pins(data_pins: list[int]) -> None:
    config = _pins(14, 15, data_pins)
    assert _validate_fixed_pins(config) is config


@pytest.mark.usefixtures("esp32_core")
@pytest.mark.parametrize(
    ("config", "key", "message"),
    [
        (_pins(13, 15, [2]), CONF_CLK_PIN, "On the ESP32, clk_pin must be GPIO14"),
        (_pins(14, 16, [2]), CONF_CMD_PIN, "On the ESP32, cmd_pin must be GPIO15"),
        (_pins(14, 15, [4]), CONF_DATA_PINS, "On the ESP32, data_pins must be GPIO2"),
        (
            _pins(14, 15, [2, 4, 13, 12]),
            CONF_DATA_PINS,
            "On the ESP32, data_pins must be GPIO2, GPIO4, GPIO12, GPIO13",
        ),
    ],
)
def test_fixed_pins_rejects_other_pins_on_esp32(
    config: ConfigType, key: str, message: str
) -> None:
    with pytest.raises(cv.Invalid) as err:
        _validate_fixed_pins(config)
    assert err.value.msg == message
    assert err.value.path == [key]


def test_fixed_pins_allows_any_pins_on_esp32s3(
    set_core_config: SetCoreConfigCallable,
) -> None:
    set_core_config(
        PlatformFramework.ESP32_IDF, platform_data={KEY_VARIANT: VARIANT_ESP32S3}
    )
    config = _pins(39, 38, [40, 41, 42, 1])
    assert _validate_fixed_pins(config) is config


@pytest.mark.parametrize(
    ("variant", "config", "slot"),
    [
        (VARIANT_ESP32, _pins(14, 15, [2]), 1),
        (VARIANT_ESP32S3, _pins(43, 44, [39]), 1),
        (VARIANT_ESP32P4, _pins(43, 44, [39]), 0),
        (VARIANT_ESP32P4, _pins(43, 44, [39, 40, 41, 42]), 0),
        (VARIANT_ESP32P4, _pins(43, 44, [39, 40, 42, 41]), 1),
        (VARIANT_ESP32P4, _pins(18, 19, [14]), 1),
    ],
)
def test_slot_follows_pins(
    set_core_config: SetCoreConfigCallable,
    variant: str,
    config: ConfigType,
    slot: int,
) -> None:
    set_core_config(PlatformFramework.ESP32_IDF, platform_data={KEY_VARIANT: variant})
    assert get_slot(config) == slot


def test_power_ldo_allowed_on_esp32p4(set_core_config: SetCoreConfigCallable) -> None:
    set_core_config(
        PlatformFramework.ESP32_IDF, platform_data={KEY_VARIANT: VARIANT_ESP32P4}
    )
    config = {CONF_POWER_LDO_CHANNEL: 4}
    assert _validate_power_ldo(config) is config


@pytest.mark.usefixtures("esp32_core")
def test_power_ldo_rejected_on_other_variants() -> None:
    with pytest.raises(cv.Invalid) as err:
        _validate_power_ldo({CONF_POWER_LDO_CHANNEL: 4})
    assert err.value.msg == "power_ldo_channel is only available on the ESP32-P4"
    assert err.value.path == [CONF_POWER_LDO_CHANNEL]


@pytest.mark.usefixtures("esp32_core")
def test_power_ldo_not_set_passes_on_other_variants() -> None:
    config = _pins(14, 15, [2])
    assert _validate_power_ldo(config) is config


def _p4_with(
    set_core_config: SetCoreConfigCallable, full_config: dict[str, ConfigType]
) -> None:
    set_core_config(
        PlatformFramework.ESP32_IDF,
        platform_data={KEY_VARIANT: VARIANT_ESP32P4},
        full_config=full_config,
    )


def _hosted(slot: int, bus: str = "sdio") -> ConfigType:
    return {CONF_TYPE: bus, CONF_SLOT: slot}


@pytest.mark.parametrize(
    ("full_config", "config"),
    [
        ({}, {**_pins(18, 19, [14]), CONF_POWER_LDO_CHANNEL: 4}),
        ({"esp32_hosted": _hosted(1)}, _pins(43, 44, [39])),
        ({"esp32_hosted": _hosted(0, "spi")}, _pins(43, 44, [39])),
        (
            {"esp_ldo": [{CONF_CHANNEL: 3}]},
            {**_pins(43, 44, [39]), CONF_POWER_LDO_CHANNEL: 4},
        ),
    ],
)
def test_final_validate_accepts(
    set_core_config: SetCoreConfigCallable,
    full_config: dict[str, ConfigType],
    config: ConfigType,
) -> None:
    _p4_with(set_core_config, full_config)
    _final_validate(config)


def test_final_validate_rejects_slot_used_by_hosted(
    set_core_config: SetCoreConfigCallable,
) -> None:
    _p4_with(set_core_config, {"esp32_hosted": _hosted(1)})
    with pytest.raises(cv.Invalid) as err:
        _final_validate(_pins(18, 19, [14]))
    assert (
        err.value.msg == "These pins use SD/MMC slot 1, which esp32_hosted already uses"
    )


def test_final_validate_rejects_ldo_used_by_esp_ldo(
    set_core_config: SetCoreConfigCallable,
) -> None:
    _p4_with(set_core_config, {"esp_ldo": [{CONF_CHANNEL: 4}]})
    with pytest.raises(cv.Invalid) as err:
        _final_validate({**_pins(43, 44, [39]), CONF_POWER_LDO_CHANNEL: 4})
    assert err.value.msg == "LDO channel 4 is already configured in esp_ldo"
    assert err.value.path == [CONF_POWER_LDO_CHANNEL]
