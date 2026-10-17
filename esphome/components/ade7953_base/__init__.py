from collections.abc import Callable

from esphome import pins
import esphome.codegen as cg
from esphome.components import binary_sensor, sensor
import esphome.config_validation as cv
from esphome.const import (
    CONF_FREQUENCY,
    CONF_IRQ_PIN,
    CONF_RESTORE,
    CONF_UPDATE_INTERVAL,
    CONF_VOLTAGE,
    CONF_VOLTAGE_GAIN,
    DEVICE_CLASS_APPARENT_POWER,
    DEVICE_CLASS_CURRENT,
    DEVICE_CLASS_ENERGY,
    DEVICE_CLASS_FREQUENCY,
    DEVICE_CLASS_POWER,
    DEVICE_CLASS_POWER_FACTOR,
    DEVICE_CLASS_REACTIVE_ENERGY,
    DEVICE_CLASS_REACTIVE_POWER,
    DEVICE_CLASS_VOLTAGE,
    STATE_CLASS_MEASUREMENT,
    STATE_CLASS_TOTAL_INCREASING,
    UNIT_AMPERE,
    UNIT_HERTZ,
    UNIT_PERCENT,
    UNIT_VOLT,
    UNIT_VOLT_AMPS,
    UNIT_VOLT_AMPS_HOURS,
    UNIT_VOLT_AMPS_REACTIVE,
    UNIT_VOLT_AMPS_REACTIVE_HOURS,
    UNIT_WATT,
    UNIT_WATT_HOURS,
)
from esphome.cpp_generator import Expression, MockObj
from esphome.types import ConfigType

CODEOWNERS = ["@angelnu"]
DOMAIN = "ade7953_base"

CONF_CURRENT_A = "current_a"
CONF_CURRENT_B = "current_b"
CONF_ACTIVE_POWER_A = "active_power_a"
CONF_ACTIVE_POWER_B = "active_power_b"
CONF_APPARENT_POWER_A = "apparent_power_a"
CONF_APPARENT_POWER_B = "apparent_power_b"
CONF_REACTIVE_POWER_A = "reactive_power_a"
CONF_REACTIVE_POWER_B = "reactive_power_b"
CONF_POWER_FACTOR_A = "power_factor_a"
CONF_POWER_FACTOR_B = "power_factor_b"
CONF_VOLTAGE_PGA_GAIN = "voltage_pga_gain"
CONF_CURRENT_PGA_GAIN_A = "current_pga_gain_a"
CONF_CURRENT_PGA_GAIN_B = "current_pga_gain_b"
CONF_CURRENT_GAIN_A = "current_gain_a"
CONF_CURRENT_GAIN_B = "current_gain_b"
CONF_ACTIVE_POWER_GAIN_A = "active_power_gain_a"
CONF_ACTIVE_POWER_GAIN_B = "active_power_gain_b"
CONF_USE_ACCUMULATED_ENERGY_REGISTERS = "use_accumulated_energy_registers"
CONF_ACTIVE_ENERGY_A = "active_energy_a"
CONF_ACTIVE_ENERGY_B = "active_energy_b"
CONF_REACTIVE_ENERGY_A = "reactive_energy_a"
CONF_REACTIVE_ENERGY_B = "reactive_energy_b"
CONF_APPARENT_ENERGY_A = "apparent_energy_a"
CONF_APPARENT_ENERGY_B = "apparent_energy_b"
CONF_NO_LOAD_THRESHOLD = "no_load_threshold"
CONF_INVERT_CURRENT_A = "invert_current_a"
CONF_INVERT_CURRENT_B = "invert_current_b"
CONF_ADE7953_ID = "ade7953_id"
CONF_NO_LOAD_A = "no_load_a"
CONF_NO_LOAD_B = "no_load_b"
CONF_REVERSE_POWER_A = "reverse_power_a"
CONF_REVERSE_POWER_B = "reverse_power_b"
ENERGY_SENSOR_KEYS = (
    CONF_ACTIVE_ENERGY_A,
    CONF_ACTIVE_ENERGY_B,
    CONF_REACTIVE_ENERGY_A,
    CONF_REACTIVE_ENERGY_B,
    CONF_APPARENT_ENERGY_A,
    CONF_APPARENT_ENERGY_B,
)
# The 24 bit energy registers overflow after about 354 Wh, which is 3 minutes at 7 kW
ENERGY_MAX_UPDATE_INTERVAL = cv.TimePeriod(seconds=180)
PGA_GAINS = {
    "1x": 0b000,
    "2x": 0b001,
    "4x": 0b010,
    "8x": 0b011,
    "16x": 0b100,
    "22x": 0b101,
}

