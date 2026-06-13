import esphome.codegen as cg
import esphome.config_validation as cv

CODEOWNERS = ["@BartdeJonge"]

chihiros_ns = cg.esphome_ns.namespace("chihiros")

CONFIG_SCHEMA = cv.Schema({})

async def to_code(config):
    cg.add_global(cg.RawStatement('#include "esphome/components/chihiros_ble/chihiros_ble.h"'))
    cg.add_global(cg.RawStatement('#include "esphome/components/chihiros_ble/chihiros_devices.h"'))
