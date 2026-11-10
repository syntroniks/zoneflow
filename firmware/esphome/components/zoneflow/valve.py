# SPDX-License-Identifier: Apache-2.0
"""A zone as a Home Assistant valve entity."""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import valve
from esphome.components.sprinkler import Sprinkler

from . import CONF_SPRINKLER_ID, CONF_ZONE, CONF_ZONEFLOW_ID, MAX_ZONES, ZoneFlowComponent, zoneflow_ns

ZoneValve = zoneflow_ns.class_("ZoneValve", valve.Valve, cg.Component, cg.Parented.template(ZoneFlowComponent))

CONFIG_SCHEMA = (
    valve.valve_schema(ZoneValve, device_class="water")
    .extend(
        {
            cv.GenerateID(CONF_ZONEFLOW_ID): cv.use_id(ZoneFlowComponent),
            cv.Required(CONF_ZONE): cv.int_range(min=1, max=MAX_ZONES),
            cv.Optional(CONF_SPRINKLER_ID): cv.use_id(Sprinkler),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = await valve.new_valve(config)
    await cg.register_component(var, config)
    await cg.register_parented(var, config[CONF_ZONEFLOW_ID])
    cg.add(var.set_zone(config[CONF_ZONE] - 1))
    if CONF_SPRINKLER_ID in config:
        cg.add_define("USE_ZONEFLOW_SPRINKLER")
        cg.add(var.set_sprinkler(await cg.get_variable(config[CONF_SPRINKLER_ID])))
