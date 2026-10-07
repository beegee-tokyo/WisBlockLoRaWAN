/**
 * @file wisblock_radio_hal.h
 * @brief SPI/GPIO glue between the Arduino core and Semtech's SX1262 radio
 * driver.
 *
 * This implements the exact function contract of Semtech's own
 * `sx126x_hal.h` (from the open-source `sx126x_driver` repo, also consumed
 * by LoRa Basics Modem). That header declares four functions the driver
 * calls into and expects the board support package (this file) to provide:
 *
 *   sx126x_hal_reset()
 *   sx126x_hal_wakeup()
 *   sx126x_hal_write()
 *   sx126x_hal_read()
 *
 * Drop Semtech's `sx126x_driver` sources into src/lbm/ (or reference them
 * directly if vendoring full LBM) and this file satisfies its BSP
 * requirement with no changes needed on the driver side.
 *
 * Board flexibility: since the Creation Log entry "Flexible hw_config-based
 * radio init (RAK3401 / non-WisBlock boards)", this file is no longer one
 * fixed implementation per RAKwireless board (wisblock_radio_hal_rak4631.cpp
 * / _rak3312.cpp used to hardcode WisBlockLoRaBoards.h's pin macros
 * directly). init(const WisBlockLoRaHwConfig&) below takes the board's pin
 * assignment and RF-switch/TCXO wiring at runtime instead, so any nRF52840
 * or ESP32-S3 board with an SX1262 can be supported without a new .cpp file
 * - see WisBlockLoRaHwConfig.h's doc comment for the field-combinations
 * this supports and wisblock_radio_hal.cpp (the merged nRF52/ESP32
 * implementation) for how they're realized in GPIO/SPI terms.
 * wisblock_radio_hal_rak11310.cpp (RP2040) is untouched and still owns its
 * own fixed pin set - RP2040/mbed has no full FreeRTOS support, so it isn't
 * part of this flexible path (WisBlockLoRaWAN::begin() still auto-selects
 * it at compile time for RP2040 builds).
 *
 * Reference (function contract only, not copied verbatim):
 * https://github.com/Lora-net/sx126x_driver — sx126x_hal.h
 */
#ifndef WISBLOCK_RADIO_HAL_H
#define WISBLOCK_RADIO_HAL_H

#include <stdint.h>

#include "WisBlockLoRaHwConfig.h"

/** Matches sx126x_hal_status_t from Semtech's sx126x_hal.h. */
enum sx126x_hal_status_e
{
	SX126X_HAL_STATUS_OK = 0,
	SX126X_HAL_STATUS_UNSUPPORTED_FEATURE = 1,
	SX126X_HAL_STATUS_UNKNOWN_VALUE = 2,
	SX126X_HAL_STATUS_ERROR = 3,
};
typedef enum sx126x_hal_status_e sx126x_hal_status_t;

