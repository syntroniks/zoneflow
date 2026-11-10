# SPDX-License-Identifier: Apache-2.0
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import button
from esphome.const import CONF_TYPE, ENTITY_CATEGORY_CONFIG, ENTITY_CATEGORY_DIAGNOSTIC

from . import CONF_ZONEFLOW_ID, ZoneFlowComponent, zoneflow_ns

ZoneFlowButton = zoneflow_ns.class_("ZoneFlowButton", button.Button, cg.Parented.template(ZoneFlowComponent))
ButtonKind = zoneflow_ns.enum("ButtonKind", is_class=True)
TYPES = {
    "clear_faults": (ButtonKind.CLEAR_FAULTS, ENTITY_CATEGORY_CONFIG),
    # Press twice within 10 s: a keep-alive detector fault needs the repair confirmed.
    "acknowledge_repair": (ButtonKind.ACKNOWLEDGE_REPAIR, ENTITY_CATEGORY_CONFIG),
    "stop_all": (ButtonKind.STOP_ALL, None),
    "self_test": (ButtonKind.SELF_TEST, ENTITY_CATEGORY_DIAGNOSTIC),
}


def _schema(config):
    _, category = TYPES[config[CONF_TYPE]]
    kwargs = {"entity_category": category} if category else {}
    return (
        button.button_schema(ZoneFlowButton, **kwargs).extend(
            {
                cv.GenerateID(CONF_ZONEFLOW_ID): cv.use_id(ZoneFlowComponent),
                cv.Required(CONF_TYPE): cv.one_of(*TYPES, lower=True),
            }
        )
    )(config)


CONFIG_SCHEMA = cv.All(
    cv.Schema({cv.Required(CONF_TYPE): cv.one_of(*TYPES, lower=True)}, extra=cv.ALLOW_EXTRA), _schema
)


async def to_code(config):
    var = await button.new_button(config)
    await cg.register_parented(var, config[CONF_ZONEFLOW_ID])
    cg.add(var.set_kind(TYPES[config[CONF_TYPE]][0]))
