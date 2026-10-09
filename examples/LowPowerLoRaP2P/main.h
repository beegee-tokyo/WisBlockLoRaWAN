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
#include <Arduino.h>
#include <WisBlockLoRaWAN.h>
#include <WisBlockLoRaAT.h>
#ifdef NRF52_SERIES
#include <nrf_nvic.h>
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
#define STATUS 0b0000000000000001
#define N_STATUS 0b1111111111111110
#define BLE_CONFIG 0b0000000000000010
#define N_BLE_CONFIG 0b1111111111111101
#define BLE_DATA 0b0000000000000100
#define N_BLE_DATA 0b1111111111111011
#define LORA_DATA 0b0000000000001000
#define N_LORA_DATA 0b1111111111110111
#define LORA_TX_FIN 0b0000000000010000
#define N_LORA_TX_FIN 0b1111111111101111
#define AT_CMD 0b0000000000100000
#define N_AT_CMD 0b1111111111011111
#define LORA_JOIN_FIN 0b0000000001000000
#define N_LORA_JOIN_FIN 0b1111111110111111
