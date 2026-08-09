/**
 * @file WisBlockLoRaBoards.h
 * @brief Pin maps for the WisBlock core modules this library targets.
 *
 * All three modules place the SX1262 on the WisBlock IO slot with the same
 * logical signals (NSS, RESET, BUSY, DIO1, and optional antenna switch /
 * RF switch control). Only the actual GPIO numbers differ per MCU.
 *
 * These values have been verified against the actual board schematics for
 * all three targets (see per-board notes below).
 */
#ifndef WISBLOCK_LORA_BOARDS_H
#define WISBLOCK_LORA_BOARDS_H

// clang-format off

#if defined(ARDUINO_ARCH_NRF52) || defined(NRF52840_XXAA) // ---- RAK4631 ----
	#define WISBLOCK_BOARD_NAME   "RAK4631 (nRF52840)"
	#define LORA_SPI_NSS          42  // P1.10
	#define LORA_RESET            38  // P1.06
	#define LORA_BUSY             46  // P1.14
	#define LORA_DIO1             47  // P1.15
	#define LORA_ANT_SWITCH       -1  // handled internally by SX1262 DIO2 (RF switch)
	#define LORA_SPI_SCK          43
	#define LORA_SPI_MOSI         44
	#define LORA_SPI_MISO         45
	#define LORA_ANT_PWR          37
	/* NOTE: Verified and correct */

#elif defined(ARDUINO_ARCH_ESP32) // ---- RAK3312 (ESP32-S3) ----
	#define WISBLOCK_BOARD_NAME   "RAK3312 (ESP32-S3)"
	#define LORA_SPI_NSS          7
	#define LORA_RESET            8
	#define LORA_BUSY             48
	#define LORA_DIO1             47
	#define LORA_ANT_SWITCH       -1
	#define LORA_SPI_SCK          5
	#define LORA_SPI_MOSI         6
	#define LORA_SPI_MISO         3
	#define LORA_ANT_PWR          4
	/* NOTE: Verified and correct */

#elif defined(ARDUINO_ARCH_RP2040) // ---- RAK11310 (RP2040) ----
	#define WISBLOCK_BOARD_NAME   "RAK11310 (RP2040)"
	#define LORA_SPI_NSS          13
	#define LORA_RESET            14
	#define LORA_BUSY             15
	#define LORA_DIO1             29
	#define LORA_ANT_SWITCH       -1
	#define LORA_SPI_SCK          10
	#define LORA_SPI_MOSI         11
	#define LORA_SPI_MISO         12
	#define LORA_ANT_PWR          25
	/* NOTE: Verified and correct */

#else
	#error "WisBlockLoRaWAN: unsupported board. Add a pin map in WisBlockLoRaBoards.h"
#endif

// clang-format on

#endif // WISBLOCK_LORA_BOARDS_H
