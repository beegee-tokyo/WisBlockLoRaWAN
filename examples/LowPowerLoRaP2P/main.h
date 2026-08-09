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
