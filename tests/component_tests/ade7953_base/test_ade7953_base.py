"""Tests for the ADE7953 configuration validation."""

import pytest

from esphome import config_validation as cv
from esphome.components.ade7953_base import (
    CONF_ACTIVE_ENERGY_A,
    validate_energy_update_interval,
)
from esphome.const import CONF_UPDATE_INTERVAL, CONF_VOLTAGE


@pytest.mark.parametrize("interval", ["181s", "10min", "never"])
def test_energy_rejects_long_update_interval(interval: str) -> None:
    """The energy registers would overflow between two reads."""
    config = {
        CONF_UPDATE_INTERVAL: cv.update_interval(interval),
        CONF_ACTIVE_ENERGY_A: {},
    }
    with pytest.raises(cv.Invalid, match="at most 180s"):
        validate_energy_update_interval(config)


def test_energy_accepts_update_interval_up_to_the_limit() -> None:
    config = {
        CONF_UPDATE_INTERVAL: cv.update_interval("180s"),
        CONF_ACTIVE_ENERGY_A: {},
    }
    assert validate_energy_update_interval(config) is config


def test_long_update_interval_without_energy_is_accepted() -> None:
    config = {CONF_UPDATE_INTERVAL: cv.update_interval("10min"), CONF_VOLTAGE: {}}
    assert validate_energy_update_interval(config) is config
