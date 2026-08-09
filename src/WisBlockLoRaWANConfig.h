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
#define WISBLOCK_CONFIG_VERSION 1

struct WisBlockPersistedConfig
{
	uint32_t magic = WISBLOCK_CONFIG_MAGIC;
	uint16_t version = WISBLOCK_CONFIG_VERSION;
	uint16_t crc16 = 0; // computed over everything below, see WisBlockLoRaWANConfig.cpp

	WisBlockWorkMode workMode = WISBLOCK_MODE_LORAWAN;
	bool lowPowerEnabled = true;

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
