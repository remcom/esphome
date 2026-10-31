"""Tests for the storage mount point validation."""

from typing import Any

import pytest

from esphome import codegen as cg, config_validation as cv
from esphome.components.storage import (
    CONF_MOUNT_POINT,
    Storage,
    _final_validate,
    storage_ns,
    storage_schema,
    validate_mount_point,
)
from esphome.const import CONF_ID, PlatformFramework
from esphome.core import ID
from esphome.cpp_generator import MockObjClass
from esphome.types import ConfigType

from ..types import SetCoreConfigCallable


@pytest.mark.parametrize("value", ["/sdcard", "/a", "/" + "a" * 14])
def test_mount_point_accepts_single_level(value: str) -> None:
    assert validate_mount_point(value) == value


@pytest.mark.parametrize("value", ["sdcard", "/", "/sd/card", "/sdcard/"])
def test_mount_point_rejects_bad_paths(value: str) -> None:
    with pytest.raises(cv.Invalid, match="single path level"):
        validate_mount_point(value)


def test_mount_point_rejects_too_long() -> None:
    with pytest.raises(cv.Invalid, match="at most 15 characters"):
        validate_mount_point("/" + "a" * 15)


def test_mount_point_rejects_non_string() -> None:
    with pytest.raises(cv.Invalid):
        validate_mount_point(123)


def test_mount_point_rejects_reserved_path() -> None:
    with pytest.raises(cv.Invalid, match="reserved for ESP-IDF"):
        validate_mount_point("/dev")


def test_storage_schema_declares_id_and_default_mount_point() -> None:
    schema = storage_schema(Storage, "/data")
    config = schema({CONF_ID: "my_storage"})
    assert config[CONF_MOUNT_POINT] == "/data"
    assert config[CONF_ID].type is Storage
    assert schema({CONF_MOUNT_POINT: "/photos"})[CONF_MOUNT_POINT] == "/photos"


def _storage(name: str, mount_point: str, type_: MockObjClass = Storage) -> ConfigType:
    return {
        CONF_ID: ID(name, is_declaration=True, type=type_),
        CONF_MOUNT_POINT: mount_point,
    }


def _run_final_validate(
    set_core_config: SetCoreConfigCallable, full_config: dict[str, Any]
) -> None:
    set_core_config(PlatformFramework.ESP32_IDF, full_config=full_config)
    _final_validate({})


def test_final_validate_accepts_distinct_mount_points(
    set_core_config: SetCoreConfigCallable,
) -> None:
    _run_final_validate(
        set_core_config,
        {
            "sd_card": _storage("card", "/sdcard"),
            "flash_storage": [_storage("data", "/data"), _storage("logs", "/logs")],
            # Not a storage, so its mount_point is not compared
            "other": _storage("thing", "/sdcard", cg.Component),
            "logger": {"level": "DEBUG"},
            "ota": ["not a dict"],
        },
    )


@pytest.mark.parametrize(
    "full_config",
    [
        {
            "sd_card": _storage("card", "/sdcard"),
            "flash_storage": [_storage("data", "/sdcard")],
        },
        {"flash_storage": [_storage("card", "/sdcard"), _storage("data", "/sdcard")]},
    ],
)
def test_final_validate_rejects_shared_mount_point(
    set_core_config: SetCoreConfigCallable, full_config: dict[str, Any]
) -> None:
    with pytest.raises(cv.Invalid) as err:
        _run_final_validate(set_core_config, full_config)
    assert err.value.msg == "Mount point /sdcard is used by both 'card' and 'data'"


def test_final_validate_finds_subclasses(
    set_core_config: SetCoreConfigCallable,
) -> None:
    sd_card = storage_ns.class_("SDCard", cg.Component, Storage)
    with pytest.raises(cv.Invalid, match="used by both"):
        _run_final_validate(
            set_core_config,
            {
                "a": _storage("card", "/sdcard", sd_card),
                "b": _storage("data", "/sdcard", sd_card),
            },
        )
