/* WisBlockLoRaWAN: build flags for the Arduino IDE, see wb_lbm_config.h */
#include "wb_lbm_config.h"
#include "wisblock_lbm_port.h"
#include "WisBlockLoRaFlash.h"
#include "wisblock_lbm_task.h"
#include "wisblock_radio_hal.h"
#include <Arduino.h>
#include <stdarg.h>
#include <string.h>

#include "lbm/smtc_modem_hal/smtc_modem_hal.h" // vendored at src/lbm/smtc_modem_hal/smtc_modem_hal.h (LBM v4.9.0)

/*
 * This file implements every function smtc_modem_hal.h declares. Grouped to
 * match that header's section comments 1:1 so the two can be read side by
 * side.
 */

// ---------------------------------------------------------------------------
// Software timer (smtc_modem_hal_start_timer / stop_timer)
// LBM is single-threaded on this port: no RTOS tick to hang the timer off,
// so we track a deadline in millis() and fire it from tick(), called every
// loop(). This is the same mechanism smtc_modem_run_engine()'s returned
// "next call me in N ms" sleep budget rides on.
// ---------------------------------------------------------------------------
namespace
{
void (*timerCallback)(void *) = nullptr;
void *timerContext = nullptr;
uint32_t timerDeadlineMs = 0;
bool timerActive = false;

// ---------------------------------------------------------------------------
// Radio IRQ (smtc_modem_hal_irq_config_radio_irq) - LBM registers exactly one
// callback+context here and expects it invoked from the DIO1 ISR.
//
// LoRa P2P mode never calls smtc_modem_init(), so radioIrqCallback is never
// set in a pure-P2P build - LoRaP2PEngine can't rely on it. Instead the ISR
// also raises a simple flag (radioIrqFlag) that P2P mode polls via
// WisBlockLbmPort::consumeRadioIrqFlag(), independent of whichever work
// mode is active. Both paths are driven from the same physical DIO1 edge;
// only one of them is actually consumed at a time depending on work mode.
// ---------------------------------------------------------------------------
void (*radioIrqCallback)(void *) = nullptr;
void *radioIrqContext = nullptr;
volatile bool radioIrqEnabled = true;
volatile bool radioIrqFlag = false;

/**
 * @brief Interrupt handler of the radio DIO1 line
 */
void onDio1Rising()
{
	radioIrqFlag = true;
	if (radioIrqEnabled && radioIrqCallback != nullptr)
	{
		radioIrqCallback(radioIrqContext);
	}
	WisBlockLbmTask::notifyFromISR(); // no-op if background task mode isn't active
}

// ---------------------------------------------------------------------------
// Context store (smtc_modem_hal_context_restore/store/flash_pages_erase)
// LBM keeps several independent contexts (see modem_context_type_t). None of
// them are large except CONTEXT_STORE_AND_FORWARD (a queued-uplink ring
// buffer, only relevant if that optional service is compiled in). This port
// keeps a fixed-size RAM shadow per context, lazily loaded from
// WisBlockLoRaFlash and written straight back through on every store() -
// simple and correct for CONTEXT_MODEM / CONTEXT_KEY_MODEM /
// CONTEXT_LORAWAN_STACK / CONTEXT_FUOTA / CONTEXT_SECURE_ELEMENT, which are
// all comfortably under a few hundred bytes in practice.
//
// TODO: if you enable the Store-and-Forward service, CONTEXT_STORE_AND_FORWARD
// needs real paged flash semantics (erase-before-write, multi-page rotation)
// instead of this shadow-buffer approach - bump kContextBlobSize way up and
// implement smtc_modem_hal_context_flash_pages_erase() against actual flash
// page boundaries, or just leave Store-and-Forward disabled (it's off by
// default and wasn't part of the original spec for this library).
// ---------------------------------------------------------------------------
constexpr size_t kContextBlobSize = 512; // per context_type; raise if LBM ever reports overflow via panic
constexpr int kContextTypeCount = 6;	  // matches modem_context_type_t in smtc_modem_hal.h
uint8_t contextShadow[kContextTypeCount][kContextBlobSize];
bool contextLoaded[kContextTypeCount] = {false, false, false, false, false, false};

/**
 * @brief Get the flash key under which a modem context is stored
 *
 * @param type Modem context type
 * @return Flash key
 */
const char *contextKey(modem_context_type_t type)
{
	static const char *keys[kContextTypeCount] = {"wb_lbm_0", "wb_lbm_1", "wb_lbm_2",
												   "wb_lbm_3", "wb_lbm_4", "wb_lbm_5"};
	return keys[(int)type];
}

/**
 * @brief Load a modem context from flash into its RAM copy if that did not happen yet
 *
 * @param type Modem context type
 */
void ensureContextLoaded(modem_context_type_t type)
{
	int idx = (int)type;
	if (!contextLoaded[idx])
	{
		if (!WisBlockLoRaFlash::read(contextKey(type), contextShadow[idx], kContextBlobSize))
		{
			memset(contextShadow[idx], 0xFF, kContextBlobSize); // erased-flash convention
		}
		contextLoaded[idx] = true;
	}
}
} // namespace

