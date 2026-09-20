/**
 * @file WisBlockLoRaWANConfig.h
 * @brief Persisted configuration blob + load/save API.
 *
 * The whole config (work mode + LoRaWAN settings + P2P settings) is stored
 * as a single versioned, CRC-checked struct so "AT+SAVE" / "AT+RESTORE" and
 * the equivalent API calls are trivial and atomic.
 */
#ifndef WISBLOCK_LORAWAN_CONFIG_H
#define WISBLOCK_LORAWAN_CONFIG_H

#include "WisBlockLoRaWANTypes.h"
#include <stddef.h> // size_t

#define WISBLOCK_CONFIG_MAGIC 0x57424C52UL // "WBLR"
// FIX: bumped for the new top-level `alias` field (AT+ALIAS getter/setter) - the CRC check
// below would likely catch the resulting size/layout change on its own even without this,
// but bumping the version makes the incompatibility with older saved blobs explicit and
// intentional rather than incidental. A config saved by an older library version is safely
// detected as invalid (falls back to factory defaults) either way - see wisblockConfigLoad().
#define WISBLOCK_CONFIG_VERSION 2

struct WisBlockPersistedConfig
{
	uint32_t magic = WISBLOCK_CONFIG_MAGIC;
	uint16_t version = WISBLOCK_CONFIG_VERSION;
	uint16_t crc16 = 0; // computed over everything below, see WisBlockLoRaWANConfig.cpp

	WisBlockWorkMode workMode = WISBLOCK_MODE_LORAWAN;
	bool lowPowerEnabled = true;
	// RUI3-compatible AT+ALIAS - a free-form, user-settable device label, unrelated to
	// LoRaWAN/P2P operation. RUI3 caps a *set* value at 16 characters (see
	// WisBlockLoRaWAN::setAlias()'s doc comment) but this buffer is sized to comfortably fit
	// the longer factory-default text below, which predates the setter and was never itself
	// meant to be re-entered verbatim through AT+ALIAS=.
#ifdef NRF52_SERIES
	char alias[32] = "WISBLOCK_BASICMODEM_RAK4631";
#elif defined(ARDUINO_ARCH_ESP32)
	char alias[32] = "WISBLOCK_BASICMODEM_RAK3312";
#elif defined(ARDUINO_ARCH_RP2040)
	char alias[32] = "WISBLOCK_BASICMODEM_RAK11310";
#else
	char alias[32] = "";
#endif

	WisBlockLoRaWANSettings lorawan;
	WisBlockP2PSettings p2p;
};

/**
 * Loads config from flash into `out`. Returns false (and fills `out` with
 * factory defaults) if no valid config was found, e.g. first boot or CRC
 * mismatch.
 */
bool wisblockConfigLoad(WisBlockPersistedConfig &out);

/** Persists `cfg` to flash. Returns false on write failure. */
bool wisblockConfigSave(const WisBlockPersistedConfig &cfg);

/** Resets flash-stored config back to factory defaults. */
bool wisblockConfigFactoryReset();

/** Computes the CRC16-CCITT used to validate the stored blob. */
uint16_t wisblockConfigCrc16(const uint8_t *data, size_t len);

#endif // WISBLOCK_LORAWAN_CONFIG_H
