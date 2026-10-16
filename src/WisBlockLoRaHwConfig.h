/**
 * @file WisBlockLoRaHwConfig.h
 * @brief Describes the wiring between the MCU and the SX1261/SX1262 radio,
 * so this library isn't limited to the exact RAKwireless WisBlock modules
 * it started with (RAK4631, RAK3312, RAK11310) - any board with a supported
 * MCU (nRF52840, ESP32-S3 or RP2040 with the Arduino-Pico core and FreeRTOS SMP)
 * and an SX1262 wired up in one of the common ways can be described with this struct and
 * passed to WisBlockLoRaWAN::begin(const WisBlockLoRaHwConfig&).
 *
 * Modeled on beegee-tokyo/SX126x-Arduino's `hw_config` struct
 * (src/boards/mcu/board.h) - same field set and meaning (renamed to this
 * library's naming convention), including the same RF-switch/TCXO control
 * combinations documented there:
 *   - radioTxEn == -1, radioRxEn != -1, useRxenAntPwr == true:
 *     radioRxEn is a single antenna-switch *power* pin (the actual TX/RX
 *     steering is done by the SX1262 itself via DIO2 - useDio2AntSwitch).
 *     This is the case all three existing WisBlock presets below use.
 *   - radioTxEn != -1 AND radioRxEn != -1, useRxenAntPwr == false:
 *     the two pins steer an external RF switch directly (radioRxEn high
 *     during RX, radioTxEn high during TX, e.g. eByte E22-style modules).
 *   - useDio3AntSwitch: DIO3 (instead of a GPIO) controls the antenna
 *     switch power (e.g. Insight SiP ISP4520).
 *   - useDio3Tcxo + tcxoCtrlVoltage/tcxoStartupTimeUs: DIO3 controls the
 *     TCXO supply instead (mutually exclusive with useDio3AntSwitch - the
 *     SX1262 only has one DIO3).
 * wisblock_radio_hal.cpp and wisblock_ral_sx126x_bsp.c implement all four
 * combinations; see their doc comments for exactly how.
 *
 * Semtech LR2021 (LR20xx) transceivers (RAK13702 module) are described with the same struct:
 * chipType = WISBLOCK_LORA_CHIP_LR2021 plus the lr* fields below. The SPI/NSS/RESET/BUSY/IRQ
 * wiring and the antenna switch power pin are the same fields as for the SX126x. See
 * wisblockLoRaHwConfigRAK3401_RAK13702(). The LR2021 backend is only compiled in when
 * WISBLOCK_ENABLE_LR2021 is defined (see wb_lbm_config.h), otherwise WisBlockLoRaWAN::begin() refuses
 * a config with this chip type. Which radio is used is decided at run time by chipType, so one build
 * can run an SX126x or an LR2021 board.
 *
 * A custom board only needs to fill in this struct (or start from one of
 * the preset builder functions below and override individual fields) and
 * pass it to WisBlockLoRaWAN::begin(const WisBlockLoRaHwConfig&) instead of
 * plain begin().
 */
#ifndef WISBLOCK_LORA_HW_CONFIG_H
#define WISBLOCK_LORA_HW_CONFIG_H

#include <stdint.h>

#include "lbm/smtc_modem_core/radio_drivers/sx126x_driver/src/sx126x.h" // vendored: src/lbm/smtc_modem_core/radio_drivers/sx126x_driver/src/sx126x.h

#if defined(ARDUINO_ARCH_NRF52) || defined(NRF52840_XXAA) || defined(ARDUINO_ARCH_ESP32) || defined(ARDUINO_ARCH_RP2040)
#include <SPI.h>
#define WISBLOCK_LORA_HW_CONFIG_HAS_SPI_INSTANCE 1
/** Defined on every MCU that supports WisBlockLoRaWAN::begin(const WisBlockLoRaHwConfig&) */
#define WISBLOCK_HAS_HW_CONFIG_INIT 1
#if defined(ARDUINO_ARCH_RP2040)
// Arduino-Pico: SPIClass is only the abstract HardwareSPI base class, without setRX()/setTX()/setSCK()
typedef SPIClassRP2040 WisBlockLoRaSpiClass;
#else
typedef SPIClass WisBlockLoRaSpiClass;
#endif
#endif

