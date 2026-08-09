/**
 * @file WisBlockLoRaFlash.h
 * @brief Thin persistent-storage interface, implemented differently per MCU
 * in WisBlockLoRaFlash.cpp:
 *   - nRF52840 (RAK4631): internal flash page via Adafruit's InternalFileSystem / LittleFS,
 *   - ESP32-S3 (RAK3312): Preferences (NVS),
 *   - RP2040   (RAK11310): LittleFS on the RP2040 flash filesystem.
 *
 * Named blobs, not a general filesystem: "give me back the last thing I
 * saved under this key". Used both for our own WisBlockPersistedConfig
 * (key "wb_cfg") and for LBM's own context store, which needs several
 * independent regions - one per modem_context_type_t (key "wb_lbm_<type>").
 */
#ifndef WISBLOCK_LORA_FLASH_H
#define WISBLOCK_LORA_FLASH_H

#include <stddef.h>
#include <stdint.h>

namespace WisBlockLoRaFlash
{
/** Must be called once before read()/write(), e.g. from WisBlockLoRaWAN::begin(). */
bool init();

/** Reads up to `len` bytes into `buf`. Returns false if nothing was ever stored under `key`. */
bool read(const char *key, uint8_t *buf, size_t len);

/** Writes `len` bytes from `buf` under `key`, overwriting any previous contents. */
bool write(const char *key, const uint8_t *buf, size_t len);

/** Erases the blob stored under `key`. */
bool erase(const char *key);
} // namespace WisBlockLoRaFlash

#endif // WISBLOCK_LORA_FLASH_H
