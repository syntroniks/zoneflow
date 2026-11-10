# SPDX-License-Identifier: Apache-2.0
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import (
    CONF_TYPE,
    DEVICE_CLASS_CURRENT,
    DEVICE_CLASS_DURATION,
    DEVICE_CLASS_FREQUENCY,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_MEASUREMENT,
    STATE_CLASS_TOTAL_INCREASING,
    UNIT_HERTZ,
    UNIT_MILLIAMP,
    UNIT_SECOND,
)

from . import CONF_ZONE, CONF_ZONEFLOW_ID, MAX_ZONES, ZoneFlowComponent, zoneflow_ns

SensorKind = zoneflow_ns.enum("SensorKind", is_class=True)
TYPES = {
    "valve_current": (SensorKind.VALVE_CURRENT, UNIT_MILLIAMP, DEVICE_CLASS_CURRENT, STATE_CLASS_MEASUREMENT, None, 0),
    "zone_current": (SensorKind.ZONE_CURRENT, UNIT_MILLIAMP, DEVICE_CLASS_CURRENT, STATE_CLASS_MEASUREMENT, None, 0),
    "noise_floor": (
        SensorKind.NOISE_FLOOR,
        UNIT_MILLIAMP,
        DEVICE_CLASS_CURRENT,
        STATE_CLASS_MEASUREMENT,
        ENTITY_CATEGORY_DIAGNOSTIC,
        0,
    ),
    "mains_frequency": (
        SensorKind.MAINS_FREQUENCY,
        UNIT_HERTZ,
        DEVICE_CLASS_FREQUENCY,
        STATE_CLASS_MEASUREMENT,
        ENTITY_CATEGORY_DIAGNOSTIC,
        1,
    ),
    "self_tests_passed": (
        SensorKind.SELF_TESTS_PASSED,
        "",
        None,
        STATE_CLASS_TOTAL_INCREASING,
        ENTITY_CATEGORY_DIAGNOSTIC,
        0,
    ),
    "zone_remaining": (SensorKind.ZONE_REMAINING, UNIT_SECOND, DEVICE_CLASS_DURATION, None, None, 0),
}
ZONE_TYPES = {"zone_current", "zone_remaining"}


def _schema(config):
    _, unit, device_class, state_class, category, decimals = TYPES[config[CONF_TYPE]]
    kwargs = {"accuracy_decimals": decimals}
    if unit:
        kwargs["unit_of_measurement"] = unit
    if device_class:
        kwargs["device_class"] = device_class
    if state_class:
        kwargs["state_class"] = state_class
    if category:
        kwargs["entity_category"] = category
    schema = sensor.sensor_schema(**kwargs).extend(
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
    var = await sensor.new_sensor(config)
    parent = await cg.get_variable(config[CONF_ZONEFLOW_ID])
    cg.add(parent.register_sensor(var, TYPES[config[CONF_TYPE]][0], config.get(CONF_ZONE, 1) - 1))
