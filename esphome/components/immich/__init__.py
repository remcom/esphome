import json

from esphome import automation
import esphome.codegen as cg
from esphome.components.http_request import CONF_HTTP_REQUEST_ID, HttpRequestComponent

try:
    # 2026.7.0 and later: online_image is a platform of `image:`
    from esphome.components.online_image.image import OnlineImage
except ImportError:
    # Remove fallback before 2027.1.0 (kept for external-component use on older releases)
    from esphome.components.online_image import OnlineImage
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_URL
from esphome.types import ConfigType

AUTO_LOAD = ["json"]
# online_image is not listed as a dependency because it is configured as a
# platform of `image:`; cv.use_id(OnlineImage) already requires an instance.
DEPENDENCIES = ["http_request"]
CODEOWNERS = ["@remcom"]
MULTI_CONF = True

CONF_API_KEY = "api_key"
CONF_ALBUM_ID = "album_id"
CONF_IMAGE_ID = "image_id"

immich_ns = cg.esphome_ns.namespace("immich")
Immich = immich_ns.class_("Immich", cg.PollingComponent)

StartAction = immich_ns.class_("StartAction", automation.Action)
StopAction = immich_ns.class_("StopAction", automation.Action)
NextImageAction = immich_ns.class_("NextImageAction", automation.Action)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(Immich),
        cv.GenerateID(CONF_HTTP_REQUEST_ID): cv.use_id(HttpRequestComponent),
        cv.Required(CONF_URL): cv.url,
        cv.Required(CONF_API_KEY): cv.string_strict,
        cv.Required(CONF_ALBUM_ID): cv.uuid,
        cv.Required(CONF_IMAGE_ID): cv.use_id(OnlineImage),
    }
).extend(cv.polling_component_schema("30s"))


async def to_code(config: ConfigType) -> None:
    image = await cg.get_variable(config[CONF_IMAGE_ID])
    base_url = config[CONF_URL].rstrip("/")
    album_id = str(config[CONF_ALBUM_ID])
    # Ask for one random image asset from the album; videos are excluded. People and
    # EXIF data are left out to keep the response small, only the asset id is used.
    search_body = json.dumps(
        {
            "albumIds": [album_id],
            "size": 1,
            "type": "IMAGE",
            "withExif": False,
            "withPeople": False,
        },
        separators=(",", ":"),
    )
    var = cg.new_Pvariable(
        config[CONF_ID],
        image,
        base_url,
        f"{base_url}/api/search/random",
        config[CONF_API_KEY],
        album_id,
        search_body,
    )
    await cg.register_component(var, config)
    await cg.register_parented(var, config[CONF_HTTP_REQUEST_ID])


IMMICH_ACTION_SCHEMA = automation.maybe_simple_id(
    {
        cv.GenerateID(): cv.use_id(Immich),
    }
)

automation.register_simple_action(
    "immich.start", StartAction, IMMICH_ACTION_SCHEMA, synchronous=True
)
automation.register_simple_action(
    "immich.stop", StopAction, IMMICH_ACTION_SCHEMA, synchronous=True
)
automation.register_simple_action(
    "immich.next_image", NextImageAction, IMMICH_ACTION_SCHEMA, synchronous=True
)
automation.register_apply_condition(
    "immich.is_running", IMMICH_ACTION_SCHEMA, "is_running()"
)
