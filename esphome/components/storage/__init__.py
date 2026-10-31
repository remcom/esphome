from collections.abc import Iterator
from typing import Any

from esphome import final_validate as fv
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome.core import ID
from esphome.cpp_generator import MockObj, MockObjClass
from esphome.types import ConfigType

CODEOWNERS = ["@remcom"]
DOMAIN = "storage"

storage_ns = cg.esphome_ns.namespace("storage")
Storage = storage_ns.class_("Storage")

CONF_MOUNT_POINT = "mount_point"

# ESP-IDF registers its own devices, such as /dev/uart/0, below this path
RESERVED_MOUNT_POINTS = ("/dev",)


def validate_mount_point(value: Any) -> str:
    """A VFS base path: an absolute path of a single level, such as ``/sdcard``."""
    value = cv.string_strict(value)
    if not value.startswith("/") or len(value) < 2 or "/" in value[1:]:
        raise cv.Invalid("Mount point must be a single path level such as '/sdcard'")
    # ESP-IDF limits a VFS base path to ESP_VFS_PATH_MAX (15) characters
    if len(value) > 15:
        raise cv.Invalid("Mount point can be at most 15 characters long")
    if value in RESERVED_MOUNT_POINTS:
        raise cv.Invalid(f"Mount point {value} is reserved for ESP-IDF")
    return value


def storage_schema(class_: MockObjClass, default_mount_point: str) -> cv.Schema:
    """The options every storage has. A storage driver extends this with its own options."""
    return cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(class_),
            cv.Optional(
                CONF_MOUNT_POINT, default=default_mount_point
            ): validate_mount_point,
        }
    )


async def new_storage(config: ConfigType, *args: Any) -> MockObj:
    """Create a storage. Its C++ constructor takes the mount point first, then args."""
    return cg.new_Pvariable(config[CONF_ID], config[CONF_MOUNT_POINT], *args)


def _storage_configs(full_config: ConfigType) -> Iterator[ConfigType]:
    """Every top-level config, or list entry, whose id is a Storage."""
    for value in full_config.values():
        for item in value if isinstance(value, list) else [value]:
            if (
                isinstance(item, dict)
                and isinstance(id_ := item.get(CONF_ID), ID)
                and id_.type is not None
                and id_.type.inherits_from(Storage)
            ):
                yield item


def _final_validate(config: ConfigType) -> None:
    owners: dict[str, ID] = {}
    for item in _storage_configs(fv.full_config.get()):
        mount_point = item[CONF_MOUNT_POINT]
        if (owner := owners.get(mount_point)) is not None:
            raise cv.Invalid(
                f"Mount point {mount_point} is used by both '{owner}' and '{item[CONF_ID]}'"
            )
        owners[mount_point] = item[CONF_ID]


FINAL_VALIDATE_SCHEMA = _final_validate
