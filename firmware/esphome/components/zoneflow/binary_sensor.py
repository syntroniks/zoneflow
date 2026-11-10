# SPDX-License-Identifier: Apache-2.0
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from esphome.const import (
    CONF_TYPE,
    DEVICE_CLASS_MOISTURE,
    DEVICE_CLASS_POWER,
    DEVICE_CLASS_PROBLEM,
    ENTITY_CATEGORY_DIAGNOSTIC,
)

from . import CONF_ZONE, CONF_ZONEFLOW_ID, MAX_ZONES, ZoneFlowComponent, zoneflow_ns

BinaryKind = zoneflow_ns.enum("BinaryKind", is_class=True)
TYPES = {
    "ac_present": (BinaryKind.AC_PRESENT, DEVICE_CLASS_POWER, ENTITY_CATEGORY_DIAGNOSTIC),
    "hardware_fault": (BinaryKind.HARDWARE_FAULT, DEVICE_CLASS_PROBLEM, ENTITY_CATEGORY_DIAGNOSTIC),
    "any_fault": (BinaryKind.ANY_FAULT, DEVICE_CLASS_PROBLEM, None),
    "leak": (BinaryKind.LEAK, DEVICE_CLASS_MOISTURE, None),
    "master_valve": (BinaryKind.MASTER_VALVE, None, None),
    "relay_closed": (BinaryKind.RELAY_CLOSED, None, ENTITY_CATEGORY_DIAGNOSTIC),
    "sensor_supply_fault": (BinaryKind.SENSOR_SUPPLY_FAULT, DEVICE_CLASS_PROBLEM, ENTITY_CATEGORY_DIAGNOSTIC),
    "current_sense_fault": (BinaryKind.CURRENT_SENSE_FAULT, DEVICE_CLASS_PROBLEM, ENTITY_CATEGORY_DIAGNOSTIC),
    "zone_fault": (BinaryKind.ZONE_FAULT, DEVICE_CLASS_PROBLEM, None),
    "zone_active": (BinaryKind.ZONE_ACTIVE, None, None),
}
ZONE_TYPES = {"zone_fault", "zone_active"}


def _schema(config):
    _, device_class, category = TYPES[config[CONF_TYPE]]
    kwargs = {}
    if device_class:
        kwargs["device_class"] = device_class
    if category:
        kwargs["entity_category"] = category
    schema = binary_sensor.binary_sensor_schema(**kwargs).extend(
        {
            cv.GenerateID(CONF_ZONEFLOW_ID): cv.use_id(ZoneFlowComponent),
            cv.Required(CONF_TYPE): cv.one_of(*TYPES, lower=True),
            (cv.Required if config[CONF_TYPE] in ZONE_TYPES else cv.Optional)(CONF_ZONE): cv.int_range(
                min=1, max=MAX_ZONES
            ),
        }
    )
    return schema(config)


CONFIG_SCHEMA = cv.All(
    cv.Schema({cv.Required(CONF_TYPE): cv.one_of(*TYPES, lower=True)}, extra=cv.ALLOW_EXTRA), _schema
)


async def to_code(config):
    var = await binary_sensor.new_binary_sensor(config)
    parent = await cg.get_variable(config[CONF_ZONEFLOW_ID])
    cg.add(parent.register_binary_sensor(var, TYPES[config[CONF_TYPE]][0], config.get(CONF_ZONE, 1) - 1))