namespace WisBlockRadioHal
{
#if defined(ARDUINO_ARCH_NRF52) || defined(NRF52840_XXAA) || defined(ARDUINO_ARCH_ESP32)
/**
 * @brief Configure SPI and all radio pins from an explicit board description
 *
 * Configures SPI + all radio GPIOs from an explicit hw_config, so a board
 * other than the three built-in WisBlock presets can be used - see
 * WisBlockLoRaWAN::begin(const WisBlockLoRaHwConfig&). Call once, before
 * anything else in this namespace or the LBM/P2P engines.
 *
 * Only compiled for nRF52840/ESP32-S3 - see this file's doc comment for why
 * RP2040 (RAK11310) doesn't take this path.
 *
 * @param hwConfig Radio wiring (pins, SPI, TCXO) of the board
 */
void init(const WisBlockLoRaHwConfig &hwConfig);
#endif

/**
 * @brief Configure SPI and all radio pins for the compile-time selected board
 *
 * Configures SPI + all radio GPIOs using the compile-time-selected
 * RAKwireless board preset (RAK4631/RAK3312/RAK11310 - see
 * WisBlockLoRaHwConfig.h). This is what plain WisBlockLoRaWAN::begin()
 * (with no hw_config argument) calls; kept for that backward-compatible
 * path and equivalent to calling the config-based init() above with the
 * matching wisblockLoRaHwConfigRAKxxxx() preset.
 */
void init();

/**
 * @brief Get the context pointer for the sx126x_hal_xxx() functions
 *
 * The context instance to pass as `context` into every sx126x_hal_*() /
 * ral_*() call. Always resolves to the single static board config this HAL
 * was last init()-ed with - see the NOTE in wisblock_radio_hal.cpp's
 * sx126x_hal_reset() for why the incoming pointer itself is never used.
 *
 * @return Context pointer
 */
const void *context();

/**
 * @brief Get the DIO1 pin of the active board
 *
 * The active board's DIO1 pin (from the WisBlockLoRaHwConfig this HAL was
 * last init()-ed with) - wisblock_lbm_port.cpp's WisBlockLbmPort::init()
 * reads this to attach the DIO1 interrupt on the right pin, instead of
 * hardcoding WisBlockLoRaBoards.h's compile-time LORA_DIO1 macro (which
 * only matches the three built-in RAKwireless presets, not a custom board
 * passed to WisBlockLoRaWAN::begin(const WisBlockLoRaHwConfig&) - see the
 * Creation Log entry "DIO1 interrupt hardcoded to the wrong pin for custom
 * hw_config boards" for the RP_FAILSAFE panic this caused on RAK3401
 * before this existed). Only valid after WisBlockRadioHal::init() has run.
 *
 * @return Pin number
 */
int8_t dio1Pin();

/**
 * @brief Check the BUSY line of the radio
 *
 * True while BUSY is asserted (chip processing a previous command / booting).
 *
 * @return true while the radio is busy
 */
bool isBusy();

/**
 * @brief Wait until the radio is not busy
 *
 * Blocks until BUSY deasserts or `timeoutMs` elapses.
 * Returns false on timeout (radio unresponsive / not powered / wrong pin).
 *
 * @param timeoutMs Maximum time to wait in ms
 * @return true if the radio became ready, false on timeout
 */
bool waitOnBusy(uint32_t timeoutMs = 1000);

/**
 * @brief Switch the antenna switch power
 *
 * Drives whichever pin(s) this board's hw_config uses for RF-switch/antenna
 * front-end power (radioRxEn if useRxenAntPwr, DIO3 if useDio3AntSwitch, or
 * both if a board somehow uses both) - see the "RF-switch power tracking"
 * note in wisblock_radio_hal.cpp: it's switched off the moment the radio
 * itself is put to sleep (SX126x SetSleep opcode observed in
 * sx126x_hal_write()) and back on, with a settle delay, the moment the next
 * SPI transaction wakes it. Exposed here only for cases outside that normal
 * flow - e.g. forcing it off before a deep MCU sleep where you know the
 * radio will be fully re-initialized on wake anyway. A no-op on a board
 * that uses neither flag (radioTxEn/radioRxEn wired as direct RF-switch
 * steering lines instead - see WisBlockLoRaHwConfig.h).
 *
 * @param on true to power the antenna switch
 */
void setAntennaPower(bool on);

/**
 * @brief Check if the radio is in sleep mode
 *
 * True if the radio is currently in our own tracked SLEEP state (last
 * command issued to it was SetSleep, and nothing has woken it since).
 * Lets a caller like LoRaP2PEngine::handleEvents() skip a routine "is
 * anything pending?" IRQ-status poll entirely while asleep, instead of
 * reading it via SPI - which checkDeviceReady() would otherwise treat as a
 * wake request (antenna power back on, NSS wake sequence) regardless of
 * why the read was requested, leaving the radio parked awake in STANDBY
 * until the next deliberate sleepRadio() call. See the "radio silently
 * re-woken by its own idle IRQ poll" note in the README for the full story.
 *
 * @return true if the radio sleeps
 */
bool isAsleep();

/**
 * @brief Get the settle time of the antenna switch power
 *
 * The settle delay (in whole milliseconds, rounded up) that
 * checkDeviceReady() waits after restoring antenna power before trusting
 * the SPI bus again on wake - see kAntPwrSettleUs in wisblock_radio_hal.cpp.
 * Exists so smtc_modem_hal_get_radio_tcxo_startup_delay_ms()
 * (wisblock_lbm_port.cpp) can fold this into the total latency it reports
 * to LBM's radio_planner, instead of reporting only the SX1262's own TCXO
 * startup time and silently running late against radio_planner's own
 * wake-ahead scheduling for every RX1/RX2/TX window - see the "LoRaWAN
 * RX1/RX2 window missed" note in the README for why this matters.
 *
 * @return Settle time in ms, rounded up
 */
uint32_t antennaPowerSettleMs();
} // namespace WisBlockRadioHal

// ---------------------------------------------------------------------------
// Functions the SX1262 driver (sx126x_driver / LBM) calls directly.
// Signatures match Semtech's sx126x_hal.h exactly so no adapter is needed.
// ---------------------------------------------------------------------------
extern "C"
{
	/**
	 * @brief Reset the radio (called by the SX126x driver)
	 *
	 * @param context Context pointer
	 * @return Driver status
	 */
	sx126x_hal_status_t sx126x_hal_reset(const void *context);
	/**
	 * @brief Wake the radio up from sleep (called by the SX126x driver)
	 *
	 * @param context Context pointer
	 * @return Driver status
	 */
	sx126x_hal_status_t sx126x_hal_wakeup(const void *context);

	/**
	 * @brief SPI write transaction (called by the SX126x driver)
	 *
	 * Full-duplex-over-two-buffers SPI transaction: asserts NSS, clocks out
	 * `command_length` command bytes followed by `data_length` data bytes,
	 * then deasserts NSS. Used for every write-type opcode (SetSleep,
	 * SetTx, WriteRegister, WriteBuffer, ...).
	 *
	 * @param context Context pointer
	 * @param command Command bytes
	 * @param command_length Number of command bytes
	 * @param data Data bytes
	 * @param data_length Number of data bytes
	 * @return Driver status
	 */
	sx126x_hal_status_t sx126x_hal_write(const void *context, const uint8_t *command,
										  const uint16_t command_length, const uint8_t *data,
										  const uint16_t data_length);

	/**
	 * @brief SPI read transaction (called by the SX126x driver)
	 *
	 * Same shape as write, but after clocking out the command it clocks in
	 * `data_length` bytes from the radio into `data`. Used for read-type
	 * opcodes (GetStatus, ReadRegister, ReadBuffer, ...).
	 *
	 * @param context Context pointer
	 * @param command Command bytes
	 * @param command_length Number of command bytes
	 * @param data Receives the data
	 * @param data_length Number of bytes to read
	 * @return Driver status
	 */
	sx126x_hal_status_t sx126x_hal_read(const void *context, const uint8_t *command,
										 const uint16_t command_length, uint8_t *data,
										 const uint16_t data_length);
}

#endif // WISBLOCK_RADIO_HAL_H
