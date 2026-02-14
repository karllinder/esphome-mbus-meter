import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text_sensor
from esphome.const import CONF_ID

from . import MbusMeter, mbus_meter_ns

DEPENDENCIES = ["mbus_meter"]

CONF_OBIS_VERSION = "obis_version"
CONF_METER_ID = "meter_id"
CONF_METER_TYPE = "meter_type"

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.use_id(MbusMeter),
            cv.Optional(CONF_OBIS_VERSION): text_sensor.text_sensor_schema(),
            cv.Optional(CONF_METER_ID): text_sensor.text_sensor_schema(),
            cv.Optional(CONF_METER_TYPE): text_sensor.text_sensor_schema(),
        }
    )
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_ID])

    if CONF_OBIS_VERSION in config:
        sens = await text_sensor.new_text_sensor(config[CONF_OBIS_VERSION])
        cg.add(parent.set_obis_version_text_sensor(sens))

    if CONF_METER_ID in config:
        sens = await text_sensor.new_text_sensor(config[CONF_METER_ID])
        cg.add(parent.set_meter_id_text_sensor(sens))

    if CONF_METER_TYPE in config:
        sens = await text_sensor.new_text_sensor(config[CONF_METER_TYPE])
        cg.add(parent.set_meter_type_text_sensor(sens))