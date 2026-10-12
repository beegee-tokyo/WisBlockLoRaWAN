/**
 * @file main.h
 * @author Bernd Giesecke (bernd@giesecke.tk)
 * @brief Defines and includes
 * @version 0.1
 * @date 2026-07-16
 * 
 * @copyright Copyright (c) 2026
 * 
 */
/// \todo This must be defined in library or in platformIO.ini
// #define NUMBER_OF_STACKS 1
// #define RP2_103 1
// #define SX126X 1
// #define SX1262 1
// #define REGION_AS_923 1
// #define REGION_AU_915 1
// #define REGION_CN_470 1
// #define REGION_CN_470_RP_1_0 1
// #define REGION_EU_868 1
// #define REGION_EU_433 1
// #define REGION_IN_865 1
// #define REGION_KR_920 1
// #define REGION_RU_864 1
// #define REGION_US_915 1
// #define ADD_CLASS_B 1
// #define ADD_CLASS_C 1
// #define ADD_RELAY_TX 1
// #define ADD_RELAY_RX 1
// #define MODEM_HAL_DBG_TRACE 1

#include <Arduino.h>
#include <SPI.h>
#include <WisBlockLoRaWAN.h>
#include <WisBlockLoRaAT.h>
#include "wisblock_cayenne.h"
#include "custom_at.h"
#include <TimeLib.h>

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
// The Arduino-Pico variant of the RAK11300 does not define the LEDs. WisBlock LED1/LED2 are GPIO23/GPIO24
// on the RAK11310, check the pins of your base board.
#ifndef LED_GREEN
#define LED_GREEN 23
#endif
#ifndef LED_BLUE
#define LED_BLUE 24
#endif
#ifndef LED_BUILTIN
#define LED_BUILTIN LED_GREEN
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
