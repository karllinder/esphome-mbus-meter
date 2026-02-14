import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import (
    CONF_ID,
    CONF_POWER,
    CONF_ENERGY,
    DEVICE_CLASS_CURRENT,
    DEVICE_CLASS_ENERGY,
    DEVICE_CLASS_POWER,
    DEVICE_CLASS_VOLTAGE,
    STATE_CLASS_MEASUREMENT,
    STATE_CLASS_TOTAL_INCREASING,
    UNIT_AMPERE,
    UNIT_VOLT,
    UNIT_WATT,
    UNIT_WATT_HOURS,
)

from . import MbusMeter, mbus_meter_ns

DEPENDENCIES = ["mbus_meter"]

CONF_CURRENT_L1 = "current_l1"
CONF_CURRENT_L2 = "current_l2"
CONF_CURRENT_L3 = "current_l3"
CONF_VOLTAGE_L1 = "voltage_l1"
CONF_VOLTAGE_L2 = "voltage_l2"
CONF_VOLTAGE_L3 = "voltage_l3"
CONF_REACTIVE_POWER = "reactive_power"
CONF_REACTIVE_ENERGY = "reactive_energy"
CONF_REACTIVE_EXPORT_ENERGY = "reactive_export_energy"
CONF_POWER_2A_FRAME = "power_2a_frame"
CONF_2A_FRAME_OWN_SENSOR = "2a_frame_own_sensor"

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.use_id(MbusMeter),
            cv.Optional(CONF_POWER): sensor.sensor_schema(
            unit_of_measurement=UNIT_WATT,
            accuracy_decimals=0,
            device_class=DEVICE_CLASS_POWER,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_CURRENT_L1): sensor.sensor_schema(
            unit_of_measurement=UNIT_AMPERE,
            accuracy_decimals=0,
            device_class=DEVICE_CLASS_CURRENT,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_CURRENT_L2): sensor.sensor_schema(
            unit_of_measurement=UNIT_AMPERE,
            accuracy_decimals=0,
            device_class=DEVICE_CLASS_CURRENT,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_CURRENT_L3): sensor.sensor_schema(
            unit_of_measurement=UNIT_AMPERE,
            accuracy_decimals=0,
            device_class=DEVICE_CLASS_CURRENT,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_VOLTAGE_L1): sensor.sensor_schema(
            unit_of_measurement=UNIT_VOLT,
            accuracy_decimals=0,
            device_class=DEVICE_CLASS_VOLTAGE,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_VOLTAGE_L2): sensor.sensor_schema(
            unit_of_measurement=UNIT_VOLT,
            accuracy_decimals=0,
            device_class=DEVICE_CLASS_VOLTAGE,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_VOLTAGE_L3): sensor.sensor_schema(
            unit_of_measurement=UNIT_VOLT,
            accuracy_decimals=0,
            device_class=DEVICE_CLASS_VOLTAGE,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_ENERGY): sensor.sensor_schema(
            unit_of_measurement=UNIT_WATT_HOURS,
            accuracy_decimals=0,
            device_class=DEVICE_CLASS_ENERGY,
            state_class=STATE_CLASS_TOTAL_INCREASING,
        ),
        cv.Optional(CONF_REACTIVE_POWER): sensor.sensor_schema(
            unit_of_measurement="var",
            accuracy_decimals=0,
            device_class=DEVICE_CLASS_POWER,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_REACTIVE_ENERGY): sensor.sensor_schema(
            unit_of_measurement="varh",
            accuracy_decimals=0,
            device_class=DEVICE_CLASS_ENERGY,
            state_class=STATE_CLASS_TOTAL_INCREASING,
        ),
        cv.Optional(CONF_REACTIVE_EXPORT_ENERGY): sensor.sensor_schema(
            unit_of_measurement="varh",
            accuracy_decimals=0,
            device_class=DEVICE_CLASS_ENERGY,
            state_class=STATE_CLASS_TOTAL_INCREASING,
        ),
        cv.Optional(CONF_POWER_2A_FRAME): sensor.sensor_schema(
            unit_of_measurement=UNIT_WATT,
            accuracy_decimals=0,
            device_class=DEVICE_CLASS_POWER,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_2A_FRAME_OWN_SENSOR, default=False): cv.boolean,
    }
    )
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_ID])

    if CONF_POWER in config:
        sens = await sensor.new_sensor(config[CONF_POWER])
        cg.add(parent.set_power_sensor(sens))

    if CONF_CURRENT_L1 in config:
        sens = await sensor.new_sensor(config[CONF_CURRENT_L1])
        cg.add(parent.set_current_l1_sensor(sens))

    if CONF_CURRENT_L2 in config:
        sens = await sensor.new_sensor(config[CONF_CURRENT_L2])
        cg.add(parent.set_current_l2_sensor(sens))

    if CONF_CURRENT_L3 in config:
        sens = await sensor.new_sensor(config[CONF_CURRENT_L3])
        cg.add(parent.set_current_l3_sensor(sens))

    if CONF_VOLTAGE_L1 in config:
        sens = await sensor.new_sensor(config[CONF_VOLTAGE_L1])
        cg.add(parent.set_voltage_l1_sensor(sens))

    if CONF_VOLTAGE_L2 in config:
        sens = await sensor.new_sensor(config[CONF_VOLTAGE_L2])
        cg.add(parent.set_voltage_l2_sensor(sens))

    if CONF_VOLTAGE_L3 in config:
        sens = await sensor.new_sensor(config[CONF_VOLTAGE_L3])
        cg.add(parent.set_voltage_l3_sensor(sens))

    if CONF_ENERGY in config:
        sens = await sensor.new_sensor(config[CONF_ENERGY])
        cg.add(parent.set_energy_sensor(sens))

    if CONF_REACTIVE_POWER in config:
        sens = await sensor.new_sensor(config[CONF_REACTIVE_POWER])
        cg.add(parent.set_reactive_power_sensor(sens))

    if CONF_REACTIVE_ENERGY in config:
        sens = await sensor.new_sensor(config[CONF_REACTIVE_ENERGY])
        cg.add(parent.set_reactive_energy_sensor(sens))

    if CONF_REACTIVE_EXPORT_ENERGY in config:
        sens = await sensor.new_sensor(config[CONF_REACTIVE_EXPORT_ENERGY])
        cg.add(parent.set_reactive_export_energy_sensor(sens))

    if CONF_POWER_2A_FRAME in config:
        sens = await sensor.new_sensor(config[CONF_POWER_2A_FRAME])
        cg.add(parent.set_power_2a_frame_sensor(sens))

    cg.add(parent.set_use_2a_frame_own_sensor(config[CONF_2A_FRAME_OWN_SENSOR]))