/**
 * @file main.h
 * @author Bernd Giesecke (bernd@giesecke.tk)
 * @brief Defines and includes for application
 * @version 0.1
 * @date 2026-10-05
 *
 * @copyright Copyright (c) 2026
 *
 */
#ifndef MAIN_H
#define MAIN_H

#include <Arduino.h>
#include <WisBlockLoRaWAN.h>
#include <WisBlockLoRaAT.h>
#include "wisblock_cayenne.h"
#include <TimeLib.h>
#include <WisBlockLoRaFlash.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "custom_at.h"
#include "mesh.h"
#include "oled.h"
#include "battery.h"

#ifdef ARDUINO_ARCH_NRF52
#include <nrf_nvic.h>
#include <nrfx_power.h>

#endif
#ifdef ARDUINO_ARCH_ESP32
#define isInISR() xPortInIsrContext() == pdTRUE
#endif

#ifdef ARDUINO_ARCH_RP2040
// Arduino-Pico (RAK11300 / RAK11310): needs FreeRTOS SMP (Arduino IDE: Tools -> Operating System -> FreeRTOS SMP,
// PlatformIO: build_flags = -DPIO_FRAMEWORK_ARDUINO_ENABLE_FREERTOS) and a flash size with a file system.
#ifndef __FREERTOS
#error "Enable FreeRTOS SMP (Tools -> Operating System, or -DPIO_FRAMEWORK_ARDUINO_ENABLE_FREERTOS)"
#endif
#include <FreeRTOS.h>
#include <semphr.h>
#include <timers.h>
#include <task.h>
#define isInISR() portCHECK_IF_IN_ISR()
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

// Wakeup events
#define NO_EVENT 0
#define STATUS 0b0000000000000001
#define N_STATUS 0b1111111111111110
#define CAD_FAIL 0b0000000000000010
#define N_CAD_FAIL 0b1111111111111101
#define LORA_DATA 0b0000000000000100
#define N_LORA_DATA 0b1111111111111011
#define LORA_TX_FIN 0b0000000000001000
#define N_LORA_TX_FIN 0b1111111111110111
#define NO_CONFIG 0b0000000000010000
#define N_NO_CONFIG 0b1111111111101111
#define USB_DATA 0b0000000000100000
#define N_USB_DATA 0b1111111111011111

// Define alternate pdMS_TO_TICKS that casts uint64_t for long intervals due to limitation in nrf52840 BSP
#define mypdMS_TO_TICKS(xTimeInMs) ((TickType_t)(((uint64_t)(xTimeInMs) * configTICK_RATE_HZ) / 1000))

/** Callbacks */
#if defined ARDUINO_ARCH_ESP32 || defined ARDUINO_ARCH_NRF52 || defined ARDUINO_ARCH_RP2040
void periodic_wakeup(TimerHandle_t unused);
// #elif defined ARDUINO_ARCH_NRF52 || defined ARDUINO_ARCH_RP2040
// void periodic_wakeup(TimerHandle_t unused);
#else
#warning MCU not supported
#endif
void onUnhandledLine(const char *line);
void onTxDone(const WisBlockTxResult &result);
void onRxDone(const WisBlockRxResult &result);
void onCad(WisBlockCADResult result);

/** Buffer for receive downlinks */
extern WisBlockRxResult rx_buffered;
/** Time received from LNS */
extern time_t unixTime; // a time stamp
/** Semaphore used by events to wake up loop task */
extern SemaphoreHandle_t g_task_sem;
/** LoRa event handler */
extern WisBlockLoRaWAN lora;
/** AT command handler */
extern WisBlockLoRaAT at_serial;
/** Flag for the event type */
extern volatile uint16_t g_task_event_type;
#if defined ARDUINO_ARCH_NRF52 || defined ARDUINO_ARCH_ESP32 || defined ARDUINO_ARCH_RP2040
extern TimerHandle_t g_task_wakeup_timer;
#else
#warning MCU not supported
#endif
/** USB data buffer for non-AT commands */
extern char usb_data[];
/** Uplink buffer in Cayenne LPP format */
extern WisCayenne g_solution_data;
// Custom settings
extern CustomAtSettings custom_settings;
void on_mesh_data(uint32_t fromID, uint8_t *rxPayload, uint16_t rxSize, int16_t rxRssi, int8_t rxSnr);
void map_changed_cb(void);
// Mesh events to be handled in the app_event_handler
#define MESH_MAP_CHANGED 0b1000000000000000
#define N_MESH_MAP_CHANGED 0b0111111111111111
extern uint32_t g_master_node_addr;
// Generic data buffer
extern char data_buffer[];

/** Union to convert uint64_t into a byte array */
union longlong_byte_u
{
	uint64_t l_value = 0;
	uint8_t b_values[8];
};

// Event flag setters and clearers
#if defined ARDUINO_ARCH_ESP32
static portMUX_TYPE g_event_mux = portMUX_INITIALIZER_UNLOCKED;
#define EVENT_LOCK() portENTER_CRITICAL(&g_event_mux)
#define EVENT_UNLOCK() portEXIT_CRITICAL(&g_event_mux)
#elif defined ARDUINO_ARCH_NRF52 || defined ARDUINO_ARCH_RP2040
#define EVENT_LOCK() taskENTER_CRITICAL()
#define EVENT_UNLOCK() taskEXIT_CRITICAL()
#endif

/**
 * @brief Set event flag secure
 *
 * @param bits event mask, e.g. AT_CMD
 */
inline void taskEventSet(uint16_t bits)
{
	EVENT_LOCK();
	g_task_event_type |= bits;
	EVENT_UNLOCK();
}

/**
 * @brief Reset event flag secure
 *
 * @param mask Inverted event mask, e.g. N_AT_CMD
 */
inline void taskEventClear(uint16_t mask)
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

#endif // MAIN_H