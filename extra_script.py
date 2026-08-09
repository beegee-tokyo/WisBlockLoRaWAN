"""
extra_script.py - PlatformIO build hook for WisBlockLoRaWAN.

Does two things LBM's own Makefile normally handles for you (see
src/lbm/lbm_lib/makefiles/*.mk in the upstream SWL2001 repo), neither of
which PlatformIO's plain compiler invocation does automatically:

1. CPPPATH - adds every nested src/lbm/... subdirectory to the include
   search path. PlatformIO's Library Dependency Finder does not reliably
   discover deeply-nested header directories the way Arduino IDE's
   recursive `src/` scanning does.

2. CPPDEFINES - LBM's source is written to be built with a specific set of
   -D flags selecting the radio, LoRaWAN Regional Parameters version, stack
   count, and which optional features are compiled in. Without them you get
   errors like `#error "RP_VERSION ... must be defined"` or
   `'NUMBER_OF_STACKS' undeclared`.

Both run automatically - PlatformIO executes any script named in a
library's library.json `build.extraScript` field during that library's own
build step, with `env` bound to its compile environment.

NOTE: this script is run by SCons via exec(), not imported as a normal
Python module, so `__file__` is NOT defined here. SCons already establishes
this script's own directory as the context for relative paths passed to
env.Dir()/CPPPATH, so plain relative paths below resolve correctly with no
need to compute an absolute base path.

IMPORTANT: this library only vendors the subset of LBM actually needed for
LoRaWAN Class A/B/C + Relay (TX and RX/serving) + LoRa P2P on SX1262 - the
scope of the original request. Deliberately NOT vendored/enabled: FUOTA
(fragmented data block transport, firmware management, multi-package
access), multicast, application layer clock sync, cloud device management,
LFU (log file upload), store-and-forward, the beacon-TX *test/demo* service
(smtc_modem_core/modem_services/beacon_tx_service - a simulated beacon
transmitter for testing Class B without a real gateway, not needed for
normal Class B operation), almanac packages and geolocation services
(LR11xx-only). If you need any of these later: check that feature's guard
macro in src/lbm/smtc_modem_core/modem_utilities/modem_services_config.h,
re-vendor its source directory from upstream SWL2001, add it to
include_dirs below, and add its -D flag to defines below.

If you re-vendor a different LBM version and the directory layout changes,
regenerate the include list with (run from the library root):

    find src/lbm -name "*.h" -exec dirname {} \\; | sort -u
"""

Import("env")

include_dirs = [
    "src/lbm",  # lora_basics_modem_version.h lives directly here, not in a subdirectory
    "src/lbm/smtc_modem_api",
    "src/lbm/smtc_modem_core",
    "src/lbm/smtc_modem_core/logging",
    "src/lbm/smtc_modem_core/lorawan_api",
    "src/lbm/smtc_modem_core/lorawan_manager",
    "src/lbm/smtc_modem_core/lorawan_packages/lorawan_certification",
    "src/lbm/smtc_modem_core/lr1mac",
    "src/lbm/smtc_modem_core/lr1mac/src",
    "src/lbm/smtc_modem_core/lr1mac/src/lr1mac_class_b",
    "src/lbm/smtc_modem_core/lr1mac/src/lr1mac_class_c",
    "src/lbm/smtc_modem_core/lr1mac/src/relay/common",
    "src/lbm/smtc_modem_core/lr1mac/src/relay/relay_rx",
    "src/lbm/smtc_modem_core/lr1mac/src/relay/relay_tx",
    "src/lbm/smtc_modem_core/lr1mac/src/services",
    "src/lbm/smtc_modem_core/lr1mac/src/smtc_real/src",
    "src/lbm/smtc_modem_core/modem_services",
    "src/lbm/smtc_modem_core/modem_services/relay_service",
    "src/lbm/smtc_modem_core/modem_supervisor",
    "src/lbm/smtc_modem_core/modem_utilities",
    "src/lbm/smtc_modem_core/radio_drivers/sx126x_driver/src",
    "src/lbm/smtc_modem_core/radio_planner/src",
    "src/lbm/smtc_modem_core/smtc_modem_crypto",
    "src/lbm/smtc_modem_core/smtc_modem_crypto/smtc_secure_element",
    "src/lbm/smtc_modem_core/smtc_modem_crypto/soft_secure_element",
    "src/lbm/smtc_modem_core/smtc_ral/src",
    "src/lbm/smtc_modem_core/smtc_ralf/src",
    "src/lbm/smtc_modem_hal",
]

env.Append(CPPPATH=[env.Dir(d) for d in include_dirs])

# --- Required build-time configuration (normally set by LBM's own Makefile) ---
defines = [
    # Single-stack device (this library doesn't support LBM's multi-stack mode).
    ("NUMBER_OF_STACKS", 1),
    # LoRaWAN Regional Parameters version. RP2_103 is LBM's own default
    # (see makefiles/regions.mk: "ifndef RP_VERSION -> -DRP2_103"). Use
    # RP2_101 instead only if your network server specifically requires it.
    "RP2_103",
    # Radio selection - all three boards use the SX1262 variant.
    "SX126X",
    "SX1262",
    # Regions: all seven sub-GHz regions LBM supports are enabled so
    # AT+REGION can select any of them at runtime (see makefiles/regions.mk
    # for the full list). REGION_WW_2G4 deliberately excluded - that's the
    # 2.4GHz worldwide band for LR11xx/SX128x radios, not applicable to the
    # SX1262 modules these three boards use.
    "REGION_AS_923",
    "REGION_AU_915",
    "REGION_CN_470",
    "REGION_CN_470_RP_1_0",
    "REGION_EU_868",
    "REGION_IN_865",
    "REGION_KR_920",
    "REGION_RU_864",
    "REGION_US_915",
    # Device classes - both required per the original spec.
    "ADD_CLASS_B",
    "ADD_CLASS_C",
    # Relay - both roles required per the original spec. See
    # LoRaWANRelay.h for the important caveat on the RX/serving side.
    "ADD_RELAY_TX",
    "ADD_RELAY_RX",
    # LBM's own debug trace macro (smtc_modem_hal_print_trace calls are
    # gated by this in some LBM internals, not just our own port). 0 = off.
    # Flip to 1 here (or override via your own platformio.ini build_flags)
    # for verbose LBM-internal trace output during bring-up.
    ("MODEM_HAL_DBG_TRACE", 0),
]

env.Append(CPPDEFINES=defines)
