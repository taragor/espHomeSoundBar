"""ESPHome component that programs a generated EDID into the 24C02 EEPROM on the HDMI DDC lines.

The EEPROM is programmed once, on demand (button), while it is connected to the ESP through
the programming jumpers. Optional entities report whether the EEPROM is connected, whether it
holds the current EDID and a short status text.
"""

import re

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor, button, i2c, text_sensor
from esphome.const import (
    CONF_ID,
    DEVICE_CLASS_CONNECTIVITY,
    ENTITY_CATEGORY_DIAGNOSTIC,
)

CODEOWNERS = []
DEPENDENCIES = ["i2c"]
AUTO_LOAD = ["binary_sensor", "text_sensor", "button"]

hdmi_edid_ns = cg.esphome_ns.namespace("hdmi_edid")
HdmiEdid = hdmi_edid_ns.class_("HdmiEdid", cg.Component, i2c.I2CDevice)
ProgramButton = hdmi_edid_ns.class_("ProgramButton", button.Button, cg.Parented.template(HdmiEdid))

CONF_PHYSICAL_ADDRESS = "physical_address"
CONF_MONITOR_NAME = "monitor_name"
CONF_MANUFACTURER = "manufacturer"
CONF_CHECK_INTERVAL = "check_interval"
CONF_CONNECTED = "connected"
CONF_PROGRAMMED = "programmed"
CONF_STATUS = "status"
CONF_PROGRAM_BUTTON = "program_button"


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
            # How often to look for the EEPROM on the bus.
            cv.Optional(CONF_CHECK_INTERVAL, default="5s"): cv.positive_time_period_milliseconds,
            # An EEPROM answers on the bus (jumpers in programming mode).
            cv.Optional(CONF_CONNECTED): binary_sensor.binary_sensor_schema(
                device_class=DEVICE_CLASS_CONNECTIVITY,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
                icon="mdi:chip",
            ),
            # The EEPROM holds the current EDID (remembered while it is disconnected).
            cv.Optional(CONF_PROGRAMMED): binary_sensor.binary_sensor_schema(
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
                icon="mdi:check-decagram",
            ),
            cv.Optional(CONF_STATUS): text_sensor.text_sensor_schema(
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
                icon="mdi:information-outline",
            ),
            # Writes the EDID into the EEPROM.
            cv.Optional(CONF_PROGRAM_BUTTON): button.button_schema(
                ProgramButton,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
                icon="mdi:content-save-cog",
            ),
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
    cg.add(var.set_check_interval(config[CONF_CHECK_INTERVAL]))

    if conf := config.get(CONF_CONNECTED):
        sens = await binary_sensor.new_binary_sensor(conf)
        cg.add(var.set_connected_sensor(sens))
    if conf := config.get(CONF_PROGRAMMED):
        sens = await binary_sensor.new_binary_sensor(conf)
        cg.add(var.set_programmed_sensor(sens))
    if conf := config.get(CONF_STATUS):
        sens = await text_sensor.new_text_sensor(conf)
        cg.add(var.set_status_sensor(sens))
    if conf := config.get(CONF_PROGRAM_BUTTON):
        btn = await button.new_button(conf)
        await cg.register_parented(btn, var)
