"""Tests for the storage mount point validation."""

import pytest

from esphome import config_validation as cv
from esphome.components.storage import validate_mount_point


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
