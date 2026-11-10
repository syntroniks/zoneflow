# SPDX-License-Identifier: Apache-2.0
"""A valve output as a switch, for the sprinkler component's valve_switch_id."""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch

from . import CONF_ZONE, CONF_ZONEFLOW_ID, MAX_ZONES, ZoneFlowComponent, zoneflow_ns

ZoneSwitch = zoneflow_ns.class_("ZoneSwitch", switch.Switch, cg.Component, cg.Parented.template(ZoneFlowComponent))

CONFIG_SCHEMA = (
    switch.switch_schema(ZoneSwitch, default_restore_mode="ALWAYS_OFF")
    .extend(
        {
            cv.GenerateID(CONF_ZONEFLOW_ID): cv.use_id(ZoneFlowComponent),
            cv.Required(CONF_ZONE): cv.int_range(min=1, max=MAX_ZONES),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = await switch.new_switch(config)
    await cg.register_component(var, config)
    await cg.register_parented(var, config[CONF_ZONEFLOW_ID])
    cg.add(var.set_zone(config[CONF_ZONE] - 1))
