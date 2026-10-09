/**
 * @file WisBlockLoRaFlash.h
 * @brief Thin persistent-storage interface, implemented differently per MCU
 * in WisBlockLoRaFlash.cpp:
 *   - nRF52840 (RAK4631): internal flash page via Adafruit's InternalFileSystem / LittleFS,
 *   - ESP32-S3 (RAK3312): Preferences (NVS),
 *   - RP2040   (RAK11310): LittleFS of the Arduino-Pico core (needs a flash size with a file system).
 *
 * Named blobs, not a general filesystem: "give me back the last thing I
 * saved under this key". Used both for our own WisBlockPersistedConfig
 * (key "wb_cfg", plus a second independent copy under "wb_factory" - see
 * WisBlockLoRaWANConfig.h's wisblockConfigSaveFactory()/
 * wisblockConfigLoadFactory() - and AT+FACTORY/ATR in WisBlockLoRaAT.cpp)
 * and for LBM's own context store, which needs several independent
 * regions - one per modem_context_type_t (key "wb_lbm_<type>").
 */
#ifndef WISBLOCK_LORA_FLASH_H
#define WISBLOCK_LORA_FLASH_H

#include <stddef.h>
#include <stdint.h>

namespace WisBlockLoRaFlash
{
/**
 * @brief Initialize the flash storage
 *
 * Must be called once before read()/write(), e.g. from WisBlockLoRaWAN::begin().
 *
 * @return true if the storage is ready
 */
bool init();

/**
 * @brief Read a stored data block
 *
 * Reads up to `len` bytes into `buf`. Returns false if nothing was ever stored under `key`.
 *
 * @param key Name of the block
 * @param buf Receives the data
 * @param len Number of bytes expected
 * @return true if a block of exactly this size was read
 */
bool read(const char *key, uint8_t *buf, size_t len);

/**
 * @brief Write a data block, replacing any previous content
 *
 * Writes `len` bytes from `buf` under `key`, overwriting any previous contents.
 *
 * @param key Name of the block
 * @param buf Data to store
 * @param len Number of bytes
 * @return true if the data was written
 */
bool write(const char *key, const uint8_t *buf, size_t len);

/**
 * @brief Delete a stored data block
 *
 * Erases the blob stored under `key`.
 *
 * @param key Name of the block
 * @return true if the block was deleted
 */
bool erase(const char *key);
} // namespace WisBlockLoRaFlash

#endif // WISBLOCK_LORA_FLASH_H
