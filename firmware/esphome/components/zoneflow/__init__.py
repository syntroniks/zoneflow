# SPDX-License-Identifier: Apache-2.0
"""ZoneFlow rev A board: keep-alive supervision, valve outputs, current and faults.

The C++ logic shared with the standalone firmware (zf_core) is not copied here.
It is built from its one copy, firmware/zoneflow-fw/components/zf_core, as an
ESP-IDF component. ESPHome fetches the whole repository for a git source, so
that directory is always next to this one.
"""

from pathlib import Path

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation, pins
from esphome.components import esp32, i2c, sensor
from esphome.components.sprinkler import Sprinkler
from esphome.const import CONF_ID, CONF_THRESHOLD, CONF_TRIGGER_ID

CONF_ENABLED = "enabled"

CODEOWNERS = []
DEPENDENCIES = ["i2c", "esp32"]
AUTO_LOAD = []
MULTI_CONF = False

ZF_CORE = Path(__file__).resolve().parents[3] / "zoneflow-fw" / "components" / "zf_core"

MAX_ZONES = 8
HARD_MAX_RUN_S = 4 * 3600  # zf::kHardMaxRunSec

CONF_ZONEFLOW_ID = "zoneflow_id"
CONF_KEEPALIVE_PIN = "keepalive_pin"
CONF_DRIVE_READBACK_PIN = "drive_readback_pin"
CONF_RELAY_READBACK_PIN = "relay_readback_pin"
CONF_OVERCURRENT_PIN = "overcurrent_pin"
CONF_ZERO_CROSS_PIN = "zero_cross_pin"
CONF_SENSOR_SUPPLY_PIN = "sensor_supply_pin"
CONF_CURRENT_HIGH_PIN = "current_high_pin"
CONF_CURRENT_LOW_PIN = "current_low_pin"
CONF_EXPANDER_BITS = "expander_bits"
CONF_ZONE_BITS = "zones"
CONF_MASTER = "master"
CONF_RELAY = "relay"
CONF_VARIANT_8ZONE_N = "variant_8zone_n"
CONF_SENSOR_FAULT_N = "sensor_fault_n"
CONF_ZONE_COUNT = "zone_count"
CONF_ZONE_NAMES = "zone_names"
CONF_MAX_CONCURRENT = "max_concurrent_zones"
CONF_MASTER_VALVE = "master_valve"
CONF_PRE_OPEN = "pre_open"
CONF_POST_CLOSE = "post_close"
CONF_MIN_CURRENT = "min_current"
CONF_MAX_CURRENT = "max_current"
CONF_MAX_RUN_TIME = "max_run_time"
CONF_REST_TIME = "rest_time"
CONF_ZONE_OVERRIDES = "zone_overrides"
CONF_ZONE = "zone"
CONF_CURRENT_SENSE = "current_sense"
CONF_IDLE_THRESHOLD = "idle_threshold"
CONF_SHORT_CIRCUIT = "short_circuit"
CONF_SETTLE_TIME = "settle_time"
CONF_CONFIRM_SAMPLES = "confirm_samples"
CONF_FLOW_RATE_SENSOR = "flow_rate_sensor"
CONF_LEAK = "leak"
CONF_DELAY = "delay"
CONF_SELF_TEST_EVERY_RUN = "self_test_before_every_run"
CONF_SPRINKLER_ID = "sprinkler_id"
CONF_ON_FAULT = "on_fault"

zoneflow_ns = cg.esphome_ns.namespace("zoneflow")
ZoneFlowComponent = zoneflow_ns.class_("ZoneFlowComponent", cg.Component, i2c.I2CDevice)
ClearFaultsAction = zoneflow_ns.class_("ClearFaultsAction", automation.Action)
StopAllAction = zoneflow_ns.class_("StopAllAction", automation.Action)
SelfTestAction = zoneflow_ns.class_("SelfTestAction", automation.Action)
FaultTrigger = zoneflow_ns.class_("FaultTrigger", automation.Trigger.template(cg.std_string))


def expander_bit(value):
    """`port.bit`, as firmware/esphome/zoneflow/board-rev-a.yaml writes it."""
    value = cv.string_strict(value)
    try:
        port, bit = (int(x) for x in value.split("."))
    except ValueError as e:
        raise cv.Invalid(f"expander bit {value!r} is not port.bit") from e
    if port not in (0, 1) or not 0 <= bit <= 7:
        raise cv.Invalid(f"expander bit {value!r}: port 0 or 1, bit 0 to 7")
    return (port, bit)


