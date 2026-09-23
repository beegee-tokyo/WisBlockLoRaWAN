/**
 * @file custom_at.cpp
 * @brief Implementation of the ATC+SENDINT custom AT command example - see
 * custom_at.h for the struct/API this file implements.
 *
 * @version 0.1
 * @date 2026-09-22
 *
 * @copyright Copyright (c) 2026
 *
 */
#include <Arduino.h>
#include "custom_at.h"
#include <WisBlockLoRaFlash.h>
#include <stdlib.h>
#include <string.h>

#if defined ARDUINO_ARCH_NRF52
extern TimerHandle_t g_task_wakeup_timer;
#elif defined ESP32
extern Ticker g_task_wakeup_timer;
#endif
// Define alternate pdMS_TO_TICKS that casts uint64_t for long intervals due to limitation in nrf52840 BSP
#define mypdMS_TO_TICKS(xTimeInMs) ((TickType_t)(((uint64_t)(xTimeInMs) * configTICK_RATE_HZ) / 1000))

namespace
{
	// Bump this whenever CustomAtSettings gains or changes a field -
	// loadCustomAtSettings() below rejects (falls back to defaults for) any
	// saved blob whose own `version` doesn't match this.
	const uint16_t CUSTOM_AT_SETTINGS_VERSION = 2; // must match CustomAtSettings::version's default in custom_at.h
	const char *CUSTOM_AT_FLASH_KEY = "wb_custom";

	CustomAtSettings g_customSettings;

	bool saveCustomAtSettings()
	{
		g_customSettings.version = CUSTOM_AT_SETTINGS_VERSION; // keep in sync in case the two ever drift
		return WisBlockLoRaFlash::write(CUSTOM_AT_FLASH_KEY,
										(const uint8_t *)&g_customSettings,
										sizeof(g_customSettings));
	}

	void loadCustomAtSettings()
	{
		CustomAtSettings loaded;
		if (WisBlockLoRaFlash::read(CUSTOM_AT_FLASH_KEY, (uint8_t *)&loaded, sizeof(loaded)) &&
			loaded.version == CUSTOM_AT_SETTINGS_VERSION)
		{
			g_customSettings = loaded;
		}
		// else: nothing saved yet (fresh board), the saved blob is a
		// different size than sizeof(CustomAtSettings) (WisBlockLoRaFlash::
		// read() already returns false for that on its own), or it's the
		// right size but an older/different `version` - e.g. a field got
		// added or repurposed since that blob was saved. Any of those
		// cases just keeps the compiled-in defaults from CustomAtSettings's
		// own member initializers (version = 2, sendIntervalS = 0) rather
		// than risk misreading a differently-laid-out struct - see
		// custom_at.h's EXTENDING THIS STRUCT note.
	}

	// ATC+SENDINT=<seconds> / ATC+SENDINT=? / ATC+SENDINT
	//
	// Handler signature matches WisBlockLoRaAT::CustomAtHandler (see
	// WisBlockLoRaAT.h's doc comment on addCustomATCommand()): `args` is
	// nullptr for a bare command, the literal "?" for a query, or the raw
	// text after '=' for a set. This command only ever takes a single
	// plain decimal number, so unlike this library's own colon-separated
	// custom commands (e.g. +JOIN=/+P2P=), there's nothing further to
	// split here.
	WisBlockAtStatus handleSendInt(Stream &port, const char *cmd, char *args)
	{
		(void)cmd; // only one name is registered for this handler - see registerCustomATCommands() below

		// No args at all (bare "ATC+SENDINT") or an explicit query
		// ("ATC+SENDINT=?") both just report the current value - there's
		// no action tied to invoking this command bare, so doubling it up
		// as a query is more useful than an error.
		if (!args || strcmp(args, "?") == 0)
		{
			port.print("ATC+SENDINT=");
			port.println(g_customSettings.sendIntervalS);
			return WISBLOCK_AT_OK;
		}

		// Write: reject anything that isn't a plain non-negative decimal
		// number outright (a leading '-', trailing garbage after the
		// digits, or no digits at all) rather than silently truncating it
		// via atoi()-style parsing.
		if (args[0] == '-')
		{
			return WISBLOCK_AT_PARAM_ERROR;
		}
		char *end = nullptr;
		unsigned long seconds = strtoul(args, &end, 10);
		if (end == args || *end != '\0')
		{
			return WISBLOCK_AT_PARAM_ERROR;
		}

		g_customSettings.sendIntervalS = (uint32_t)seconds;

		// Saved immediately on every set, unlike the library's own
		// AT+<setting>= commands (which stage changes in RAM until an
		// explicit AT+SAVE - see WisBlockLoRaAT.h's addCustomATCommand()
		// doc comment for why custom commands don't have to follow that
		// same convention). The new value is already live in RAM
		// (g_customSettings, read via getCustomAtSettings()) regardless of
		// whether the flash write below succeeds - a failed save here
		// means the value won't survive a reboot, not that it failed to
		// take effect right now.
		if (!saveCustomAtSettings())
		{
			return WISBLOCK_AT_ERROR; // flash write failed - see WisBlockLoRaFlash.h
		}

		// Stop timer if it already exists
#if defined NRF52_SERIES
		if (g_task_wakeup_timer != NULL)
		{
			if (isInISR())
			{
				BaseType_t xHigherPriorityTaskWoken = pdFALSE;
				xTimerStopFromISR(g_task_wakeup_timer, &xHigherPriorityTaskWoken);
				portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
			}
			else
			{
				xTimerStop(g_task_wakeup_timer, 0);
			}
		}
#endif
#if defined ESP32
		g_task_wakeup_timer.detach();
#endif

		if (g_customSettings.sendIntervalS != 0)
		{
#if defined NRF52_SERIES
			if (isInISR())
			{
				BaseType_t xHigherPriorityTaskWoken = pdFALSE;
				xTimerChangePeriodFromISR(g_task_wakeup_timer, mypdMS_TO_TICKS(g_customSettings.sendIntervalS * 1000), &xHigherPriorityTaskWoken);
				portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
			}
			else
			{
				xTimerChangePeriod(g_task_wakeup_timer, mypdMS_TO_TICKS(g_customSettings.sendIntervalS * 1000), 0);
			}
#endif
#if defined ESP32
			g_task_wakeup_timer.attach_ms(g_lorawan_settings.send_repeat_time, periodic_wakeup);
#endif
		}

		return WISBLOCK_AT_OK;
	}
} // namespace

void registerCustomATCommands(WisBlockLoRaAT &atParser)
{
	loadCustomAtSettings();

	// Registers as ATC+SENDINT (the "SENDINT" here is the bare command
	// name - no "ATC+", no "="; addCustomATCommand() adds the "ATC+"
	// prefix itself - see WisBlockLoRaAT.h). Add more
	// atParser.addCustomATCommand(...) calls here as more custom AT
	// commands get added alongside ATC+SENDINT.
	if (!atParser.addCustomATCommand("SENDINT", "get/set the periodic send interval in seconds (0 = disabled)", handleSendInt))
	{
		Serial.println("[CustomAT] Failed to register ATC+SENDINT - table full or already registered?");
	}
}

const CustomAtSettings &getCustomAtSettings()
{
	return g_customSettings;
}
