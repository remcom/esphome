from esphome import pins
import esphome.codegen as cg

# Aliased to avoid clashing with this package's own binary_sensor.py platform submodule, which the
# component loader binds as an attribute of this package.
from esphome.components import binary_sensor as binary_sensor_, uart
import esphome.config_validation as cv
from esphome.const import (
    CONF_ID,
    CONF_MOTION,
    CONF_RESET_PIN,
    CONF_SENSITIVITY,
    DEVICE_CLASS_MOTION,
    DEVICE_CLASS_OCCUPANCY,
)

from ..aqara_fp2_accel import AqaraFP2Accel

CODEOWNERS = ["@remcom"]
DEPENDENCIES = ["uart"]
AUTO_LOAD = ["ld24xx"]

aqara_fp2_ns = cg.esphome_ns.namespace("aqara_fp2")
FP2Component = aqara_fp2_ns.class_("FP2Component", cg.Component, uart.UARTDevice)
LocationReportSwitch = aqara_fp2_ns.class_("LocationReportSwitch")

CONF_AQARA_FP2_ID = "aqara_fp2_id"

CONF_ACCEL = "accel"
CONF_MOUNTING_POSITION = "mounting_position"
CONF_LEFT_RIGHT_REVERSE = "left_right_reverse"
CONF_PRESENCE_SENSITIVITY = "presence_sensitivity"
CONF_INTERFERENCE_GRID = "interference_grid"
CONF_EXIT_GRID = "exit_grid"
CONF_EDGE_GRID = "edge_grid"
CONF_ZONES = "zones"
CONF_GRID = "grid"
CONF_PRESENCE = "presence"

MOUNTING_POSITIONS = {
    "wall": 0x01,
    "left_corner": 0x02,
    "right_corner": 0x03,
}

SENSITIVITY_LEVELS = {
    "low": 1,
    "medium": 2,
    "high": 3,
}

# Active detection grid geometry. Users describe a 14x14 area which is centered inside the
# radar's native 20-row x 16-column (320-bit / 40-byte) map.
GRID_INPUT_ROWS = 14
GRID_INPUT_COLS = 14
GRID_COL_OFFSET = 2


def parse_ascii_grid(value):
    """Parse a 14x14 ASCII grid into the radar's 40-byte map.

    'x'/'X' mark an active cell; '.'/space mark an inactive cell.
    """
    value = cv.string(value)
    lines = [line.strip() for line in value.strip().splitlines() if line.strip()]
    if len(lines) != GRID_INPUT_ROWS:
        raise cv.Invalid(
            f"Grid must have exactly {GRID_INPUT_ROWS} rows, got {len(lines)}"
        )

    grid = bytearray(40)
    for row, line in enumerate(lines):
        cells = line.replace(" ", "")
        if len(cells) != GRID_INPUT_COLS:
            raise cv.Invalid(
                f"Row {row + 1} must have {GRID_INPUT_COLS} cells, got {len(cells)}: '{cells}'"
            )
        row_bits = 0
        for col, char in enumerate(cells):
            if char in ("x", "X"):
                # Bit 15 is column 0; the active area starts GRID_COL_OFFSET columns in.
                row_bits |= 1 << (15 - (col + GRID_COL_OFFSET))
            elif char not in (".", "-"):
                raise cv.Invalid(f"Invalid grid character '{char}' in row {row + 1}")
        grid[row * 2] = (row_bits >> 8) & 0xFF
        grid[row * 2 + 1] = row_bits & 0xFF
    return list(grid)


ZONE_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_SENSITIVITY, default="medium"): cv.enum(SENSITIVITY_LEVELS),
        cv.Required(CONF_GRID): parse_ascii_grid,
        cv.Optional(CONF_PRESENCE): binary_sensor_.binary_sensor_schema(
            device_class=DEVICE_CLASS_OCCUPANCY,
        ),
        cv.Optional(CONF_MOTION): binary_sensor_.binary_sensor_schema(
            device_class=DEVICE_CLASS_MOTION,
        ),
    }
)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(FP2Component),
            cv.Optional(CONF_ACCEL): cv.use_id(AqaraFP2Accel),
            cv.Optional(CONF_RESET_PIN): pins.gpio_output_pin_schema,
            cv.Optional(CONF_MOUNTING_POSITION, default="wall"): cv.enum(
                MOUNTING_POSITIONS
            ),
            cv.Optional(CONF_LEFT_RIGHT_REVERSE, default=False): cv.boolean,
            cv.Optional(CONF_PRESENCE_SENSITIVITY, default="medium"): cv.enum(
                SENSITIVITY_LEVELS
            ),
            cv.Optional(CONF_INTERFERENCE_GRID): parse_ascii_grid,
            cv.Optional(CONF_EXIT_GRID): parse_ascii_grid,
            cv.Optional(CONF_EDGE_GRID): parse_ascii_grid,
            cv.Optional(CONF_ZONES): cv.ensure_list(ZONE_SCHEMA),
        }
    )
    .extend(uart.UART_DEVICE_SCHEMA)
    .extend(cv.COMPONENT_SCHEMA)
)

# The stock FP2 firmware talks to the radar at 890000 baud, 8N1. The baud rate is verified at
# runtime via check_uart_settings() rather than enforced here so shared test buses can be reused.
FINAL_VALIDATE_SCHEMA = uart.final_validate_device_schema(
    "aqara_fp2",
    require_tx=True,
    require_rx=True,
    parity="NONE",
    stop_bits=1,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    if (accel := config.get(CONF_ACCEL)) is not None:
        cg.add(var.set_accel(await cg.get_variable(accel)))
    if (reset_pin := config.get(CONF_RESET_PIN)) is not None:
        cg.add(var.set_reset_pin(await cg.gpio_pin_expression(reset_pin)))

    cg.add(var.set_mounting_position(config[CONF_MOUNTING_POSITION]))
    cg.add(var.set_left_right_reverse(config[CONF_LEFT_RIGHT_REVERSE]))
    cg.add(var.set_presence_sensitivity(config[CONF_PRESENCE_SENSITIVITY]))

    if (grid := config.get(CONF_INTERFERENCE_GRID)) is not None:
        cg.add(var.set_interference_grid(grid))
    if (grid := config.get(CONF_EXIT_GRID)) is not None:
        cg.add(var.set_exit_grid(grid))
    if (grid := config.get(CONF_EDGE_GRID)) is not None:
        cg.add(var.set_edge_grid(grid))

    if zones := config.get(CONF_ZONES):
        cg.add(var.init_zones(len(zones)))
        for index, zone in enumerate(zones):
            presence = cg.nullptr
            motion = cg.nullptr
            if (presence_config := zone.get(CONF_PRESENCE)) is not None:
                presence = await binary_sensor_.new_binary_sensor(presence_config)
            if (motion_config := zone.get(CONF_MOTION)) is not None:
                motion = await binary_sensor_.new_binary_sensor(motion_config)
            cg.add(
                var.add_zone(
                    index + 1, zone[CONF_GRID], zone[CONF_SENSITIVITY], presence, motion
                )
            )
