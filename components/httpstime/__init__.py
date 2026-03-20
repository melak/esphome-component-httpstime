import esphome.codegen as cg
import esphome.config_validation as cv

from esphome.components.time import RealTimeClock, TIME_SCHEMA, register_time
from esphome.components.http_request import CONF_HTTP_REQUEST_ID, HttpRequestComponent
from esphome.const import CONF_ID, CONF_URL

DEPENDENCIES = ["http_request"]

httpstime_ns = cg.esphome_ns.namespace("httpstime")

HTTPSTimeComponent = httpstime_ns.class_(
    "HTTPSTimeComponent",
    RealTimeClock,
)

CONFIG_SCHEMA = TIME_SCHEMA.extend(
    {
        cv.GenerateID(): cv.declare_id(HTTPSTimeComponent),
        cv.GenerateID(CONF_HTTP_REQUEST_ID): cv.use_id(HttpRequestComponent),
        cv.Required(CONF_URL): cv.string,
    }
).extend(
    cv.polling_component_schema("12h")
)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])

    await cg.register_component(var, config)
    await register_time(var, config)

    await cg.register_parented(var, config[CONF_HTTP_REQUEST_ID])

    cg.add(var.set_url(config[CONF_URL]))
