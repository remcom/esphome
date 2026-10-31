"""Tests for the sd_card config validation."""

import pytest

from esphome import config_validation as cv
from esphome.components.esp32 import KEY_VARIANT, VARIANT_ESP32, VARIANT_ESP32S3
from esphome.components.sd_card import (
    CONF_CMD_PIN,
    _validate_bus_width,
    _validate_fixed_pins,
)
from esphome.const import CONF_CLK_PIN, CONF_DATA_PINS, PlatformFramework
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
