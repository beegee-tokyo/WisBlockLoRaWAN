/**
 * @file custom_at.h
 * @author Bernd Giesecke (bernd@giesecke.tk)
 * @brief Definitions and includes for custom AT commands
 * @version 0.1
 * @date 2026-11-17
 * 
 * @copyright Copyright (c) 2026
 * 
 */
#include <Arduino.h>

/**
 * Application-defined settings, persisted separately from the library's own
 * WisBlockPersistedConfig (its own flash key, "wb_custom" - see
 * custom_at.cpp) so this can grow independently as more ATC+ commands get
 * added.
 *
 * EXTENDING THIS STRUCT: only ever add new fields at the end, and bump
 * CUSTOM_AT_SETTINGS_VERSION in custom_at.cpp when you do. WisBlockLoRaFlash
 * (see WisBlockLoRaFlash.h)'s read() requires an exact byte-count match, so
 * a struct that grew since the last save simply fails the size check and
 * loadCustomAtSettings() falls back to defaults - it doesn't return garbage
 * for the new field(s), but it also doesn't preserve what was saved under
 * the old, shorter layout. That's the same tradeoff the library's own
 * WisBlockPersistedConfig makes (see WisBlockLoRaWANConfig.cpp) - fine for
 * a field you're just introducing, but don't reorder or resize existing
 * fields, or every device in the field will reset those to default on next
 * boot after an OTA update.
 */
struct CustomAtSettings
{
	/// Bump this whenever a field is added/changed below
	uint16_t version = 3;

	/// AT+SENDINT= - application-defined periodic send interval, in seconds.
	uint32_t sendIntervalS = 0;

	/// Master Node address
	uint32_t masterNodeAddr = 0;
};

/**
 * Registers this file's custom AT command(s) (currently just ATC+SENDINT)
 * on `atParser`, and loads any previously-saved CustomAtSettings from flash.
 * Call once from setup(), after both lora.begin() (WisBlockLoRaFlash must
 * already be initialized - see WisBlockLoRaFlash.h) and atParser.begin().
 */
void registerCustomATCommands(WisBlockLoRaAT &atParser);

/**
 * Read-only access to the current settings for the rest of the application
 * (e.g. to decide whether/when to send based on sendIntervalS). Reflects
 * whatever was last loaded from flash or set via ATC+SENDINT=, not
 * necessarily what's currently on flash if a write ever silently failed -
 * see the comment in custom_at.cpp's handleSendInt() for why that's the
 * chosen tradeoff here.
 */
const CustomAtSettings &getCustomAtSettings();
