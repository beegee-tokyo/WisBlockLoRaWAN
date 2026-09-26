/**
 * @file custom_at.h
 * @brief Example custom AT command - ATC+SENDINT=<seconds> - registered
 * with WisBlockLoRaAT::addCustomATCommand() (see WisBlockLoRaAT.h).
 *
 * Demonstrates the three things a "real" custom command usually needs:
 *   1. a value the rest of the application can read back (getCustomAtSettings()),
 *   2. persistence across reboots, in a struct that's meant to grow over
 *      time as more custom AT commands get added (CustomAtSettings below),
 *   3. wiring it into the AT parser with one call from setup().
 *
 * @version 0.1
 * @date 2026-09-22
 *
 * @copyright Copyright (c) 2026
 *
 */
#ifndef CUSTOM_AT_H
#define CUSTOM_AT_H

#include <WisBlockLoRaAT.h>
#include <stdint.h>

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
	/// Bump this whenever a field is added/changed below - see the
	/// EXTENDING THIS STRUCT note above. loadCustomAtSettings() (in
	/// custom_at.cpp) checks this against its own compiled-in
	/// CUSTOM_AT_SETTINGS_VERSION and falls back to defaults on a
	/// mismatch, the same way it already does on a straight size
	/// mismatch - belt and suspenders against loading a differently-laid-
	/// out struct as if it were this one.
	uint16_t version = 2; // bumped from 1: sendIntervalMs (ms) renamed/reinterpreted as sendIntervalS (s)

	/// AT+SENDINT= - application-defined periodic send interval, in
	/// seconds. 0 (the default) means "no automatic sending" - it's up to
	/// the application (see LowPowerLoRaWAN.cpp's loop()) to decide what 0
	/// actually does, this command only stores the value.
	uint32_t sendIntervalS = 0;

	// Add new custom AT command values below this line (see the
	// EXTENDING THIS STRUCT note above, and bump `version` above when you
	// do) - e.g.:
	// uint8_t someOtherSetting = 0;
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

#endif // CUSTOM_AT_H
