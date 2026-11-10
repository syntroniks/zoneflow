# SPDX-License-Identifier: Apache-2.0
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text_sensor
from esphome.const import CONF_TYPE, ENTITY_CATEGORY_DIAGNOSTIC

from . import CONF_ZONE, CONF_ZONEFLOW_ID, MAX_ZONES, ZoneFlowComponent, zoneflow_ns

TextKind = zoneflow_ns.enum("TextKind", is_class=True)
TYPES = {
    "output_stage": (TextKind.OUTPUT_STAGE, ENTITY_CATEGORY_DIAGNOSTIC),
    "faults": (TextKind.FAULTS, None),
    "zone_status": (TextKind.ZONE_STATUS, None),
}


def _schema(config):
    _, category = TYPES[config[CONF_TYPE]]
    kwargs = {"entity_category": category} if category else {}
    schema = text_sensor.text_sensor_schema(**kwargs).extend(
        {
            cv.GenerateID(CONF_ZONEFLOW_ID): cv.use_id(ZoneFlowComponent),
            cv.Required(CONF_TYPE): cv.one_of(*TYPES, lower=True),
            (cv.Required if config[CONF_TYPE] == "zone_status" else cv.Optional)(CONF_ZONE): cv.int_range(
                min=1, max=MAX_ZONES
            ),
        }
    )
    return schema(config)


CONFIG_SCHEMA = cv.All(
    cv.Schema({cv.Required(CONF_TYPE): cv.one_of(*TYPES, lower=True)}, extra=cv.ALLOW_EXTRA), _schema
)


async def to_code(config):
    var = await text_sensor.new_text_sensor(config)
    parent = await cg.get_variable(config[CONF_ZONEFLOW_ID])
    cg.add(parent.register_text_sensor(var, TYPES[config[CONF_TYPE]][0], config.get(CONF_ZONE, 1) - 1))