ade7953_base_ns = cg.esphome_ns.namespace("ade7953_base")
ADE7953 = ade7953_base_ns.class_("ADE7953", cg.PollingComponent)
ADE7953EnergySensor = ade7953_base_ns.class_("ADE7953EnergySensor", sensor.Sensor)


def _energy_schema(unit: str, device_class: str = cv.UNDEFINED) -> cv.Schema:
    return sensor.sensor_schema(
        ADE7953EnergySensor,
        unit_of_measurement=unit,
        accuracy_decimals=1,
        device_class=device_class,
        state_class=STATE_CLASS_TOTAL_INCREASING,
    ).extend({cv.Optional(CONF_RESTORE, default=True): cv.boolean})


ADE7953_CONFIG_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_IRQ_PIN): pins.internal_gpio_input_pin_schema,
        cv.Optional(CONF_VOLTAGE): sensor.sensor_schema(
            unit_of_measurement=UNIT_VOLT,
            accuracy_decimals=1,
            device_class=DEVICE_CLASS_VOLTAGE,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_FREQUENCY): sensor.sensor_schema(
            unit_of_measurement=UNIT_HERTZ,
            accuracy_decimals=2,
            device_class=DEVICE_CLASS_FREQUENCY,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_CURRENT_A): sensor.sensor_schema(
            unit_of_measurement=UNIT_AMPERE,
            accuracy_decimals=2,
            device_class=DEVICE_CLASS_CURRENT,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_CURRENT_B): sensor.sensor_schema(
            unit_of_measurement=UNIT_AMPERE,
            accuracy_decimals=2,
            device_class=DEVICE_CLASS_CURRENT,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_ACTIVE_POWER_A): sensor.sensor_schema(
            unit_of_measurement=UNIT_WATT,
            accuracy_decimals=1,
            device_class=DEVICE_CLASS_POWER,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_ACTIVE_POWER_B): sensor.sensor_schema(
            unit_of_measurement=UNIT_WATT,
            accuracy_decimals=1,
            device_class=DEVICE_CLASS_POWER,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_APPARENT_POWER_A): sensor.sensor_schema(
            unit_of_measurement=UNIT_VOLT_AMPS,
            accuracy_decimals=1,
            device_class=DEVICE_CLASS_APPARENT_POWER,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_APPARENT_POWER_B): sensor.sensor_schema(
            unit_of_measurement=UNIT_VOLT_AMPS,
            accuracy_decimals=1,
            device_class=DEVICE_CLASS_APPARENT_POWER,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_REACTIVE_POWER_A): sensor.sensor_schema(
            unit_of_measurement=UNIT_VOLT_AMPS_REACTIVE,
            accuracy_decimals=1,
            device_class=DEVICE_CLASS_REACTIVE_POWER,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_REACTIVE_POWER_B): sensor.sensor_schema(
            unit_of_measurement=UNIT_VOLT_AMPS_REACTIVE,
            accuracy_decimals=1,
            device_class=DEVICE_CLASS_REACTIVE_POWER,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_POWER_FACTOR_A): sensor.sensor_schema(
            unit_of_measurement=UNIT_PERCENT,
            accuracy_decimals=2,
            device_class=DEVICE_CLASS_POWER_FACTOR,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_POWER_FACTOR_B): sensor.sensor_schema(
            unit_of_measurement=UNIT_PERCENT,
            accuracy_decimals=2,
            device_class=DEVICE_CLASS_POWER_FACTOR,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_ACTIVE_ENERGY_A): _energy_schema(
            UNIT_WATT_HOURS, DEVICE_CLASS_ENERGY
        ),
        cv.Optional(CONF_ACTIVE_ENERGY_B): _energy_schema(
            UNIT_WATT_HOURS, DEVICE_CLASS_ENERGY
        ),
        cv.Optional(CONF_REACTIVE_ENERGY_A): _energy_schema(
            UNIT_VOLT_AMPS_REACTIVE_HOURS, DEVICE_CLASS_REACTIVE_ENERGY
        ),
        cv.Optional(CONF_REACTIVE_ENERGY_B): _energy_schema(
            UNIT_VOLT_AMPS_REACTIVE_HOURS, DEVICE_CLASS_REACTIVE_ENERGY
        ),
        cv.Optional(CONF_APPARENT_ENERGY_A): _energy_schema(UNIT_VOLT_AMPS_HOURS),
        cv.Optional(CONF_APPARENT_ENERGY_B): _energy_schema(UNIT_VOLT_AMPS_HOURS),
        cv.Optional(
            CONF_VOLTAGE_PGA_GAIN,
            default="1x",
        ): cv.one_of(*PGA_GAINS, lower=True),
        cv.Optional(
            CONF_CURRENT_PGA_GAIN_A,
            default="1x",
        ): cv.one_of(*PGA_GAINS, lower=True),
        cv.Optional(
            CONF_CURRENT_PGA_GAIN_B,
            default="1x",
        ): cv.one_of(*PGA_GAINS, lower=True),
        cv.Optional(CONF_VOLTAGE_GAIN, default=0x400000): cv.hex_int_range(
            min=0x100000, max=0x800000
        ),
        cv.Optional(CONF_CURRENT_GAIN_A, default=0x400000): cv.hex_int_range(
            min=0x100000, max=0x800000
        ),
        cv.Optional(CONF_CURRENT_GAIN_B, default=0x400000): cv.hex_int_range(
            min=0x100000, max=0x800000
        ),
        cv.Optional(CONF_ACTIVE_POWER_GAIN_A, default=0x400000): cv.hex_int_range(
            min=0x100000, max=0x800000
        ),
        cv.Optional(CONF_ACTIVE_POWER_GAIN_B, default=0x400000): cv.hex_int_range(
            min=0x100000, max=0x800000
        ),
        cv.Optional(CONF_USE_ACCUMULATED_ENERGY_REGISTERS, default=False): cv.boolean,
        cv.Optional(CONF_NO_LOAD_THRESHOLD): cv.hex_int_range(min=0, max=0xFFFFFF),
        cv.Optional(CONF_INVERT_CURRENT_A, default=False): cv.boolean,
        cv.Optional(CONF_INVERT_CURRENT_B, default=False): cv.boolean,
    }
).extend(cv.polling_component_schema("60s"))