namespace WisBlockLbmPort
{
void init()
{
	WisBlockLoRaFlash::init();
	// FIX (was the direct cause of "background task wakes constantly, high
	// idle current"): a bare `INPUT` pinMode leaves DIO1 floating between
	// SX1262 boot and its first real configuration (and, worse, would also
	// float any time an external RF-switch/antenna-power rail was
	// transitioning - see wisblock_radio_hal_*.cpp's antenna power
	// handling). A floating input drifts/couples noise across the digital
	// threshold randomly, firing spurious RISING edges - each one calls
	// WisBlockLbmTask::notifyFromISR(), which yanks the background task out
	// of tickless idle every time. INPUT_PULLDOWN holds the line
	// deterministically low (matching the SX1262's own idle/no-IRQ level on
	// DIO1) until the radio itself genuinely drives it high, so the task
	// only wakes for real IRQs. Confirmed supported pinMode value on all
	// three cores this library targets (Adafruit nRF52, ESP32 Arduino,
	// arduino-pico).
	// NOTE: reads WisBlockRadioHal::dio1Pin() (the active WisBlockLoRaHwConfig's
	// pinDio1), not WisBlockLoRaBoards.h's compile-time LORA_DIO1 macro -
	// that macro only matches the three built-in RAKwireless presets, and
	// using it unconditionally here left a custom board passed to
	// WisBlockLoRaWAN::begin(const WisBlockLoRaHwConfig&) (e.g. RAK3401,
	// whose real DIO1 pin is 10, not RAK4631's 47) with its DIO1 interrupt
	// silently attached to the wrong physical pin: the join's TX itself
	// still went out fine (that part doesn't depend on DIO1), but the
	// TX-done IRQ the radio raises afterward never reached this ISR, so
	// radio_planner never heard back and eventually hit its own failsafe
	// timeout ("[LBM PANIC] rp_callback:441 RP_FAILSAFE") instead of
	// opening RX1 - see the Creation Log entry "DIO1 interrupt hardcoded to
	// the wrong pin for custom hw_config boards" for the full story. This
	// requires WisBlockRadioHal::init() to have already run - see the
	// ordering note on WisBlockLoRaWAN::begin()'s call sequence.
#if defined(ARDUINO_ARCH_RP2040)
	// Arduino-Pico's random() is rand() % n and is never seeded by the core: every device would
	// get the same sequence after every reset (LBM draws channels, back-offs and jitter from it).
	randomSeed(rp2040.hwrand32());
#endif
	int8_t dio1Pin = WisBlockRadioHal::dio1Pin();
	pinMode(dio1Pin, INPUT_PULLDOWN);
	attachInterrupt(digitalPinToInterrupt(dio1Pin), onDio1Rising, RISING);
}

void tick()
{
	if (timerActive && (int32_t)(millis() - timerDeadlineMs) >= 0)
	{
		timerActive = false;
		if (timerCallback)
		{
			timerCallback(timerContext);
		}
	}
}

bool radioIrqPending()
{
	return radioIrqFlag;
}

bool consumeRadioIrqFlag()
{
	if (radioIrqFlag)
	{
		radioIrqFlag = false;
		return true;
	}
	return false;
}
} // namespace WisBlockLbmPort

