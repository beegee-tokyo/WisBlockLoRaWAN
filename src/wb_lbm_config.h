/**
 * @file wb_lbm_config.h
 * @brief Build configuration for the vendored LoRa Basics Modem (LBM) sources.
 *
 * PlatformIO gets these -D flags and the nested include directories from
 * extra_script.py. The Arduino IDE / arduino-cli do neither: they only put
 * this library's `src/` folder on the include path and have no per-library
 * compiler flags. So that the same sources build in both:
 *
 *  - every include of an LBM header inside this library is a path relative
 *    to the including file (rewritten by tools/make_lbm_includes_relative.py),
 *    so nothing depends on the include path or on other installed libraries,
 *  - every translation unit that uses LBM starts with this header, which
 *    supplies the -D flags below when the build system has not.
 *
 * Under PlatformIO the flags already exist and everything here is a no-op.
 * Keep this list in sync with `defines` in extra_script.py.
 */
#ifndef WB_LBM_CONFIG_H
#define WB_LBM_CONFIG_H

// RP2040 (RAK11300 / RAK11310): only Earle Philhower's Arduino-Pico core is supported. The
// original Arduino mbed core has no FreeRTOS and a different SPI/GPIO API.
#if defined(ARDUINO_ARCH_MBED) || defined(ARDUINO_ARCH_MBED_RP2040)
#error "WisBlockLoRaWAN: the Arduino mbed core is not supported for RP2040. Use arduino-pico (https://github.com/earlephilhower/arduino-pico, PlatformIO: https://github.com/maxgerhardt/platform-raspberrypi)."
#endif

#ifndef NUMBER_OF_STACKS
#define NUMBER_OF_STACKS 1
#endif

#if !defined(RP2_101) && !defined(RP2_103)
#define RP2_103
#endif

#ifndef SX126X
#define SX126X
#endif
#ifndef SX1262
#define SX1262
#endif

#ifndef REGION_AS_923
#define REGION_AS_923
#endif
#ifndef REGION_AU_915
#define REGION_AU_915
#endif
#ifndef REGION_CN_470
#define REGION_CN_470
#endif
#ifndef REGION_CN_470_RP_1_0
#define REGION_CN_470_RP_1_0
#endif
#ifndef REGION_EU_868
#define REGION_EU_868
#endif
#ifndef REGION_EU_433
#define REGION_EU_433
#endif
#ifndef REGION_IN_865
#define REGION_IN_865
#endif
#ifndef REGION_KR_920
#define REGION_KR_920
#endif
#ifndef REGION_RU_864
#define REGION_RU_864
#endif
#ifndef REGION_US_915
#define REGION_US_915
#endif

#ifndef ADD_CLASS_B
#define ADD_CLASS_B
#endif
#ifndef ADD_CLASS_C
#define ADD_CLASS_C
#endif
#ifndef SMTC_MULTICAST
#define SMTC_MULTICAST
#endif

// LBM's own debug trace (join, TX/RX windows, MAC commands ... printed through
// smtc_modem_hal_print_trace()). 0 = off, same as extra_script.py for PlatformIO. LBM defaults to ON
// when nothing is defined, so this must be here for the Arduino IDE, which does not run
// extra_script.py. Set it to 1 for verbose output during bring-up: edit it here, or define
// MODEM_HAL_DBG_TRACE=1 in the build flags (it is only set when it is not defined already).
#ifndef MODEM_HAL_DBG_TRACE
#define MODEM_HAL_DBG_TRACE 0
#endif

// Semtech LR2021 (LR20xx) radio, RAK13702 module on a RAK3401: compiled in only when this is defined.
// It is OFF by default, so builds for the SX1262 modules (and all other boards) are unchanged and do
// not grow. Enable it by removing the // in front of the #define below, or by defining
// WISBLOCK_ENABLE_LR2021 in the build flags. The radio that is used is still selected at runtime
// by WisBlockLoRaHwConfig::chipType, so one build can run an SX1262 or an LR2021 board.
// #define WISBLOCK_ENABLE_LR2021
#if defined(WISBLOCK_ENABLE_LR2021) && !defined(ARDUINO_ARCH_NRF52) && !defined(NRF52840_XXAA)
#error "WisBlockLoRaWAN: WISBLOCK_ENABLE_LR2021 is only supported for the nRF52840 (RAK3401) for now."
#endif

// The 2.4 GHz LoRaWAN region (WW2G4, WISBLOCK_REGION_WW2G4) is available together with the LR2021 and is
// usable only on a board that has one. It is the only region that is not defined above.
#if defined(WISBLOCK_ENABLE_LR2021) && !defined(REGION_WW_2G4)
#define REGION_WW_2G4
#endif

#endif /* WB_LBM_CONFIG_H */
