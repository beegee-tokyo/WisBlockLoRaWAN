#include "WisBlockLoRaFlash.h"
#include <stdio.h>
#include <string.h>

#define WISBLOCK_FLASH_FILE_PREFIX "/wb_"
#define WISBLOCK_FLASH_FILE_SUFFIX ".bin"
#define WISBLOCK_NVS_NAMESPACE "wb_lora"

namespace
{
// Builds "/wb_<key>.bin" (nRF52/RP2040) - stays well under typical LittleFS
// filename limits since keys here are short fixed strings ("wb_cfg",
// "wb_lbm_0".."wb_lbm_5").
void buildFilename(const char *key, char *out, size_t outLen)
{
	snprintf(out, outLen, "%s%s%s", WISBLOCK_FLASH_FILE_PREFIX, key, WISBLOCK_FLASH_FILE_SUFFIX);
}
} // namespace

// ---------------------------------------------------------------------------
// RAK4631 / nRF52840: Adafruit InternalFileSystem (LittleFS over internal flash)
// ---------------------------------------------------------------------------
#if defined(ARDUINO_ARCH_NRF52) || defined(NRF52840_XXAA)

#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>
using namespace Adafruit_LittleFS_Namespace;

namespace WisBlockLoRaFlash
{
bool init()
{
	InternalFS.begin();
	return true;
}

bool read(const char *key, uint8_t *buf, size_t len)
{
	char filename[32];
	buildFilename(key, filename, sizeof(filename));

	File file(InternalFS);
	if (!file.open(filename, FILE_O_READ))
	{
		return false;
	}
	size_t got = file.read(buf, len);
	file.close();
	return got == len;
}

bool write(const char *key, const uint8_t *buf, size_t len)
{
	char filename[32];
	buildFilename(key, filename, sizeof(filename));

	InternalFS.remove(filename);
	File file(InternalFS);
	if (!file.open(filename, FILE_O_WRITE))
	{
		return false;
	}
	size_t wrote = file.write(buf, len);
	file.close();
	return wrote == len;
}

bool erase(const char *key)
{
	char filename[32];
	buildFilename(key, filename, sizeof(filename));
	return InternalFS.remove(filename);
}
} // namespace WisBlockLoRaFlash

// ---------------------------------------------------------------------------
// RAK3312 / ESP32-S3: Preferences (NVS)
// ---------------------------------------------------------------------------
#elif defined(ARDUINO_ARCH_ESP32)

#include <Preferences.h>
static Preferences prefs;

namespace WisBlockLoRaFlash
{
bool init()
{
	return true; // Preferences opens per-transaction below.
}

bool read(const char *key, uint8_t *buf, size_t len)
{
	// FIX: calling getBytes() directly on a key that doesn't exist yet is
	// functionally fine (returns 0, so this correctly returns false below)
	// but the ESP32 Arduino core's Preferences library logs a scary-looking
	// "[E][Preferences.cpp] getBytesLength(): nvs_get_blob len fail: ...
	// NOT_FOUND" for every single miss, regardless of whether the caller
	// treats a miss as expected. Every key this library reads is
	// legitimately absent on a truly fresh board (wb_cfg before the first
	// saveConfig(), wb_lbm_crash/wb_lbm_crash_flag before LBM's very first
	// crash-status check, wb_lbm_0.. before the first LBM context store) -
	// and LBM's own crash-status HAL callbacks in particular get called
	// many times during a single smtc_modem_init() (once per internal
	// service that checks for a stored crash on bring-up), so a fresh
	// board could log dozens of these for something that was never
	// actually an error. isKey() checks NVS's own key-presence flag
	// without triggering that log path at all - same functional result
	// (false on a genuine miss), silent about it.
	prefs.begin(WISBLOCK_NVS_NAMESPACE, true /* read-only */);
	if (!prefs.isKey(key))
	{
		prefs.end();
		return false;
	}
	size_t got = prefs.getBytes(key, buf, len);
	prefs.end();
	return got == len;
}

bool write(const char *key, const uint8_t *buf, size_t len)
{
	prefs.begin(WISBLOCK_NVS_NAMESPACE, false);
	size_t wrote = prefs.putBytes(key, buf, len);
	prefs.end();
	return wrote == len;
}

bool erase(const char *key)
{
	prefs.begin(WISBLOCK_NVS_NAMESPACE, false);
	bool ok = prefs.remove(key);
	prefs.end();
	return ok;
}
} // namespace WisBlockLoRaFlash

// ---------------------------------------------------------------------------
// RAK11310 / RP2040: LittleFS (arduino-pico core)
// ---------------------------------------------------------------------------
#elif defined(ARDUINO_ARCH_RP2040)

#include <LittleFS.h>

namespace WisBlockLoRaFlash
{
bool init()
{
	return LittleFS.begin();
}

bool read(const char *key, uint8_t *buf, size_t len)
{
	char filename[32];
	buildFilename(key, filename, sizeof(filename));

	File file = LittleFS.open(filename, "r");
	if (!file)
	{
		return false;
	}
	size_t got = file.read(buf, len);
	file.close();
	return got == len;
}

bool write(const char *key, const uint8_t *buf, size_t len)
{
	char filename[32];
	buildFilename(key, filename, sizeof(filename));

	File file = LittleFS.open(filename, "w");
	if (!file)
	{
		return false;
	}
	size_t wrote = file.write(buf, len);
	file.close();
	return wrote == len;
}

bool erase(const char *key)
{
	char filename[32];
	buildFilename(key, filename, sizeof(filename));
	return LittleFS.remove(filename);
}
} // namespace WisBlockLoRaFlash

#else
#error "WisBlockLoRaWAN: no flash backend for this architecture"
#endif