def milliamps(value):
    return cv.int_range(min=0, max=6000)(value)


def max_run_time(value):
    value = cv.positive_time_period_seconds(value)
    if not 60 <= value.total_seconds <= HARD_MAX_RUN_S:
        raise cv.Invalid(f"max_run_time must be between 1 min and {HARD_MAX_RUN_S // 3600} h (zf::kHardMaxRunSec)")
    return value


def _validate_bits(config):
    bits = config[CONF_ZONE_BITS] + [
        config[k] for k in (CONF_MASTER, CONF_RELAY, CONF_VARIANT_8ZONE_N, CONF_SENSOR_FAULT_N)
    ]
    if len(set(bits)) != len(bits):
        raise cv.Invalid("expander bits must be distinct")
    return config


EXPANDER_BITS_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.Required(CONF_ZONE_BITS): cv.All(cv.ensure_list(expander_bit), cv.Length(min=MAX_ZONES, max=MAX_ZONES)),
            cv.Required(CONF_MASTER): expander_bit,
            cv.Required(CONF_RELAY): expander_bit,
            cv.Required(CONF_VARIANT_8ZONE_N): expander_bit,
            cv.Required(CONF_SENSOR_FAULT_N): expander_bit,
        }
    ),
    _validate_bits,
)

ZONE_OVERRIDE_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_ZONE): cv.int_range(min=1, max=MAX_ZONES),
        cv.Optional(CONF_ENABLED): cv.boolean,
        cv.Optional(CONF_MAX_RUN_TIME): max_run_time,
        cv.Optional(CONF_MIN_CURRENT): milliamps,
        cv.Optional(CONF_MAX_CURRENT): milliamps,
    }
)


def _validate(config):
    if len(config[CONF_ZONE_NAMES]) > config[CONF_ZONE_COUNT]:
        raise cv.Invalid(f"{len(config[CONF_ZONE_NAMES])} zone names for {config[CONF_ZONE_COUNT]} zones")
    for o in config[CONF_ZONE_OVERRIDES]:
        if o[CONF_ZONE] > config[CONF_ZONE_COUNT]:
            raise cv.Invalid(f"zone_overrides: zone {o[CONF_ZONE]} is above zone_count {config[CONF_ZONE_COUNT]}")
    if not ZF_CORE.is_dir():
        raise cv.Invalid(
            f"zf_core not found at {ZF_CORE}. The zoneflow component builds the shared code from the "
            "ZoneFlow repository; use it from a full checkout or a git source, not a copied folder."
        )
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(ZoneFlowComponent),
            cv.Required(CONF_KEEPALIVE_PIN): pins.internal_gpio_output_pin_number,
            cv.Required(CONF_DRIVE_READBACK_PIN): pins.internal_gpio_input_pin_number,
            cv.Required(CONF_RELAY_READBACK_PIN): pins.internal_gpio_input_pin_number,
            cv.Required(CONF_OVERCURRENT_PIN): pins.internal_gpio_input_pin_number,
            cv.Required(CONF_ZERO_CROSS_PIN): pins.internal_gpio_input_pin_number,
            cv.Required(CONF_CURRENT_HIGH_PIN): pins.internal_gpio_input_pin_number,
            cv.Required(CONF_CURRENT_LOW_PIN): pins.internal_gpio_input_pin_number,
            cv.Optional(CONF_SENSOR_SUPPLY_PIN): pins.internal_gpio_output_pin_number,
            cv.Required(CONF_EXPANDER_BITS): EXPANDER_BITS_SCHEMA,
            cv.Optional(CONF_ZONE_COUNT, default=MAX_ZONES): cv.int_range(min=1, max=MAX_ZONES),
            cv.Optional(CONF_ZONE_NAMES, default=[]): cv.ensure_list(cv.string_strict),
            # System spec section 1: up to 2 zones plus the master at once.
            cv.Optional(CONF_MAX_CONCURRENT, default=1): cv.int_range(min=1, max=2),
            cv.Optional(CONF_MASTER_VALVE, default={}): cv.Schema(
                {
                    cv.Optional(CONF_ENABLED, default=True): cv.boolean,
                    cv.Optional(CONF_PRE_OPEN, default="1s"): cv.positive_time_period_milliseconds,
                    cv.Optional(CONF_POST_CLOSE, default="2s"): cv.positive_time_period_milliseconds,
                    cv.Optional(CONF_MIN_CURRENT, default=80): milliamps,
                    cv.Optional(CONF_MAX_CURRENT, default=700): milliamps,
                }
            ),
            cv.Optional(CONF_MAX_RUN_TIME, default="60min"): max_run_time,
            # Spec section 2 rule 4: default 30 minutes.
            cv.Optional(CONF_REST_TIME, default="30min"): cv.All(
                cv.positive_time_period_seconds, cv.Range(min=cv.TimePeriod(minutes=1))
            ),
            # Hardware design 19.3 item 10: 80 to 700 mA per output.
            cv.Optional(CONF_MIN_CURRENT, default=80): milliamps,
            cv.Optional(CONF_MAX_CURRENT, default=700): milliamps,
            cv.Optional(CONF_ZONE_OVERRIDES, default=[]): cv.ensure_list(ZONE_OVERRIDE_SCHEMA),
            cv.Optional(CONF_CURRENT_SENSE, default={}): cv.Schema(
                {
                    cv.Optional(CONF_ENABLED, default=True): cv.boolean,
                    cv.Optional(CONF_IDLE_THRESHOLD, default=30): milliamps,  # hardware design 19.2
                    cv.Optional(CONF_SHORT_CIRCUIT, default=3000): milliamps,
                    cv.Optional(CONF_SETTLE_TIME, default="500ms"): cv.positive_time_period_milliseconds,
                    cv.Optional(CONF_CONFIRM_SAMPLES, default=3): cv.int_range(min=1, max=20),
                }
            ),
            cv.Optional(CONF_FLOW_RATE_SENSOR): cv.use_id(sensor.Sensor),
            cv.Optional(CONF_LEAK, default={}): cv.Schema(
                {
                    cv.Optional(CONF_THRESHOLD, default=0.5): cv.positive_float,
                    cv.Optional(CONF_DELAY, default="60s"): cv.positive_time_period_seconds,
                }
            ),
            cv.Optional(CONF_SELF_TEST_EVERY_RUN, default=True): cv.boolean,
            cv.Optional(CONF_SPRINKLER_ID): cv.use_id(Sprinkler),
            cv.Optional(CONF_ON_FAULT): automation.validate_automation(
                {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(FaultTrigger)}
            ),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(i2c.i2c_device_schema(0x74)),
    _validate,
)


