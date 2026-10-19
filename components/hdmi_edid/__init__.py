"""ESPHome component that keeps a generated EDID in the 24C02 EEPROM on the HDMI DDC lines."""

import re

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import i2c
from esphome.const import CONF_ID

CODEOWNERS = []
DEPENDENCIES = ["i2c"]

hdmi_edid_ns = cg.esphome_ns.namespace("hdmi_edid")
HdmiEdid = hdmi_edid_ns.class_("HdmiEdid", cg.Component, i2c.I2CDevice)

CONF_PHYSICAL_ADDRESS = "physical_address"
CONF_MONITOR_NAME = "monitor_name"
CONF_MANUFACTURER = "manufacturer"
CONF_FORCE_WRITE = "force_write"


def _monitor_name(value):
    value = cv.string_strict(value)
    if not 1 <= len(value) <= 13 or not all(0x20 <= ord(c) <= 0x7E for c in value):
        raise cv.Invalid("monitor_name must be 1-13 printable ASCII characters")
    return value


def _manufacturer(value):
    value = cv.string_strict(value)
    if not re.fullmatch(r"[A-Z]{3}", value):
        raise cv.Invalid("manufacturer must be three capital letters (PNP ID)")
    return value


CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(HdmiEdid),
            # Physical address the SOUNDBAR should adopt, e.g. 0x1000 = 1.0.0.0
            cv.Optional(CONF_PHYSICAL_ADDRESS, default=0x1000): cv.hex_uint16_t,
            cv.Optional(CONF_MONITOR_NAME, default="ESP ARC"): _monitor_name,
            cv.Optional(CONF_MANUFACTURER, default="ESP"): _manufacturer,
            cv.Optional(CONF_FORCE_WRITE, default=False): cv.boolean,
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(i2c.i2c_device_schema(0x50))
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await i2c.register_i2c_device(var, config)
    cg.add(var.set_physical_address(config[CONF_PHYSICAL_ADDRESS]))
    cg.add(var.set_monitor_name(config[CONF_MONITOR_NAME]))
    cg.add(var.set_manufacturer(config[CONF_MANUFACTURER]))
    cg.add(var.set_force_write(config[CONF_FORCE_WRITE]))