extern "C"
{
	// --- Reset management ---------------------------------------------
	/**
	 * @brief Reset the MCU (modem HAL, see smtc_modem_hal.h)
	 */
	void smtc_modem_hal_reset_mcu(void)
	{
#if defined(ARDUINO_ARCH_NRF52)
		NVIC_SystemReset();
#elif defined(ARDUINO_ARCH_ESP32)
		ESP.restart();
#elif defined(ARDUINO_ARCH_RP2040)
		rp2040.reboot();
#endif
	}

	// --- Watchdog management ---------------------------------------------
	/**
	 * @brief Reload the watchdog, not used in this port
	 */
	void smtc_modem_hal_reload_wdog(void)
	{
		// TODO: hook up each platform's watchdog if you enable one:
		// nRF52: NRF_WDT->RR[0] = WDT_RR_RR_Reload;
		// ESP32: esp_task_wdt_reset();
		// RP2040: rp2040.wdt_begin(ms) once, then rp2040.wdt_reset();
	}

	// --- Time management ---------------------------------------------
	/**
	 * @brief Get the time since boot
	 * @return Time in seconds
	 */
	uint32_t smtc_modem_hal_get_time_in_s(void)
	{
		return millis() / 1000;
	}

	/**
	 * @brief Get the time since boot
	 * @return Time in milliseconds
	 */
	uint32_t smtc_modem_hal_get_time_in_ms(void)
	{
		return millis();
	}

	/**
	 * @brief Set an offset for the time functions to test the counter wrapping
	 *
	 * @param offset_to_test_wrapping Offset in seconds
	 */
	void smtc_modem_hal_set_offset_to_test_wrapping(const uint32_t offset_to_test_wrapping)
	{
		// Debug-only hook LBM uses to test millis() wraparound handling.
		// Not wired up; only matters if you're specifically testing the
		// 49-day wraparound path.
		(void)offset_to_test_wrapping;
	}

	// --- Timer management ---------------------------------------------
	/**
	 * @brief Start the modem software timer
	 *
	 * When WisBlockLbmTask is active (background task mode - see
	 * WisBlockLoRaWAN::enableBackgroundTask()), timers are scheduled via a
	 * real FreeRTOS software timer instead of the millis()-polled fallback
	 * below, so LBM's own retransmission/join-backoff scheduling keeps
	 * working with zero loop() polling required.
	 *
	 * @param milliseconds Time until the callback is called
	 * @param callback Function called when the timer expires
	 * @param context Context passed to the callback
	 */
	void smtc_modem_hal_start_timer(const uint32_t milliseconds, void (*callback)(void *context), void *context)
	{
		if (WisBlockLbmTask::isActive())
		{
			WisBlockLbmTask::scheduleTimer(milliseconds, callback, context);
			return;
		}
		timerCallback = callback;
		timerContext = context;
		timerDeadlineMs = millis() + milliseconds;
		timerActive = true;
	}

	/**
	 * @brief Stop the modem software timer
	 */
	void smtc_modem_hal_stop_timer(void)
	{
		if (WisBlockLbmTask::isActive())
		{
			WisBlockLbmTask::cancelTimer();
			return;
		}
		timerActive = false;
	}

	// --- IRQ management ---------------------------------------------
	/**
	 * @brief Disable the interrupts that call into the modem
	 */
	void smtc_modem_hal_disable_modem_irq(void)
	{
		radioIrqEnabled = false;
		noInterrupts();
	}

	/**
	 * @brief Enable the interrupts that call into the modem
	 */
	void smtc_modem_hal_enable_modem_irq(void)
	{
		interrupts();
		radioIrqEnabled = true;
	}

	// --- Context saving management ---------------------------------------------
	/**
	 * @brief Read a modem context from flash
	 *
	 * @param ctx_type Modem context type
	 * @param offset Offset in the context
	 * @param buffer Receives the data
	 * @param size Number of bytes
	 */
	void smtc_modem_hal_context_restore(const modem_context_type_t ctx_type, uint32_t offset, uint8_t *buffer,
										 const uint32_t size)
	{
		ensureContextLoaded(ctx_type);
		if (offset + size > kContextBlobSize)
		{
			// See kContextBlobSize TODO above - only expected to trip for
			// CONTEXT_STORE_AND_FORWARD if that service is enabled.
			memset(buffer, 0xFF, size);
			return;
		}
		memcpy(buffer, contextShadow[(int)ctx_type] + offset, size);
	}

	/**
	 * @brief Write a modem context to flash
	 *
	 * @param ctx_type Modem context type
	 * @param offset Offset in the context
	 * @param buffer Data to store
	 * @param size Number of bytes
	 */
	void smtc_modem_hal_context_store(const modem_context_type_t ctx_type, uint32_t offset, const uint8_t *buffer,
									   const uint32_t size)
	{
		ensureContextLoaded(ctx_type);
		if (offset + size > kContextBlobSize)
		{
			return; // see TODO above
		}
		memcpy(contextShadow[(int)ctx_type] + offset, buffer, size);
		WisBlockLoRaFlash::write(contextKey(ctx_type), contextShadow[(int)ctx_type], kContextBlobSize);
	}

	/**
	 * @brief Erase flash pages of a modem context
	 *
	 * @param ctx_type Modem context type
	 * @param offset Offset in the context
	 * @param nb_page Number of pages
	 */
	void smtc_modem_hal_context_flash_pages_erase(const modem_context_type_t ctx_type, uint32_t offset,
												   uint8_t nb_page)
	{
		// Only used by CONTEXT_STORE_AND_FORWARD in stock LBM. This resets
		// our whole shadow region for that context to the erased-flash
		// convention (0xFF) rather than doing real per-page erase, which is
		// fine as long as Store-and-Forward stays disabled (see TODO above).
		(void)offset;
		(void)nb_page;
		int idx = (int)ctx_type;
		memset(contextShadow[idx], 0xFF, kContextBlobSize);
		contextLoaded[idx] = true;
		WisBlockLoRaFlash::write(contextKey(ctx_type), contextShadow[idx], kContextBlobSize);
	}

	// --- Panic management ---------------------------------------------
	/**
	 * @brief Handle a fatal error of the modem: print it and reset the MCU
	 *
	 * @param func Name of the function that reported the error
	 * @param line Line number
	 * @param fmt Printf style format of the message
	 */
	void smtc_modem_hal_on_panic(uint8_t *func, uint32_t line, const char *fmt, ...)
	{
		Serial.print("[LBM PANIC] ");
		Serial.print((const char *)func);
		Serial.print(":");
		Serial.println(line);

		char msg[160];
		va_list args;
		va_start(args, fmt);
		vsnprintf(msg, sizeof(msg), fmt, args);
		va_end(args);
		Serial.println(msg);

		// TODO: decide your own recovery policy. Stock LBM examples spin
		// forever here so a watchdog resets the board; that's the safest
		// default for a field device, so it's what's implemented, but you
		// may prefer smtc_modem_hal_reset_mcu() immediately instead.
		while (true)
		{
			delay(1000);
		}
	}

	// --- Random management ---------------------------------------------
	/**
	 * @brief Get a random number
	 *
	 * @param val_1 Lower limit
	 * @param val_2 Upper limit
	 * @return Random number between the limits
	 */
	uint32_t smtc_modem_hal_get_random_nb_in_range(const uint32_t val_1, const uint32_t val_2)
	{
		uint32_t lo = val_1 < val_2 ? val_1 : val_2;
		uint32_t hi = val_1 < val_2 ? val_2 : val_1;
		return lo + (uint32_t)random(0, (long)(hi - lo) + 1);
	}

	// --- Radio env management ---------------------------------------------
	/**
	 * @brief Register the callback for the radio interrupt
	 *
	 * @param callback Function called when the radio interrupt fires
	 * @param context Context passed to the callback
	 */
	void smtc_modem_hal_irq_config_radio_irq(void (*callback)(void *context), void *context)
	{
		radioIrqCallback = callback;
		radioIrqContext = context;
	}

	/**
	 * @brief Tell if another stack uses the radio
	 * @return Always false, the radio is only used by the modem
	 */
	bool smtc_modem_external_stack_currently_use_radio(void)
	{
		// No other stack (e.g. a separate BLE/802.15.4 radio driver) shares
		// this SX1262, so always report the radio as free to LBM.
		return false;
	}

	/**
	 * @brief Start the radio TCXO
	 */
	void smtc_modem_hal_start_radio_tcxo(void)
	{
		// The RAK4631/RAK3312/RAK11310 SX1262 modules use a TCXO fed
		// directly from the module's own regulator (DIO3-controlled inside
		// the SX1262 itself per RAK's schematic), not an MCU-switched
		// supply rail - so there is nothing to enable here. If your
		// specific board revision gates the TCXO from a GPIO instead,
		// drive it high here.
	}

	/**
	 * @brief Stop the radio TCXO
	 */
	void smtc_modem_hal_stop_radio_tcxo(void)
	{
		// See smtc_modem_hal_start_radio_tcxo() above.
	}

	/**
	 * @brief Get the start-up time of the radio TCXO and antenna switch power
	 * @return Delay in ms
	 */
	uint32_t smtc_modem_hal_get_radio_tcxo_startup_delay_ms(void)
	{
		// FIX (root cause of every LoRaWAN join that sent a real Join
		// Request but timed out on both RX1 and RX2, confirmed step by step
		// via WISBLOCK_RADIO_HAL_DEBUG tracing - see the README's "LoRaWAN
		// RX1/RX2 window missed" note for the full story): this returned a
		// bare 5ms, treated as this board's confirmed TCXO startup time.
		// It wasn't - LoRaP2PEngine.cpp's own sx126x_set_dio3_as_tcxo_ctrl()
		// call configures 50ms for the exact same physical TCXO on the same
		// board, and P2P mode's TX/RX have been working reliably at that
		// value. 5ms was simply too optimistic for this hardware.
		//
		// radio_planner uses this value to decide how far ahead of a
		// scheduled RX/TX window to issue the radio command, so that the
		// chip's own internal TCXO/PLL ramp-up finishes exactly as the
		// window is meant to open. Under-reporting it doesn't delay when
		// the command is *issued* - radio_planner still logged "RX1 LoRa at
		// 8335 ms" and our HAL issued SetRx at that exact instant, right on
		// schedule - it delays when the radio is actually *ready*, pushing
		// real reception readiness to several ms after the network's
		// precisely-timed downlink had already started. Standard SX126x
		// LoRaWAN receive strategy also arms a short symbol-count preamble
		// check (SetLoRaSymbNumTimeout, ~6-8 symbols here) ahead of the
		// full RX1/RX2 window as a normal power-saving measure - and with
		// the window opening late, that short check was already over
		// before the (very real, on-time) downlink's preamble ever arrived,
		// so it kept firing IRQ_TIMEOUT with nothing detected, well within
		// the 3-second window LBM had genuinely budgeted.
		//
		// The base 40ms is a deliberately conservative middle ground -
		// meaningfully closer to P2P's proven-working 50ms than the
		// original wrong 5ms - not a measured value; if RX1/RX2 are still
		// marginal after this, that's the number to tune first.
		constexpr uint32_t kBaseTcxoStartupMs = 40;
		return kBaseTcxoStartupMs + WisBlockRadioHal::antennaPowerSettleMs();
	}

	/**
	 * @brief Switch the antenna switch between TX and RX, not used because the radio drives the switch
	 *
	 * @param is_tx_on true when transmitting
	 */
	void smtc_modem_hal_set_ant_switch(bool is_tx_on)
	{
		// SX1262 drives its own RF switch via DIO2 (useDio2AntSwitch in the
		// active WisBlockLoRaHwConfig, true for all four built-in presets)
		// via the radio driver's SetDio2AsRfSwitchCtrl - not something the
		// MCU toggles per TX/RX here. If a board revision instead wires an
		// MCU GPIO to the antenna switch, drive it from is_tx_on here.
		(void)is_tx_on;
	}

	// --- Environment management ---------------------------------------------
	/**
	 * @brief Get the battery level
	 * @return 255 (unknown), not measured in this port
	 */
	uint8_t smtc_modem_hal_get_battery_level(void)
	{
		// 0 = mains-powered, 1..254 = battery level, 255 = unknown.
		// TODO: wire to a real fuel gauge / resistor-divider ADC read if
		// your application is battery powered; 255 is a safe placeholder
		// that tells the network "not measured" rather than lying with a
		// fixed value.
		return 255;
	}

	/**
	 * @brief Get the board specific delay that is added to the RX window timing
	 * @return Delay in ms
	 */
	int8_t smtc_modem_hal_get_board_delay_ms(void)
	{
		// Extra fixed delay LBM adds to its RX window timing to compensate
		// for board-specific latency (SPI, IRQ, MCU wakeup). 1ms is a
		// reasonable starting point for these three targets; tighten with
		// a logic analyzer against DIO1 if you see RX window timing drift.
		return 1;
	}

	// --- Trace management ---------------------------------------------
	/**
	 * @brief Print a trace message of the modem to the serial port
	 *
	 * @param fmt Printf style format of the message
	 */
	void smtc_modem_hal_print_trace(const char *fmt, ...)
	{
		char msg[192];
		va_list args;
		va_start(args, fmt);
		vsnprintf(msg, sizeof(msg), fmt, args);
		va_end(args);
		Serial.print(msg);
	}

	// --- Fuota management (only exercised if FMP package is enabled) ---------------------------------------------
	/**
	 * @brief FUOTA hardware version, not used
	 * @return 0
	 */
	uint32_t smtc_modem_hal_get_hw_version_for_fuota(void) { return 0; }
	/**
	 * @brief FUOTA firmware version, not used
	 * @return 0
	 */
	uint32_t smtc_modem_hal_get_fw_version_for_fuota(void) { return 0; }
	/**
	 * @brief FUOTA firmware status, not used
	 * @return 0
	 */
	uint8_t smtc_modem_hal_get_fw_status_available_for_fuota(void) { return 0; }
	/**
	 * @brief FUOTA firmware delete status, not used
	 *
	 * @param fw_to_delete_version Firmware version to delete
	 * @return 0
	 */
	uint8_t smtc_modem_hal_get_fw_delete_status_for_fuota(uint32_t fw_to_delete_version)
	{
		(void)fw_to_delete_version;
		return 0;
	}
	/**
	 * @brief FUOTA next firmware version, not used
	 * @return 0
	 */
	uint32_t smtc_modem_hal_get_next_fw_version_for_fuota(void) { return 0; }

	// --- Needed for Device Management ---------------------------------------------
	/**
	 * @brief Get the temperature for the device management
	 * @return Temperature in degree Celsius, fixed at 25
	 */
	int8_t smtc_modem_hal_get_temperature(void)
	{
		// TODO: wire to the SX1262's internal temperature sensor
		// (accessible via the radio driver) or an on-board sensor if
		// present; 25 is a placeholder room-temperature default.
		return 25;
	}

	/**
	 * @brief Get the supply voltage for the device management
	 * @return Voltage in mV, fixed at 3300
	 */
	uint16_t smtc_modem_hal_get_voltage_mv(void)
	{
		// TODO: wire to an actual VBAT ADC read; RAK4631/3312/11310 all
		// expose a battery-voltage-sense pin on the WisBlock base board
		// (separate from the core module pins WisBlockLoRaHwConfig
		// describes). A custom board would need its own ADC pin/scaling
		// here too - not something WisBlockLoRaHwConfig currently covers.
		return 3300;
	}

	/**
	 * @brief Store a crash log, not used
	 *
	 * @param crash_string Crash text
	 * @param crash_string_length Length of the text
	 */
	void smtc_modem_hal_crashlog_store(const uint8_t *crash_string, uint8_t crash_string_length)
	{
		// WisBlockLoRaFlash::write("wb_lbm_crash", crash_string, crash_string_length);
	}

	/**
	 * @brief Restore a crash log, not used
	 *
	 * @param crash_string Receives the crash text
	 * @param crash_string_length Receives the length of the text
	 */
	void smtc_modem_hal_crashlog_restore(uint8_t *crash_string, uint8_t *crash_string_length)
	{
		// CRASH_LOG_SIZE is defined in smtc_modem_hal.h (242 bytes).
		// bool ok = WisBlockLoRaFlash::read("wb_lbm_crash", crash_string, CRASH_LOG_SIZE);
		// *crash_string_length = ok ? CRASH_LOG_SIZE : 0;
	}

	/**
	 * @brief Set the crash log status
	 *
	 * @param available true if a crash log is available
	 */
	void smtc_modem_hal_crashlog_set_status(bool available)
	{
		// uint8_t flag = available ? 1 : 0;
		// WisBlockLoRaFlash::write("wb_lbm_crash_flag", &flag, 1);
	}

	/**
	 * @brief Get the crash log status
	 * @return true if a crash log is available
	 */
	bool smtc_modem_hal_crashlog_get_status(void)
	{
		uint8_t flag = 0;
		// WisBlockLoRaFlash::read("wb_lbm_crash_flag", &flag, 1);
		return flag != 0;
	}

	// --- Needed for Store and Forward service ---------------------------------------------
	/**
	 * @brief Number of flash pages for the store and forward service, not used
	 * @return 3
	 */
	uint16_t smtc_modem_hal_store_and_forward_get_number_of_pages(void)
	{
		// Only consulted if the Store-and-Forward service is compiled in
		// (it isn't, by default, in this port - see the context-store TODO
		// above). 3 is LBM's documented minimum.
		return 3;
	}

	/**
	 * @brief Flash page size for the store and forward service, not used
	 * @return Page size in bytes
	 */
	uint16_t smtc_modem_hal_flash_get_page_size(void)
	{
		// Nominal flash page size; only meaningful alongside real
		// Store-and-Forward paging support, which this port doesn't
		// implement yet.
		return 4096;
	}

	// --- For Real Time OS compatibility ---------------------------------------------
	/**
	 * @brief Called by the modem when it needs the application to run it, not used
	 */
	void smtc_modem_hal_user_lbm_irq(void)
	{
		// LBM calls this whenever it wants to notify an RTOS-based port
		// that new work is pending (per its own doc comment: "wake the
		// task that calls smtc_modem_run_engine()"). Wakes
		// WisBlockLbmTask's background task when active; no-op otherwise
		// (bare-metal builds rely on the loop()-polled tick()/handleEvents()
		// path instead).
		WisBlockLbmTask::notifyFromISR();
	}
}
