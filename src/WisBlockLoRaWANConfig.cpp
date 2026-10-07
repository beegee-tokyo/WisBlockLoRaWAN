#include "WisBlockLoRaWANConfig.h"
#include "WisBlockLoRaFlash.h"
#include <string.h>

#define WISBLOCK_CONFIG_FLASH_KEY "wb_cfg"
#define WISBLOCK_FACTORY_FLASH_KEY "wb_factory"

uint16_t wisblockConfigCrc16(const uint8_t *data, size_t len)
{
	uint16_t crc = 0xFFFF;
	for (size_t i = 0; i < len; i++)
	{
		crc ^= (uint16_t)data[i] << 8;
		for (uint8_t b = 0; b < 8; b++)
		{
			crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
		}
	}
	return crc;
}

/**
 * @brief Calculate the CRC16 of a stored configuration
 *
 * @param cfg Configuration
 * @return CRC16 of the data behind the CRC field
 */
static uint16_t computeCrc(const WisBlockPersistedConfig &cfg)
{
	// CRC covers everything after the crc16 field itself.
	const uint8_t *base = reinterpret_cast<const uint8_t *>(&cfg);
	size_t offset = offsetof(WisBlockPersistedConfig, workMode);
	size_t len = sizeof(WisBlockPersistedConfig) - offset;
	return wisblockConfigCrc16(base + offset, len);
}

// ---------------------------------------------------------------------------
// Version 3 -> 4 migration. Layout of the version 3 blob (WisBlockP2PSettings
// before iqInversion/syncWord were appended). Must stay byte-for-byte what
// version 3 wrote - do not edit.
// ---------------------------------------------------------------------------
namespace
{
struct WisBlockP2PSettingsV3
{
	uint32_t frequencyHz;
	uint8_t spreadingFactor;
	WisBlockP2PBandwidth bandwidth;
	WisBlockP2PCodingRate codingRate;
	uint16_t preambleLength;
	int8_t txPowerDbm;
	bool cadEnabled;
	uint16_t symbolTimeout;
	bool rxBoostedGainEnabled;
};

struct WisBlockPersistedConfigV3
{
	uint32_t magic;
	uint16_t version;
	uint16_t crc16;
	WisBlockWorkMode workMode;
	bool lowPowerEnabled;
	char alias[32];
	char firmwarever[32];
	WisBlockLoRaWANSettings lorawan;
	WisBlockP2PSettingsV3 p2p;
};

/**
 * @brief Load a configuration saved by library version 3 and convert it to the current layout
 *
 * Reads `key` as a version 3 blob. On success fills `out` (new P2P fields at their defaults).
 *
 * @param key Flash key of the block
 * @param out Receives the converted configuration
 * @return true if a valid version 3 block was found
 */
bool loadV3(const char *key, WisBlockPersistedConfig &out)
{
	WisBlockPersistedConfigV3 old;
	if (!WisBlockLoRaFlash::read(key, reinterpret_cast<uint8_t *>(&old), sizeof(old)))
	{
		return false;
	}
	const uint8_t *base = reinterpret_cast<const uint8_t *>(&old);
	size_t offset = offsetof(WisBlockPersistedConfigV3, workMode);
	if (old.magic != WISBLOCK_CONFIG_MAGIC || old.version != 3 ||
		old.crc16 != wisblockConfigCrc16(base + offset, sizeof(old) - offset))
	{
		return false;
	}
	WisBlockPersistedConfig cfg; // defaults, including the new P2P fields
	cfg.workMode = old.workMode;
	cfg.lowPowerEnabled = old.lowPowerEnabled;
	memcpy(cfg.alias, old.alias, sizeof(cfg.alias));
	memcpy(cfg.firmwarever, old.firmwarever, sizeof(cfg.firmwarever));
	cfg.lorawan = old.lorawan;
	cfg.p2p.frequencyHz = old.p2p.frequencyHz;
	cfg.p2p.spreadingFactor = old.p2p.spreadingFactor;
	cfg.p2p.bandwidth = old.p2p.bandwidth;
	cfg.p2p.codingRate = old.p2p.codingRate;
	cfg.p2p.preambleLength = old.p2p.preambleLength;
	cfg.p2p.txPowerDbm = old.p2p.txPowerDbm;
	cfg.p2p.cadEnabled = old.p2p.cadEnabled;
	cfg.p2p.symbolTimeout = old.p2p.symbolTimeout;
	cfg.p2p.rxBoostedGainEnabled = old.p2p.rxBoostedGainEnabled;
	out = cfg;
	return true;
}
} // namespace

bool wisblockConfigLoad(WisBlockPersistedConfig &out)
{
	WisBlockPersistedConfig fromFlash;
	bool readOk = WisBlockLoRaFlash::read(WISBLOCK_CONFIG_FLASH_KEY, reinterpret_cast<uint8_t *>(&fromFlash), sizeof(fromFlash));

	if (readOk && fromFlash.magic == WISBLOCK_CONFIG_MAGIC &&
		fromFlash.version == WISBLOCK_CONFIG_VERSION &&
		fromFlash.crc16 == computeCrc(fromFlash))
	{
		out = fromFlash;
		return true;
	}

	// Saved by the previous library version -> keep it, new fields get defaults.
	if (loadV3(WISBLOCK_CONFIG_FLASH_KEY, out))
	{
		return true;
	}

	// Not valid (first boot, corrupted, or version mismatch) -> defaults.
	out = WisBlockPersistedConfig();
	return false;
}

bool wisblockConfigSave(const WisBlockPersistedConfig &cfgIn)
{
	WisBlockPersistedConfig cfg = cfgIn;
	cfg.magic = WISBLOCK_CONFIG_MAGIC;
	cfg.version = WISBLOCK_CONFIG_VERSION;
	cfg.crc16 = computeCrc(cfg);
	return WisBlockLoRaFlash::write(WISBLOCK_CONFIG_FLASH_KEY, reinterpret_cast<const uint8_t *>(&cfg), sizeof(cfg));
}

bool wisblockConfigSaveFactory(const WisBlockPersistedConfig &cfgIn)
{
	WisBlockPersistedConfig cfg = cfgIn;
	cfg.magic = WISBLOCK_CONFIG_MAGIC;
	cfg.version = WISBLOCK_CONFIG_VERSION;
	cfg.crc16 = computeCrc(cfg);
	return WisBlockLoRaFlash::write(WISBLOCK_FACTORY_FLASH_KEY, reinterpret_cast<const uint8_t *>(&cfg), sizeof(cfg));
}

bool wisblockConfigLoadFactory(WisBlockPersistedConfig &out)
{
	WisBlockPersistedConfig fromFlash;
	bool readOk = WisBlockLoRaFlash::read(WISBLOCK_FACTORY_FLASH_KEY, reinterpret_cast<uint8_t *>(&fromFlash), sizeof(fromFlash));

	if (readOk && fromFlash.magic == WISBLOCK_CONFIG_MAGIC &&
		fromFlash.version == WISBLOCK_CONFIG_VERSION &&
		fromFlash.crc16 == computeCrc(fromFlash))
	{
		out = fromFlash;
		return true;
	}

	if (loadV3(WISBLOCK_FACTORY_FLASH_KEY, out))
	{
		return true;
	}

	// Unlike wisblockConfigLoad() above, deliberately no fallback to
	// compiled-in defaults here - a caller asking for the factory backup
	// (restoreFactoryDefaults(), via ATR) needs to know whether one
	// actually exists rather than silently getting *some* config back.
	return false;
}