/** Which radio chip the board carries. For the SX126x variants: only SX1262 (the HP PA variant)
 * is actually wired up in wisblock_ral_sx126x_bsp.c's TX power table today -
 * see that file's doc comment. Kept as a field for completeness/parity with
 * SX126x-Arduino's hw_config; setting SX1261 does not yet change the PA
 * config actually programmed into the chip. */
enum WisBlockLoRaChipType : uint8_t
{
	WISBLOCK_LORA_CHIP_SX1261 = 0,
	WISBLOCK_LORA_CHIP_SX1262 = 1,
	WISBLOCK_LORA_CHIP_LR2021 = 2, ///< Semtech LR2021 (LR20xx), RAK13702 module
};

struct WisBlockLoRaHwConfig
{
	WisBlockLoRaChipType chipType = WISBLOCK_LORA_CHIP_SX1262;

	// SPI + control pins. -1 is a legal value only for pinDio1 in a P2P-only,
	// polling-driven build; every other pin is required, so the presets
	// below always fill in all of them and there's no "unset" default here
	// that would silently compile into something half-working.
	int8_t pinNss = -1;
	int8_t pinReset = -1;
	int8_t pinBusy = -1;
	int8_t pinDio1 = -1; ///< MCU pin of the radio IRQ line: DIO1 of an SX126x, the DIO named in lrIrqDio of an LR2021
	int8_t pinSck = -1;
	int8_t pinMosi = -1;
	int8_t pinMiso = -1;

	// RF switch / PA control - see the field-combination list in this file's
	// doc comment above.
	int8_t radioTxEn = -1; ///< LORA ANTENNA TX ENABLE, -1 = unused
	int8_t radioRxEn = -1; ///< LORA ANTENNA RX ENABLE (or antenna switch power), -1 = unused

	bool useDio2AntSwitch = false;	///< SX1262 DIO2 controls the antenna/RF switch
	bool useDio3Tcxo = false;		///< SX1262 DIO3 controls the TCXO supply voltage
	bool useDio3AntSwitch = false; ///< SX1262 DIO3 controls the antenna (e.g. Insight SiP ISP4520)
	bool useRxenAntPwr = false;	///< radioRxEn is antenna-switch *power*, not a TX/RX steering line
	bool useLdo = false;			///< SX1262 uses its LDO regulator instead of the DC-DC converter

	/** Same values as SX126x-Arduino's RadioTcxoCtrlVoltage_t - both mirror
	 * the SX126x datasheet's TCXO_CTRL bitfield, so sx126x_tcxo_ctrl_voltages_t
	 * (already vendored for LBM) is reused here directly instead of adding a
	 * second, numerically-identical enum. */
	sx126x_tcxo_ctrl_voltages_t tcxoCtrlVoltage = SX126X_TCXO_CTRL_3_3V;
	uint32_t tcxoStartupTimeUs = 5000; ///< TCXO startup delay. Ignored unless useDio3Tcxo is set.

	// LR2021 (LR20xx) only, ignored for the SX126x chips. These are DIO numbers of the radio chip,
	// not MCU pins. -1 = unused. For the LR2021 the antenna switch power pin is radioRxEn with
	// useRxenAntPwr (same as the SX126x presets), the TCXO uses tcxoCtrlVoltage/tcxoStartupTimeUs
	// and the regulator is DC-DC unless useLdo is set. useDio2AntSwitch, useDio3Tcxo and
	// useDio3AntSwitch do not apply to the LR2021.
	int8_t lrIrqDio = -1;		 ///< LR2021 DIO that drives the IRQ line wired to pinDio1 (RAK13702: DIO8)
	int8_t lrRfSwitchDioLf = -1; ///< LR2021 DIO steering the sub-GHz antenna switch, RF_LF1/RF_LF2 (RAK13702: DIO7)
	int8_t lrRfSwitchDioHf = -1; ///< LR2021 DIO steering the 2.4 GHz antenna switch, RFI_HF (RAK13702: DIO9)

	uint32_t spiHz = 8000000UL; ///< SPI clock; SX1262's datasheet max is 16 MHz.

#if WISBLOCK_LORA_HW_CONFIG_HAS_SPI_INSTANCE
	/** Custom SPI peripheral instance. nullptr (default) = remap and use the
	 * Arduino core's global `SPI` object with the pins above (RP2040: `SPI` or `SPI1`,
	 * whichever one the SCK pin belongs to). Set this if
	 * your board wires the radio to a second SPI peripheral instead - see
	 * wisblockLoRaHwConfigRAK3401_RAK13300() below for a real example (RAK3400's
	 * default SPI is already committed to onboard flash). */
	WisBlockLoRaSpiClass *spiInstance = nullptr;
#endif
};

