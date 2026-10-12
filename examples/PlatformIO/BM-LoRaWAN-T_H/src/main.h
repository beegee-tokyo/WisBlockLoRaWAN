/**
 * @file main.h
 * @author Bernd Giesecke (bernd@giesecke.tk)
 * @brief Defines and includes for application
 * @version 0.1
 * @date 2026-10-12
 * 
 * @copyright Copyright (c) 2026
 * 
 */
#ifndef MAIN_H
#define MAIN_H

#include <Arduino.h>
#include <SPI.h>
#include <WisBlockLoRaWAN.h>
#include <WisBlockLoRaAT.h>
#include <WisBlockLoRaFlash.h>
#include <Wire.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <TimeLib.h>
#include "wisblock_cayenne.h"

#ifdef NRF52_SERIES
// #include <nrf_nvic.h>
#endif
#ifdef ARDUINO_ARCH_ESP32
#include <Ticker.h>
#endif

#ifdef ARDUINO_ARCH_RP2040
// Arduino-Pico (RAK11300 / RAK11310): this example needs FreeRTOS SMP.
// Arduino IDE: Tools -> Operating System -> FreeRTOS SMP. PlatformIO: build_flags = -DPIO_FRAMEWORK_ARDUINO_ENABLE_FREERTOS
// The file system is needed to save the settings: Tools -> Flash Size -> e.g. "2MB (Sketch: 1MB, FS: 1MB)".
#ifndef __FREERTOS
#error "Enable FreeRTOS SMP (Tools -> Operating System, or -DPIO_FRAMEWORK_ARDUINO_ENABLE_FREERTOS)"
#endif
#include <FreeRTOS.h>
#include <semphr.h>
#include <timers.h>
#include <task.h>
#define isInISR() portCHECK_IF_IN_ISR()
// Required to get printf on Arduino
__asm__(".global _printf_float");
// Add missing WisBlock definitions
#ifndef _VARIANT_RAK11300_
#define WB_IO1 6
#define WB_IO2 22
#define WB_IO3 7
#define WB_IO4 28
#define WB_IO5 9
#define WB_IO6 8
#define WB_A0 26
#define WB_A1 27
#endif

// LEDs
#define PIN_LED1 (23)
#define PIN_LED2 (24)

#define LED_BUILTIN PIN_LED1
#define LED_CONN PIN_LED2

#ifndef _VARIANT_RAK11300_
#define LED_GREEN PIN_LED1
#define LED_BLUE PIN_LED2
#endif
#endif

/** Wake up events, more events can be defined in app.h */
#define NO_EVENT 0
#define STATUS           0b0000000000000001
#define N_STATUS         0b1111111111111110
#define LINK_CHECK       0b0000000000000010
#define N_LINK_CHECK     0b1111111111111101
#define LORA_JOIN_FAIL   0b0000000000000100
#define N_LORA_JOIN_FAIL 0b1111111111111011
#define LORA_DATA        0b0000000000001000
#define N_LORA_DATA      0b1111111111110111
#define LORA_TX_FIN      0b0000000000010000
#define N_LORA_TX_FIN    0b1111111111101111
#define LORA_JOIN_FIN    0b0000000000100000
#define N_LORA_JOIN_FIN  0b1111111111011111
#define LORA_TIME        0b0000000001000000
#define N_LORA_TIME      0b1111111110111111
#define NO_CONFIG        0b0000000010000000
#define N_NO_CONFIG      0b1111111101111111
#define USB_DATA         0b0000000100000000
#define N_USB_DATA       0b1111111011111111

// Forward declarations
#if defined ARDUINO_ARCH_NRF52 || defined ARDUINO_ARCH_RP2040
void periodic_wakeup(TimerHandle_t unused);
#elif defined ESP32
void periodic_wakeup(void);
#endif
void onUnhandledLine(const char *line);
void onJoined(void);
void onJoinFailed(void);
void onTxDone(const WisBlockTxResult &result);
void onRxDone(const WisBlockRxResult &result);
void onTimeAnswer(bool success, const WisBlockTimeAnswer &t);
void onLinkCheck(bool success, const WisBlockLinkCheckResult &r);

/** Sensor Functions */
void init_batt(void);
float read_batt(void);
bool init_rak1901(void);
void read_rak1901(void);
/** Flag if RAK1901 temperature and humidity sensor was found */
extern bool has_rak1901;

