/**
 * @file WisBlockLoRaWAN_all.h
 * @brief Convenience header pulling in the whole public surface (API + AT).
 * Applications can also just `#include <WisBlockLoRaWAN.h>` directly if they
 * don't need the AT command interface.
 *
 * Also the canonical home for this library's own version number (distinct
 * from RUI3's own firmware version, which AT+VER mirrors the *format* of but
 * not the value - this library isn't RUI3 firmware, just AT-compatible with
 * it). Bump the three numeric parts below for a release; the combined
 * string macro is derived from them, so nothing else needs to be kept in
 * sync by hand. Used by WisBlockLoRaAT.cpp's AT+VER handler.
 */
#ifndef WISBLOCK_LORAWAN_ALL_H
#define WISBLOCK_LORAWAN_ALL_H

#define WISBLOCK_LORAWAN_VERSION_MAJOR 1
#define WISBLOCK_LORAWAN_VERSION_MINOR 0
#define WISBLOCK_LORAWAN_VERSION_PATCH 0

#define WISBLOCK_LORAWAN_STRINGIFY_( x ) #x
#define WISBLOCK_LORAWAN_STRINGIFY( x ) WISBLOCK_LORAWAN_STRINGIFY_( x )
/** "1.0.0" - a plain C string literal, safe to concatenate with adjacent
 * literals (`"prefix_" WISBLOCK_LORAWAN_VERSION_STRING "_suffix"`). */
#define WISBLOCK_LORAWAN_VERSION_STRING                          \
	WISBLOCK_LORAWAN_STRINGIFY( WISBLOCK_LORAWAN_VERSION_MAJOR )  \
	"." WISBLOCK_LORAWAN_STRINGIFY( WISBLOCK_LORAWAN_VERSION_MINOR ) \
	"." WISBLOCK_LORAWAN_STRINGIFY( WISBLOCK_LORAWAN_VERSION_PATCH )

#include "WisBlockLoRaAT.h"
#include "WisBlockLoRaWAN.h"

#ifdef NRF52_SERIES
#include <nrf_nvic.h>
#endif
#ifdef ESP32
#include <Preferences.h>
#include <esp_system.h>
#endif

#endif // WISBLOCK_LORAWAN_ALL_H