/**
 * @brief Radio wiring of the RAK4631 (nRF52840 WisBlock Core)
 *
 * RAK4631: nRF52840 WisBlock Core module with an integrated SX1262.
 *
 * @return Hardware configuration
 */
WisBlockLoRaHwConfig wisblockLoRaHwConfigRAK4631();
/**
 * @brief Radio wiring of the RAK3312 (ESP32-S3 WisBlock Core)
 *
 * RAK3312: ESP32-S3 WisBlock Core module with an integrated SX1262.
 *
 * @return Hardware configuration
 */
WisBlockLoRaHwConfig wisblockLoRaHwConfigRAK3312();
/**
 * @brief Radio wiring of the RAK11310 (RP2040 WisBlock Core)
 *
 * RAK11310: RP2040 WisBlock Core module with an integrated SX1262 (RAK11300 module).
 * Needs the Arduino-Pico core. The radio is hardwired to the SPI1 peripheral.
 *
 * @return Hardware configuration
 */
WisBlockLoRaHwConfig wisblockLoRaHwConfigRAK11310();
/**
 * @brief Radio wiring of the RAK11300 (RP2040 WisDuo module), same as wisblockLoRaHwConfigRAK11310()
 *
 * @return Hardware configuration
 */
WisBlockLoRaHwConfig wisblockLoRaHwConfigRAK11300();

#if defined(ARDUINO_ARCH_NRF52) || defined(NRF52840_XXAA)
/**
 * @brief Radio wiring of the RAK3401 with a RAK13300 / RAK13302 SX1262 module
 *
 * RAK3401: a RAK3400 WisDuo nRF52840 module used as a WisBlock Core module,
 * combined with a RAK13300 or RAK13302 SX1262 LoRa transceiver module (both
 * share the same HW connection) - see the Creation Log entry \"Flexible
 * hw_config-based radio init (RAK3401 / non-WisBlock boards)\" for where
 * these values come from.
 *
 * Unlike the other three presets, this one uses a second SPI peripheral
 * (NRF_SPIM1, on pins MISO=29/SCK=3/MOSI=30) instead of the MCU's default
 * SPI - the default SPI0 is already used elsewhere on this board. The
 * SPIClass instance itself is a static local inside this function's .cpp
 * file; every call returns a config pointing at the same one.
 *
 * Formerly named wisblockLoRaHwConfigRAK3401(), which still exists as an alias.
 *
 * @return Hardware configuration
 */
WisBlockLoRaHwConfig wisblockLoRaHwConfigRAK3401_RAK13300();

/**
 * @brief Radio wiring of the RAK3401 with a RAK13702 LR2021 module
 *
 * Same MCU pins and SPI peripheral as wisblockLoRaHwConfigRAK3401_RAK13300() (SPI, NSS, RESET,
 * BUSY, antenna switch power), with chipType WISBLOCK_LORA_CHIP_LR2021. The LR2021 drives its IRQ
 * on DIO8, which is wired to the same MCU pin as DIO1 of the RAK13300. Antenna switch RFI_HF
 * (2.4 GHz) is steered by DIO9, RF_LF1/RF_LF2 (sub-GHz) by DIO7, both powered by the same pin
 * as on the RAK13300. TCXO 3.3 V / 50 ms, DC-DC regulator.
 *
 * Needs WISBLOCK_ENABLE_LR2021 (wb_lbm_config.h or the build flags), without it
 * WisBlockLoRaWAN::begin() refuses this configuration. Not tested on hardware yet, see the top of
 * wisblock_ral_lr20xx_bsp.c for the assumptions (antenna switch truth table, PA settings).
 *
 * @return Hardware configuration
 */
WisBlockLoRaHwConfig wisblockLoRaHwConfigRAK3401_RAK13702();

/**
 * @brief Old name of wisblockLoRaHwConfigRAK3401_RAK13300(), kept so existing sketches build unchanged
 *
 * @return Hardware configuration
 */
inline WisBlockLoRaHwConfig wisblockLoRaHwConfigRAK3401()
{
	return wisblockLoRaHwConfigRAK3401_RAK13300();
}
#endif

#endif // WISBLOCK_LORA_HW_CONFIG_H