async def to_code(config):
    # The shared library, from its one copy in the standalone firmware tree.
    esp32.add_idf_component(name="zf_core", path=str(ZF_CORE))
    esp32.include_builtin_idf_component("json")  # zf_core's config code links cJSON
    esp32.include_builtin_idf_component("esp_driver_gptimer")
    esp32.include_builtin_idf_component("esp_adc")
    # The keep-alive interrupt keeps running while the flash cache is off
    # (NVS writes, OTA), so a flash erase does not stop the edges.
    esp32.add_idf_sdkconfig_option("CONFIG_GPTIMER_ISR_CACHE_SAFE", True)
    # Hardware design 19.1 rule 7: the R8 module needs PSRAM ECC at the design
    # temperature. Has no effect unless PSRAM is enabled.
    esp32.add_idf_sdkconfig_option("CONFIG_SPIRAM_ECC_ENABLE", True)
    from esphome.components.ota import request_ota_state_listeners

    request_ota_state_listeners()

    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await i2c.register_i2c_device(var, config)

    pins_struct = cg.StructInitializer(
        zoneflow_ns.struct("BoardPins"),
        ("keepalive", config[CONF_KEEPALIVE_PIN]),
        ("drive_readback", config[CONF_DRIVE_READBACK_PIN]),
        ("relay_readback", config[CONF_RELAY_READBACK_PIN]),
        ("overcurrent", config[CONF_OVERCURRENT_PIN]),
        ("zero_cross", config[CONF_ZERO_CROSS_PIN]),
        ("sensor_supply", config.get(CONF_SENSOR_SUPPLY_PIN, -1)),
        ("current_high", config[CONF_CURRENT_HIGH_PIN]),
        ("current_low", config[CONF_CURRENT_LOW_PIN]),
    )
    cg.add(var.set_pins(pins_struct))

    bits = config[CONF_EXPANDER_BITS]
    layout = cg.MockObj("layout")
    cg.add(cg.RawStatement("{"))
    cg.add(cg.RawStatement("zf::hw::ExpanderLayout layout{};"))
    for i, b in enumerate(bits[CONF_ZONE_BITS]):
        cg.add(cg.RawStatement(f"layout.zone[{i}] = {{{b[0]}, {b[1]}}};"))
    for key, field in (
        (CONF_MASTER, "master"),
        (CONF_RELAY, "relay"),
        (CONF_VARIANT_8ZONE_N, "variant8ZoneN"),
        (CONF_SENSOR_FAULT_N, "sensorFaultN"),
    ):
        cg.add(cg.RawStatement(f"layout.{field} = {{{bits[key][0]}, {bits[key][1]}}};"))
    cg.add(var.set_layout(layout))

    zones = config[CONF_ZONE_COUNT]
    mv = config[CONF_MASTER_VALVE]
    cs = config[CONF_CURRENT_SENSE]
    lk = config[CONF_LEAK]
    cfg = var.config()
    stmts = [
        f"c.zone_count = {zones};",
        f"c.max_concurrent = {config[CONF_MAX_CONCURRENT]};",
        f"c.master_enabled = {'true' if mv[CONF_ENABLED] else 'false'};",
        f"c.master_pre_open_ms = {mv[CONF_PRE_OPEN].total_milliseconds};",
        f"c.master_post_close_ms = {mv[CONF_POST_CLOSE].total_milliseconds};",
        f"c.master_min_ma = {mv[CONF_MIN_CURRENT]};",
        f"c.master_max_ma = {mv[CONF_MAX_CURRENT]};",
        f"c.min_rest_sec = {config[CONF_REST_TIME].total_seconds};",
        f"c.current_enabled = {'true' if cs[CONF_ENABLED] else 'false'};",
        f"c.idle_threshold_ma = {cs[CONF_IDLE_THRESHOLD]};",
        f"c.short_circuit_ma = {cs[CONF_SHORT_CIRCUIT]};",
        f"c.settle_ms = {cs[CONF_SETTLE_TIME].total_milliseconds};",
        f"c.confirm_samples = {cs[CONF_CONFIRM_SAMPLES]};",
        f"c.leak_threshold_lpm = {float(lk[CONF_THRESHOLD])}f;",
        f"c.leak_detect_sec = {lk[CONF_DELAY].total_seconds};",
        f"c.stage.pinHighTestEveryRun = {'true' if config[CONF_SELF_TEST_EVERY_RUN] else 'false'};",
    ]
    overrides = {o[CONF_ZONE] - 1: o for o in config[CONF_ZONE_OVERRIDES]}
    for z in range(MAX_ZONES):
        o = overrides.get(z, {})
        enabled = z < zones and o.get(CONF_ENABLED, True)
        stmts.append(
            f"c.zones[{z}] = zfe::ZoneSettings{{{'true' if enabled else 'false'}, "
            f"{o.get(CONF_MAX_RUN_TIME, config[CONF_MAX_RUN_TIME]).total_seconds}, "
            f"{o.get(CONF_MIN_CURRENT, config[CONF_MIN_CURRENT])}, "
            f"{o.get(CONF_MAX_CURRENT, config[CONF_MAX_CURRENT])}}};"
        )
    cg.add(cg.RawStatement(f"auto &c = {cfg};"))
    for s in stmts:
        cg.add(cg.RawStatement(s))
    cg.add(cg.RawStatement("}"))

    for i, name in enumerate(config[CONF_ZONE_NAMES]):
        cg.add(var.set_zone_name(i, name))
    if CONF_FLOW_RATE_SENSOR in config:
        cg.add(var.set_flow_rate_sensor(await cg.get_variable(config[CONF_FLOW_RATE_SENSOR])))
    if CONF_SPRINKLER_ID in config:
        cg.add_define("USE_ZONEFLOW_SPRINKLER")
        cg.add(var.set_sprinkler(await cg.get_variable(config[CONF_SPRINKLER_ID])))
    for conf in config.get(CONF_ON_FAULT, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [(cg.std_string, "fault")], conf)


ACTION_SCHEMA = automation.maybe_simple_id({cv.GenerateID(): cv.use_id(ZoneFlowComponent)})


@automation.register_action("zoneflow.clear_faults", ClearFaultsAction, ACTION_SCHEMA, synchronous=True)
@automation.register_action("zoneflow.stop_all", StopAllAction, ACTION_SCHEMA, synchronous=True)
@automation.register_action("zoneflow.self_test", SelfTestAction, ACTION_SCHEMA, synchronous=True)
async def zoneflow_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var
