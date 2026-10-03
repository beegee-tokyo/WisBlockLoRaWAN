/**
 * @file wb_lbm_config.h
 * @brief Build configuration for the vendored LoRa Basics Modem (LBM) sources.
 *
 * PlatformIO gets these -D flags and the nested include directories from
 * extra_script.py. The Arduino IDE / arduino-cli do neither: they only put
 * this library's `src/` folder on the include path and have no per-library
 * compiler flags. So that the same sources build in both:
 *
 *  - every #include of an LBM header inside this library is a path relative
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

#endif /* WB_LBM_CONFIG_H */
