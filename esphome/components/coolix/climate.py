import esphome.codegen as cg
from esphome.components import climate, climate_ir
import esphome.config_validation as cv
from esphome.const import CONF_SUPPORTED_FAN_MODES, CONF_SUPPORTED_SWING_MODES

AUTO_LOAD = ["climate_ir"]
CODEOWNERS = ["@glmnet"]

coolix_ns = cg.esphome_ns.namespace("coolix")
CoolixClimate = coolix_ns.class_("CoolixClimate", climate_ir.ClimateIR)

COOLIX_FAN_MODES = {
    key: climate.CLIMATE_FAN_MODES[key] for key in ("AUTO", "LOW", "MEDIUM", "HIGH")
}
COOLIX_SWING_MODES = {
    key: climate.CLIMATE_SWING_MODES[key] for key in ("OFF", "VERTICAL")
}

CONFIG_SCHEMA = climate_ir.climate_ir_with_receiver_schema(CoolixClimate).extend(
    {
        cv.Optional(
            CONF_SUPPORTED_FAN_MODES,
            default=["AUTO", "LOW", "MEDIUM", "HIGH"],
        ): cv.All(
            cv.ensure_list(cv.enum(COOLIX_FAN_MODES, upper=True)),
            cv.Length(min=1),
        ),
        cv.Optional(
            CONF_SUPPORTED_SWING_MODES, default=["OFF", "VERTICAL"]
        ): cv.ensure_list(cv.enum(COOLIX_SWING_MODES, upper=True)),
    }
)


async def to_code(config):
    var = await climate_ir.new_climate_ir(config)
    cg.add(var.set_supported_fan_modes(config[CONF_SUPPORTED_FAN_MODES]))
    cg.add(var.set_supported_swing_modes(config[CONF_SUPPORTED_SWING_MODES]))
