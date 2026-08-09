#include "WisBlockLoRaWANConfig.h"
#include "WisBlockLoRaFlash.h"
#include <string.h>

#define WISBLOCK_CONFIG_FLASH_KEY "wb_cfg"

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

static uint16_t computeCrc(const WisBlockPersistedConfig &cfg)
{
	// CRC covers everything after the crc16 field itself.
	const uint8_t *base = reinterpret_cast<const uint8_t *>(&cfg);
	size_t offset = offsetof(WisBlockPersistedConfig, workMode);
	size_t len = sizeof(WisBlockPersistedConfig) - offset;
	return wisblockConfigCrc16(base + offset, len);
}

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

bool wisblockConfigFactoryReset()
{
	WisBlockPersistedConfig defaults;
	return wisblockConfigSave(defaults);
}