// Globals
// Define alternate pdMS_TO_TICKS that casts uint64_t for long intervals due to limitation in nrf52840 BSP
#define mypdMS_TO_TICKS(xTimeInMs) ((TickType_t)(((uint64_t)(xTimeInMs) * configTICK_RATE_HZ) / 1000))
/** LoRa event handler */
extern WisBlockLoRaWAN lora;
/** AT command handler */
extern WisBlockLoRaAT at_serial;
/** Flag for the event type */
extern volatile uint16_t g_task_event_type;
/** Semaphore used by events to wake up loop task */
extern SemaphoreHandle_t g_task_sem;
#if defined ARDUINO_ARCH_NRF52 || defined ARDUINO_ARCH_RP2040
/** Timer to wakeup task frequently and send message */
extern TimerHandle_t g_task_wakeup_timer;
#elif defined ESP32
/** Timer to wakeup task frequently and send message */
extern Ticker g_task_wakeup_timer;
#else
#warning MCU not supported
#endif
extern WisBlockRxResult rx_buffered;
/** Time received from LNS */
extern time_t last_unixTime;
/** LinkCheck results */
extern WisBlockLinkCheckResult last_linkCheck;
/** LinkCheck success flag */
extern bool last_LinkCheckSuccess;
/** USB data buffer for non-AT commands */
extern char usb_data[];
/** Uplink buffer in Cayenne LPP format */
extern WisCayenne g_solution_data;

/**
 * The event flags are set from several tasks (LBM task, USB event task,
 * timers) and cleared from loop(), possibly on different cores. A plain
 * "flags |= x" / "flags &= ~x" is a read-modify-write that can lose an event
 * when two of them overlap, so all updates go through these helpers.
 */
#if defined ARDUINO_ARCH_ESP32
static portMUX_TYPE g_event_mux = portMUX_INITIALIZER_UNLOCKED;
#define EVENT_LOCK() portENTER_CRITICAL(&g_event_mux)
#define EVENT_UNLOCK() portEXIT_CRITICAL(&g_event_mux)
#elif defined ARDUINO_ARCH_NRF52 || defined ARDUINO_ARCH_RP2040
#define EVENT_LOCK() taskENTER_CRITICAL()
#define EVENT_UNLOCK() taskEXIT_CRITICAL()
#else
#define EVENT_LOCK()
#define EVENT_UNLOCK()
#endif

/**
 * @brief Set event flags
 *
 * The event flags are set from several tasks (LBM task, USB event task, timers) and cleared from
 * loop(), possibly on different cores. A plain `flags |= x` is a read-modify-write that can lose an
 * event when two of them overlap, so all updates run in a critical section.
 *
 * @param bits Event flag bits to set, e.g. STATUS
 */
static inline void taskEventSet(uint16_t bits)
{
	EVENT_LOCK();
	g_task_event_type |= bits;
	EVENT_UNLOCK();
}

/**
 * @brief Clear event flags
 *
 * Counterpart of taskEventSet(), runs in a critical section as well.
 *
 * @param mask Inverted event mask, e.g. N_USB_DATA (all bits set except the one to clear)
 */
static inline void taskEventClear(uint16_t mask)
{
	EVENT_LOCK();
	g_task_event_type &= mask;
	EVENT_UNLOCK();
}

// Debug
// Debug output set to 0 to disable app debug output
#ifndef MY_DEBUG
#define MY_DEBUG 1
#endif // MY_DEBUG

#if MY_DEBUG > 0
#define MYLOG(tag, ...)                  \
	do                                   \
	{                                    \
		if (tag)                         \
			Serial.printf("[%s] ", tag); \
		Serial.printf(__VA_ARGS__);      \
		Serial.printf("\n");             \
	} while (0);                         \
	delay(100)
#else
#define MYLOG(...)
#endif // MY_DEBUG

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
	/// the application (see loop() in LowPowerLoRaWAN.ino) to decide what 0
	/// actually does, this command only stores the value.
	uint32_t sendIntervalS = 0;

	// Add new custom AT command values below this line (see the
	// EXTENDING THIS STRUCT note above, and bump `version` above when you
	// do) - e.g.:
	// uint8_t someOtherSetting = 0;
};

/**
 * @brief Register the custom AT commands and load their saved settings
 *
 * Registers this file's custom AT commands (ATC+SENDINT and ATC+STATUS) on `atParser` and loads any
 * previously saved CustomAtSettings from flash. Call once from setup(), after both lora.begin()
 * (WisBlockLoRaFlash must already be initialized, see WisBlockLoRaFlash.h) and atParser.begin().
 *
 * @param atParser AT command parser the custom commands are added to
 */
void registerCustomATCommands(WisBlockLoRaAT &atParser);

/**
 * @brief Get the current custom settings
 *
 * Read-only access to the current settings for the rest of the application (e.g. to decide
 * whether and when to send based on sendIntervalS). Reflects whatever was last loaded from flash or
 * set with ATC+SENDINT=, not necessarily what is on flash if a write ever silently failed. See
 * handleSendInt() in custom_at.cpp for why that is the chosen tradeoff.
 *
 * @return Reference to the current settings
 */
const CustomAtSettings &getCustomAtSettings();
#endif // MAIN_H