def validate_energy_update_interval(config: ConfigType) -> ConfigType:
    """Reject an update interval that lets the energy registers overflow between reads."""
    if config[CONF_UPDATE_INTERVAL] > ENERGY_MAX_UPDATE_INTERVAL and any(
        key in config for key in ENERGY_SENSOR_KEYS
    ):
        raise cv.Invalid(
            f"Energy sensors need an {CONF_UPDATE_INTERVAL} of at most "
            f"{ENERGY_MAX_UPDATE_INTERVAL}: the chip's energy registers overflow "
            "after about 354 Wh",
            path=[CONF_UPDATE_INTERVAL],
        )
    return config


ADE7953_BINARY_SENSOR_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_ADE7953_ID): cv.use_id(ADE7953),
        cv.Optional(CONF_NO_LOAD_A): binary_sensor.binary_sensor_schema(),
        cv.Optional(CONF_NO_LOAD_B): binary_sensor.binary_sensor_schema(),
        cv.Optional(CONF_REVERSE_POWER_A): binary_sensor.binary_sensor_schema(),
        cv.Optional(CONF_REVERSE_POWER_B): binary_sensor.binary_sensor_schema(),
    }
)


async def _energy_sensor(
    config: ConfigType, key: str, setter: Callable[[MockObj], Expression]
) -> None:
    if (conf := config.get(key)) is None:
        return
    sens = await sensor.new_sensor(conf, conf[CONF_RESTORE])
    cg.add(setter(sens))


