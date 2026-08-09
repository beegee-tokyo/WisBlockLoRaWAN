/**
 * @file wisblock_radio_hal.h
 * @brief SPI/GPIO glue between the Arduino core and Semtech's SX1262 radio
 * driver, for the RAK4631 (nRF52840) WisBlock core module.
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
 * requirement with no changes needed on the driver side. The `context`
 * pointer threaded through every call is Semtech's mechanism for supporting
 * multiple radios/boards from one driver instance; here it resolves to a
 * `WisBlockRadioContext*` describing which board's pins to use.
 *
 * Reference (function contract only, not copied verbatim):
 * https://github.com/Lora-net/sx126x_driver — sx126x_hal.h
 */
#ifndef WISBLOCK_RADIO_HAL_H
#define WISBLOCK_RADIO_HAL_H

#include <stdint.h>

/** Matches sx126x_hal_status_t from Semtech's sx126x_hal.h. */
enum sx126x_hal_status_e
{
	SX126X_HAL_STATUS_OK = 0,
	SX126X_HAL_STATUS_UNSUPPORTED_FEATURE = 1,
	SX126X_HAL_STATUS_UNKNOWN_VALUE = 2,
	SX126X_HAL_STATUS_ERROR = 3,
};
typedef enum sx126x_hal_status_e sx126x_hal_status_t;

/**
 * Per-board radio context. One static instance is provided for the RAK4631
 * (see wisblockRadioContext in the .cpp); the `context` pointer LBM/the
 * radio driver carries around is a `const void*` to one of these.
 */
struct WisBlockRadioContext
{
	int8_t pinNss;
	int8_t pinReset;
	int8_t pinBusy;
	int8_t pinDio1;
	int8_t pinAntPwr;
	uint32_t spiHz;
};

namespace WisBlockRadioHal
{
/** Configures SPI + all radio GPIOs. Call once from WisBlockLoRaWAN::begin(). */
void init();

/** The context instance to pass as `context` into every sx126x_hal_*() / ral_*() call. */
const void *context();

/** True while BUSY is asserted (chip processing a previous command / booting). */
bool isBusy();

/**
 * Blocks until BUSY deasserts or `timeoutMs` elapses.
 * Returns false on timeout (radio unresponsive / not powered / wrong pin).
 */
bool waitOnBusy(uint32_t timeoutMs = 1000);

/**
 * Drives the LORA_ANT_PWR GPIO that feeds this board's RF-switch/antenna
 * front-end supply. This is normally managed automatically - see the
 * "Radio HAL: RF-switch power tracking" note in each
 * wisblock_radio_hal_<board>.cpp: it's switched off the moment the radio
 * itself is put to sleep (SX126x SetSleep opcode observed in
 * sx126x_hal_write()) and back on, with a settle delay, the moment the next
 * SPI transaction wakes it. Exposed here only for cases outside that normal
 * flow - e.g. forcing it off before a deep MCU sleep where you know the
 * radio will be fully re-initialized on wake anyway.
 */
void setAntennaPower(bool on);

/**
 * True if the radio is currently in our own tracked SLEEP state (last
 * command issued to it was SetSleep, and nothing has woken it since).
 * Lets a caller like LoRaP2PEngine::handleEvents() skip a routine "is
 * anything pending?" IRQ-status poll entirely while asleep, instead of
 * reading it via SPI - which checkDeviceReady() would otherwise treat as a
 * wake request (antenna power back on, NSS wake sequence) regardless of
 * why the read was requested, leaving the radio parked awake in STANDBY
 * until the next deliberate sleepRadio() call. See the "radio silently
 * re-woken by its own idle IRQ poll" note in the README for the full story.
 */
bool isAsleep();

/**
 * The settle delay (in whole milliseconds, rounded up) that
 * checkDeviceReady() waits after restoring LORA_ANT_PWR before trusting the
 * SPI bus again on wake - see kAntPwrSettleUs in each
 * wisblock_radio_hal_<board>.cpp. Exists so smtc_modem_hal_get_radio_tcxo_startup_delay_ms()
 * (wisblock_lbm_port.cpp) can fold this into the total latency it reports
 * to LBM's radio_planner, instead of reporting only the SX1262's own TCXO
 * startup time and silently running late against radio_planner's own
 * wake-ahead scheduling for every RX1/RX2/TX window - see the "LoRaWAN
 * RX1/RX2 window missed" note in the README for why this matters.
 */
uint32_t antennaPowerSettleMs();
} // namespace WisBlockRadioHal

// ---------------------------------------------------------------------------
// Functions the SX1262 driver (sx126x_driver / LBM) calls directly.
// Signatures match Semtech's sx126x_hal.h exactly so no adapter is needed.
// ---------------------------------------------------------------------------
extern "C"
{
	sx126x_hal_status_t sx126x_hal_reset(const void *context);
	sx126x_hal_status_t sx126x_hal_wakeup(const void *context);

	/**
	 * Full-duplex-over-two-buffers SPI transaction: asserts NSS, clocks out
	 * `command_length` command bytes followed by `data_length` data bytes,
	 * then deasserts NSS. Used for every write-type opcode (SetSleep,
	 * SetTx, WriteRegister, WriteBuffer, ...).
	 */
	sx126x_hal_status_t sx126x_hal_write(const void *context, const uint8_t *command,
										  const uint16_t command_length, const uint8_t *data,
										  const uint16_t data_length);

	/**
	 * Same shape as write, but after clocking out the command it clocks in
	 * `data_length` bytes from the radio into `data`. Used for read-type
	 * opcodes (GetStatus, ReadRegister, ReadBuffer, ...).
	 */
	sx126x_hal_status_t sx126x_hal_read(const void *context, const uint8_t *command,
										 const uint16_t command_length, uint8_t *data,
										 const uint16_t data_length);
}

#endif // WISBLOCK_RADIO_HAL_H
