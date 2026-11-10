# SPDX-License-Identifier: Apache-2.0
"""On-device scheduler for the ZoneFlow ESPHome package.

Programs are ordinary template entities declared in YAML (see
firmware/esphome/zoneflow/schedule.yaml); this component reads them once a
second and queues the program's zones on the sprinkler controller.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.components import binary_sensor, datetime, number, select, switch, text, text_sensor, time
from esphome.components.sprinkler import Sprinkler
from esphome.components.zoneflow import ZoneFlowComponent
from esphome.const import CONF_ID, CONF_NAME, CONF_TIME_ID

DEPENDENCIES = ["sprinkler", "time", "zoneflow"]

CONF_SPRINKLER_ID = "sprinkler_id"
CONF_ZONEFLOW_ID = "zoneflow_id"
CONF_MISSED_START_GRACE = "missed_start_grace"
CONF_RAIN_SENSOR = "rain_sensor"
CONF_RAIN_SENSOR_ENABLED = "rain_sensor_enabled"
CONF_RAIN_DELAY_NUMBER = "rain_delay_number"
CONF_STATUS = "status"
CONF_NEXT_RUN = "next_run"
CONF_PROGRAMS = "programs"
CONF_ENABLED_SWITCH = "enabled"
CONF_START_TIMES = "start_times"
CONF_START_ENABLED = "start_enabled"
CONF_DAYS = "days"
CONF_WEEKDAYS = "weekdays"
CONF_SCALE = "scale"
CONF_ZONES = "zones"
CONF_OBEY_RAIN = "obey_rain"
CONF_ZONE_ENABLE_SWITCHES = "zone_enable_switches"
CONF_PROGRAM = "program"
CONF_HOURS = "hours"

ns = cg.esphome_ns.namespace("zoneflow_schedule")
ZoneFlowSchedule = ns.class_("ZoneFlowSchedule", cg.Component)
Program = ns.struct("Program")
RunProgramAction = ns.class_("RunProgramAction", automation.Action)
SetRainDelayAction = ns.class_("SetRainDelayAction", automation.Action)

PROGRAM_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_NAME): cv.string_strict,
        cv.Required(CONF_ENABLED_SWITCH): cv.use_id(switch.Switch),
        cv.Required(CONF_START_TIMES): cv.All(cv.ensure_list(cv.use_id(datetime.TimeEntity)), cv.Length(min=1, max=4)),
        # One entry per start time, in the same order; omit or shorten to leave starts always used.
        cv.Optional(CONF_START_ENABLED, default=[]): cv.All(
            cv.ensure_list(cv.Any(cv.none, cv.use_id(switch.Switch))), cv.Length(max=4)
        ),
        cv.Required(CONF_DAYS): cv.use_id(select.Select),
        cv.Optional(CONF_WEEKDAYS): cv.All(cv.ensure_list(cv.use_id(switch.Switch)), cv.Length(min=7, max=7)),
        cv.Optional(CONF_SCALE): cv.use_id(number.Number),
        cv.Optional(CONF_ZONES): cv.use_id(text.Text),
        cv.Optional(CONF_OBEY_RAIN, default=True): cv.boolean,
    }
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(ZoneFlowSchedule),
        cv.Required(CONF_SPRINKLER_ID): cv.use_id(Sprinkler),
        cv.Required(CONF_TIME_ID): cv.use_id(time.RealTimeClock),
        cv.Required(CONF_ZONEFLOW_ID): cv.use_id(ZoneFlowComponent),
        # Standalone firmware default (firmware-architecture.md section 5).
        cv.Optional(CONF_MISSED_START_GRACE, default="60min"): cv.All(
            cv.positive_time_period_minutes, cv.Range(max=cv.TimePeriod(minutes=720))
        ),
        cv.Optional(CONF_RAIN_SENSOR): cv.use_id(binary_sensor.BinarySensor),
        cv.Optional(CONF_RAIN_SENSOR_ENABLED): cv.use_id(switch.Switch),
        cv.Optional(CONF_RAIN_DELAY_NUMBER): cv.use_id(number.Number),
        cv.Optional(CONF_STATUS): cv.use_id(text_sensor.TextSensor),
        cv.Optional(CONF_NEXT_RUN): cv.use_id(text_sensor.TextSensor),
        cv.Optional(CONF_ZONE_ENABLE_SWITCHES, default=[]): cv.All(
            cv.ensure_list(cv.use_id(switch.Switch)), cv.Length(max=8)
        ),
        cv.Required(CONF_PROGRAMS): cv.All(cv.ensure_list(PROGRAM_SCHEMA), cv.Length(min=1, max=8)),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_sprinkler(await cg.get_variable(config[CONF_SPRINKLER_ID])))
    cg.add(var.set_time(await cg.get_variable(config[CONF_TIME_ID])))
    cg.add(var.set_zoneflow(await cg.get_variable(config[CONF_ZONEFLOW_ID])))
    cg.add(var.set_grace_minutes(int(config[CONF_MISSED_START_GRACE].total_minutes)))
    if CONF_RAIN_SENSOR in config:
        cg.add(var.set_rain_sensor(await cg.get_variable(config[CONF_RAIN_SENSOR])))
    if CONF_RAIN_SENSOR_ENABLED in config:
        cg.add(var.set_rain_sensor_enabled(await cg.get_variable(config[CONF_RAIN_SENSOR_ENABLED])))
    if CONF_RAIN_DELAY_NUMBER in config:
        cg.add(var.set_rain_delay_number(await cg.get_variable(config[CONF_RAIN_DELAY_NUMBER])))
    if CONF_STATUS in config:
        cg.add(var.set_status_sensor(await cg.get_variable(config[CONF_STATUS])))
    if CONF_NEXT_RUN in config:
        cg.add(var.set_next_run_sensor(await cg.get_variable(config[CONF_NEXT_RUN])))
    for i, sw in enumerate(config[CONF_ZONE_ENABLE_SWITCHES]):
        cg.add(var.set_zone_enable_switch(i, await cg.get_variable(sw)))

    for prog in config[CONF_PROGRAMS]:
        cg.add(cg.RawStatement("{"))
        cg.add(cg.RawStatement("esphome::zoneflow_schedule::Program p;"))
        cg.add(cg.RawStatement(f"p.name = {cg.safe_exp(prog[CONF_NAME])};"))
        en = await cg.get_variable(prog[CONF_ENABLED_SWITCH])
        cg.add(cg.RawStatement(f"p.enabled = {en};"))
        for i, t in enumerate(prog[CONF_START_TIMES]):
            cg.add(cg.RawStatement(f"p.starts[{i}] = {await cg.get_variable(t)};"))
        for i, sw in enumerate(prog[CONF_START_ENABLED]):
            if sw is not None:
                cg.add(cg.RawStatement(f"p.start_enabled[{i}] = {await cg.get_variable(sw)};"))
        cg.add(cg.RawStatement(f"p.days = {await cg.get_variable(prog[CONF_DAYS])};"))
        for i, sw in enumerate(prog.get(CONF_WEEKDAYS, [])):
            cg.add(cg.RawStatement(f"p.weekdays[{i}] = {await cg.get_variable(sw)};"))
        if CONF_SCALE in prog:
            cg.add(cg.RawStatement(f"p.scale = {await cg.get_variable(prog[CONF_SCALE])};"))
        if CONF_ZONES in prog:
            cg.add(cg.RawStatement(f"p.zones = {await cg.get_variable(prog[CONF_ZONES])};"))
        cg.add(cg.RawStatement(f"p.obey_rain = {'true' if prog[CONF_OBEY_RAIN] else 'false'};"))
        cg.add(var.add_program(cg.MockObj("p")))
        cg.add(cg.RawStatement("}"))


@automation.register_action(
    "zoneflow_schedule.run_program",
    RunProgramAction,
    cv.Schema(
        {
            cv.GenerateID(): cv.use_id(ZoneFlowSchedule),
            cv.Required(CONF_PROGRAM): cv.templatable(cv.int_range(min=1, max=8)),
        }
    ),
    synchronous=True,
)
async def run_program_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    # 1-based, as the program numbers in the entity names.
    cg.add(var.set_program(await cg.templatable(config[CONF_PROGRAM], args, cg.uint8)))
    return var


@automation.register_action(
    "zoneflow_schedule.set_rain_delay",
    SetRainDelayAction,
    cv.Schema(
        {
            cv.GenerateID(): cv.use_id(ZoneFlowSchedule),
            cv.Required(CONF_HOURS): cv.templatable(cv.float_range(min=0, max=24 * 14)),
        }
    ),
    synchronous=True,
)
async def set_rain_delay_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    cg.add(var.set_hours(await cg.templatable(config[CONF_HOURS], args, cg.float_)))
    return var