async def register_ade7953(var: MockObj, config: ConfigType) -> None:
    await cg.register_component(var, config)

    if irq_pin_config := config.get(CONF_IRQ_PIN):
        irq_pin = await cg.gpio_pin_expression(irq_pin_config)
        cg.add(var.set_irq_pin(irq_pin))

    cg.add(var.set_pga_v(PGA_GAINS[config.get(CONF_VOLTAGE_PGA_GAIN)]))
    cg.add(var.set_pga_ia(PGA_GAINS[config.get(CONF_CURRENT_PGA_GAIN_A)]))
    cg.add(var.set_pga_ib(PGA_GAINS[config.get(CONF_CURRENT_PGA_GAIN_B)]))
    cg.add(var.set_vgain(config.get(CONF_VOLTAGE_GAIN)))
    cg.add(var.set_aigain(config.get(CONF_CURRENT_GAIN_A)))
    cg.add(var.set_bigain(config.get(CONF_CURRENT_GAIN_B)))
    cg.add(var.set_awgain(config.get(CONF_ACTIVE_POWER_GAIN_A)))
    cg.add(var.set_bwgain(config.get(CONF_ACTIVE_POWER_GAIN_B)))
    cg.add(
        var.set_use_acc_energy_regs(config.get(CONF_USE_ACCUMULATED_ENERGY_REGISTERS))
    )

    if (threshold := config.get(CONF_NO_LOAD_THRESHOLD)) is not None:
        cg.add(var.set_no_load_threshold(threshold))
    if config[CONF_INVERT_CURRENT_A]:
        cg.add(var.set_invert_current_a(True))
    if config[CONF_INVERT_CURRENT_B]:
        cg.add(var.set_invert_current_b(True))

    sensors = sensor.sub_sensors(config)
    await sensors(CONF_VOLTAGE, var.set_voltage_sensor)
    await sensors(CONF_FREQUENCY, var.set_frequency_sensor)
    await sensors(CONF_CURRENT_A, var.set_current_a_sensor)
    await sensors(CONF_CURRENT_B, var.set_current_b_sensor)
    await sensors(CONF_POWER_FACTOR_A, var.set_power_factor_a_sensor)
    await sensors(CONF_POWER_FACTOR_B, var.set_power_factor_b_sensor)
    await sensors(CONF_APPARENT_POWER_A, var.set_apparent_power_a_sensor)
    await sensors(CONF_APPARENT_POWER_B, var.set_apparent_power_b_sensor)
    await sensors(CONF_ACTIVE_POWER_A, var.set_active_power_a_sensor)
    await sensors(CONF_ACTIVE_POWER_B, var.set_active_power_b_sensor)
    await sensors(CONF_REACTIVE_POWER_A, var.set_reactive_power_a_sensor)
    await sensors(CONF_REACTIVE_POWER_B, var.set_reactive_power_b_sensor)

    await _energy_sensor(config, CONF_ACTIVE_ENERGY_A, var.set_active_energy_a_sensor)
    await _energy_sensor(config, CONF_ACTIVE_ENERGY_B, var.set_active_energy_b_sensor)
    await _energy_sensor(
        config, CONF_REACTIVE_ENERGY_A, var.set_reactive_energy_a_sensor
    )
    await _energy_sensor(
        config, CONF_REACTIVE_ENERGY_B, var.set_reactive_energy_b_sensor
    )
    await _energy_sensor(
        config, CONF_APPARENT_ENERGY_A, var.set_apparent_energy_a_sensor
    )
    await _energy_sensor(
        config, CONF_APPARENT_ENERGY_B, var.set_apparent_energy_b_sensor
    )


async def register_ade7953_binary_sensors(config: ConfigType) -> None:
    hub = await cg.get_variable(config[CONF_ADE7953_ID])
    sensors = binary_sensor.sub_binary_sensors(config)
    await sensors(CONF_NO_LOAD_A, hub.set_no_load_a_binary_sensor)
    await sensors(CONF_NO_LOAD_B, hub.set_no_load_b_binary_sensor)
    await sensors(CONF_REVERSE_POWER_A, hub.set_reverse_power_a_binary_sensor)
    await sensors(CONF_REVERSE_POWER_B, hub.set_reverse_power_b_binary_sensor)
