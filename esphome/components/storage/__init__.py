import esphome.codegen as cg
import esphome.config_validation as cv

CODEOWNERS = ["@remcom"]
DOMAIN = "storage"

storage_ns = cg.esphome_ns.namespace("storage")
Storage = storage_ns.class_("Storage")

CONF_MOUNT_POINT = "mount_point"


def validate_mount_point(value: str) -> str:
    """A VFS base path: an absolute path of a single level, such as ``/sdcard``."""
    value = cv.string_strict(value)
    if not value.startswith("/") or len(value) < 2 or "/" in value[1:]:
        raise cv.Invalid("Mount point must be a single path level such as '/sdcard'")
    # ESP-IDF limits a VFS base path to ESP_VFS_PATH_MAX (15) characters
    if len(value) > 15:
        raise cv.Invalid("Mount point can be at most 15 characters long")
    return value
