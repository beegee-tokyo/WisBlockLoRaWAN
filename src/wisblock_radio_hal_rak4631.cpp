#include "wisblock_radio_hal.h"

/*
 * RAK4631 (nRF52840) implementation. RAK3312 (ESP32-S3) and RAK11310
 * (RP2040) get their own wisblock_radio_hal_<board>.cpp with the same
 * sx126x_hal_* signatures — only the SPI/GPIO calls underneath differ, so
 * this file is guarded to compile only for the nRF52 target and is meant to
 * be the first of three sibling files.
 */
#if defined(ARDUINO_ARCH_NRF52) || defined(NRF52840_XXAA)

#include "WisBlockLoRaBoards.h"
#include <Arduino.h>
#include <SPI.h>

namespace
{
// SX1262 datasheet timing:
//  - NRESET low pulse: >= 100 us (we use 1 ms for margin on top of Arduino's
//    delay() granularity).
//  - After NRESET release, BUSY goes high almost immediately then low again
//    once the chip has booted (typically < 3.5 ms, cold start can be longer
//    after a fresh flash/POR - 1000 ms timeout below is generous, not tight).
constexpr uint32_t kResetPulseUs = 1000;
constexpr uint32_t kBusyTimeoutMs = 1000;

// RF-switch/antenna-power settle time: how long to wait after driving
// LORA_ANT_PWR high before trusting the SPI bus / BUSY line again. 1 ms is a
// conservative default for a simple GPIO-driven load switch or LDO enable
// pin; TODO: verify against your specific WisBlock module revision - tighten
// with a scope on LORA_ANT_PWR vs. the first successful post-wake SPI
// transaction if you need faster wake latency than this costs you.
constexpr uint32_t kAntPwrSettleUs = 1000;

WisBlockRadioContext wisblockRadioContext = {
	.pinNss = LORA_SPI_NSS,
	.pinReset = LORA_RESET,
	.pinBusy = LORA_BUSY,
	.pinDio1 = LORA_DIO1,
	.pinAntPwr = LORA_ANT_PWR,
	.spiHz = 8000000UL, // SX1262 SPI max is 16 MHz; 8 MHz is a safe default over WisBlock header traces
};

SPISettings spiSettings(8000000UL, MSBFIRST, SPI_MODE0);

// --- Sleep-state tracking -------------------------------------------------
// CRITICAL, per Semtech's own reference sx126x_hal.c (lbm_applications/
// 2_porting_nrf_52840/radio_hal/sx126x_hal.c in the upstream SWL2001 repo):
// "Busy is HIGH in sleep mode" - while the chip is genuinely asleep, BUSY
// does NOT read low the way it does during normal idle/standby. It only
// clears once you pull NSS low to initiate wake, and that wake pulse is a
// *different* sequence from a normal transaction's busy-wait.
//
// A plain sx126x_hal_write()/read() that just waits for BUSY to go low
// before starting (as if the chip were merely idle, not asleep) will
// deadlock waiting for a transition that can only happen after an NSS pulse
// it never sends - exactly the bug this replaces. This library never
// explicitly puts the radio to sleep itself, but LBM's own radio_planner
// does (ral_set_sleep(), see src/lbm/smtc_modem_core/radio_planner/src/
// radio_planner.c), autonomously, between scheduled tasks - so this HAL
// must track that state regardless of what higher-level code in this
// library does or doesn't do.
enum class RadioMode
{
	Awake,
	Asleep,
};
RadioMode radioMode = RadioMode::Awake;

// --- RF-switch power tracking ---------------------------------------------
// FIX: LORA_ANT_PWR was previously driven HIGH once in init() and never
// touched again - it stayed powered for the entire time the radio was
// "asleep", which quietly defeated a large part of the point of putting the
// SX1262 in SLEEP mode at all (the RF switch / antenna front-end supply it
// feeds has its own non-trivial quiescent draw). It's now tied directly to
// the same Awake/Asleep tracking this file already has to do for BUSY's
// sleep-mode behavior, so every caller that reaches SLEEP - LBM's
// radio_planner in LoRaWAN mode, or LoRaP2PEngine::sleep() in P2P mode -
// gets the antenna-power savings automatically, with no call-site changes
// needed anywhere else in the library.
// ---------------------------------------------------------------------------

// Matches Semtech's sx126x_hal_check_device_ready(): normal case just waits
// for BUSY (assumed already low or clearing quickly); asleep case restores
// antenna power (and gives it kAntPwrSettleUs to stabilize) before issuing
// the special NSS-pulse wake sequence.
void checkDeviceReady()
{
	if (radioMode != RadioMode::Asleep)
	{
		WisBlockRadioHal::waitOnBusy(kBusyTimeoutMs);
		return;
	}

#ifdef WISBLOCK_RADIO_HAL_DEBUG
	// Opt-in trace (define WISBLOCK_RADIO_HAL_DEBUG before including any
	// library header, e.g. at the top of main.h) for correlating exactly
	// when a sleep->wake transition happens against LBM's own
	// MODEM_HAL_DBG_TRACE output (radio_planner's "Open Rx1/Rx2" lines) -
	// useful for narrowing down a missed RX1/RX2 window: does the wake
	// happen before radio_planner's target time, or after?
#ifdef WISBLOCK_RADIO_HAL_KEEP_ANT_PWR_ALWAYS_ON
	// Bisection toggle: define this (alongside WISBLOCK_RADIO_HAL_DEBUG or
	// alone) to leave LORA_ANT_PWR permanently high and skip the settle
	// delay entirely, isolating whether the antenna-power rail is actually
	// the dominant factor in whatever residual current or timing issue
	// you're chasing. Revert once you're done - this defeats the RF-switch
	// power savings documented earlier in this file.
	Serial.printf("[wake] t=%lu ms (ant pwr forced always-on, settle skipped)\n", millis());
#else
	digitalWrite(wisblockRadioContext.pinAntPwr, HIGH);
	delayMicroseconds(kAntPwrSettleUs);
	Serial.printf("[wake] t=%lu ms (ant pwr restored, %lu us settle)\n", millis(), (unsigned long)kAntPwrSettleUs);
#endif
#else
#ifdef WISBLOCK_RADIO_HAL_KEEP_ANT_PWR_ALWAYS_ON
	// See the WISBLOCK_RADIO_HAL_DEBUG branch above for what this does.
#else
	digitalWrite(wisblockRadioContext.pinAntPwr, HIGH);
	delayMicroseconds(kAntPwrSettleUs);
#endif
#endif

#ifndef WISBLOCK_RADIO_HAL_KEEP_SPI_ALWAYS_ON
	// Re-enable the SPIM peripheral disabled in the SetSleep branch above -
	// see the comment there. setPins() again because SPI.end() may reset
	// pin/peripheral state that begin() alone doesn't restore.
	SPI.setPins(LORA_SPI_MISO, LORA_SPI_SCK, LORA_SPI_MOSI);
	SPI.begin();
#endif

	SPI.beginTransaction(spiSettings);
	digitalWrite(wisblockRadioContext.pinNss, LOW);
	WisBlockRadioHal::waitOnBusy(kBusyTimeoutMs);
	digitalWrite(wisblockRadioContext.pinNss, HIGH);
	SPI.endTransaction();
	radioMode = RadioMode::Awake;
}
} // namespace

namespace WisBlockRadioHal
{
void init()
{
	pinMode(wisblockRadioContext.pinNss, OUTPUT);
	digitalWrite(wisblockRadioContext.pinNss, HIGH);

	pinMode(wisblockRadioContext.pinReset, OUTPUT);
	digitalWrite(wisblockRadioContext.pinReset, HIGH);

	pinMode(wisblockRadioContext.pinBusy, INPUT);
	// DIO1 pinMode/attachInterrupt is handled in wisblock_lbm_port.cpp,
	// since it's shared wake/IRQ infrastructure rather than pure SPI glue.

	pinMode(wisblockRadioContext.pinAntPwr, OUTPUT);
	// Powered on for the initial reset/probe below; from here on this
	// pin's state is tracked automatically alongside RadioMode (see
	// "RF-switch power tracking" note above) - off while the radio is
	// asleep, on otherwise.
	digitalWrite(wisblockRadioContext.pinAntPwr, HIGH);

#if defined(ARDUINO_ARCH_NRF52)
	// Adafruit nRF52 core: if your wiring doesn't match the SPI peripheral's
	// default pins, remap before begin(). Comment out if LORA_SPI_* already
	// matches the board's default SPI pinout.
	SPI.setPins(LORA_SPI_MISO, LORA_SPI_SCK, LORA_SPI_MOSI);
#endif
	SPI.begin();

	sx126x_hal_reset(nullptr); // see NOTE below: this port ignores the context arg entirely
}

const void *context()
{
	return &wisblockRadioContext;
}

bool isBusy()
{
	return digitalRead(wisblockRadioContext.pinBusy) == HIGH;
}

bool waitOnBusy(uint32_t timeoutMs)
{
	uint32_t start = millis();
	while (digitalRead(wisblockRadioContext.pinBusy) == HIGH)
	{
		if (millis() - start > timeoutMs)
		{
			return false;
		}
	}
	return true;
}

void setAntennaPower(bool on)
{
	digitalWrite(wisblockRadioContext.pinAntPwr, on ? HIGH : LOW);
	if (on)
	{
		delayMicroseconds(kAntPwrSettleUs);
	}
}

bool isAsleep()
{
	return radioMode == RadioMode::Asleep;
}

uint32_t antennaPowerSettleMs()
{
	// Round up: reporting less than the real delay is what caused the
	// RX1/RX2 miss this function exists to fix; reporting a little more
	// than necessary just costs a touch of extra wake-ahead margin.
	return (kAntPwrSettleUs + 999) / 1000;
}
} // namespace WisBlockRadioHal

extern "C"
{
	sx126x_hal_status_t sx126x_hal_reset(const void *context)
	{
		// NOTE: LBM instantiates its radio as RALF_SX126X_INSTANTIATE(NULL)
		// (see src/lbm/smtc_modem_core/smtc_modem.c), so `context` is always
		// NULL when LBM itself calls into this function - never dereference
		// it. This port only supports one radio, so it always operates on
		// the static wisblockRadioContext above instead of trusting the
		// incoming pointer.
		(void)context;

		digitalWrite(wisblockRadioContext.pinReset, LOW);
		delayMicroseconds(kResetPulseUs);
		digitalWrite(wisblockRadioContext.pinReset, HIGH);
		radioMode = RadioMode::Awake;

		// After reset release the chip re-runs its boot sequence; BUSY stays
		// high until it's ready to accept commands.
		if (!WisBlockRadioHal::waitOnBusy(kBusyTimeoutMs))
		{
			return SX126X_HAL_STATUS_ERROR;
		}
		return SX126X_HAL_STATUS_OK;
	}

	sx126x_hal_status_t sx126x_hal_wakeup(const void *context)
	{
		(void)context; // see NOTE in sx126x_hal_reset() above
		checkDeviceReady();
		return SX126X_HAL_STATUS_OK;
	}

	sx126x_hal_status_t sx126x_hal_write(const void *context, const uint8_t *command,
										  const uint16_t command_length, const uint8_t *data,
										  const uint16_t data_length)
	{
		(void)context; // see NOTE in sx126x_hal_reset() above

		checkDeviceReady();

		SPI.beginTransaction(spiSettings);
		digitalWrite(wisblockRadioContext.pinNss, LOW);

		for (uint16_t i = 0; i < command_length; i++)
		{
			SPI.transfer(command[i]);
		}
		for (uint16_t i = 0; i < data_length; i++)
		{
			SPI.transfer(data[i]);
		}

		digitalWrite(wisblockRadioContext.pinNss, HIGH);
		SPI.endTransaction();

#ifdef WISBLOCK_RADIO_HAL_DEBUG
		// SetLoRaSymbNumTimeout opcode (0xA0): a SEPARATE, usually much
		// shorter "give up if not even a preamble shows up within N
		// symbols" timeout, distinct from SetRx's overall window - both
		// conditions set the exact same IRQ_TIMEOUT bit (0x0200), so a
		// short chip-reported timeout can come from either one. The
		// parameter is mantissa/exponent-encoded per the datasheet, not a
		// direct symbol count - 0x00 means disabled (SetRx's own timeout is
		// the only one armed); any nonzero value means this shorter timeout
		// is also armed and is a real candidate for what's actually firing.
		if (command_length == 2 && command[0] == 0xA0 /* SX126x SetLoRaSymbNumTimeout opcode */)
		{
			Serial.printf("[symb timeout] t=%lu ms: raw=0x%02X\n", millis(), command[1]);
		}

		// SetRx opcode (0x82): print the raw 24-bit RTC-step timeout LBM
		// actually sent, decoded back to ms (1 step = 15.625us, i.e. /64.0)
		// - to check directly whether the hardware timeout parameter itself
		// is short, versus the chip's own IRQ_TIMEOUT (0x0200, see the
		// GetIrqStatus trace above) firing correctly against a short value
		// LBM legitimately intended.
		if (command_length == 4 && command[0] == 0x82 /* SX126x SetRx opcode */)
		{
			uint32_t rtcSteps = ((uint32_t)command[1] << 16) | ((uint32_t)command[2] << 8) | (uint32_t)command[3];
			Serial.printf("[set rx] t=%lu ms: %lu rtc steps (%.1f ms)\n", millis(),
						  (unsigned long)rtcSteps, rtcSteps / 64.0);
		}
#endif

		// SetSleep (opcode 0x84): BUSY reads HIGH throughout sleep (see the
		// RadioMode note above) - don't wait on it, just record that the
		// chip is now asleep so the *next* transaction knows to use the
		// wake sequence instead of a normal busy-wait.
		if (command_length > 0 && command[0] == 0x84 /* SX126x SetSleep opcode */)
		{
			radioMode = RadioMode::Asleep;
#ifdef WISBLOCK_RADIO_HAL_DEBUG
			Serial.printf("[sleep] t=%lu ms\n", millis());
#endif
#ifndef WISBLOCK_RADIO_HAL_KEEP_ANT_PWR_ALWAYS_ON
			// Cut the RF-switch/antenna supply now that the radio itself is
			// asleep - see "RF-switch power tracking" note above. Restored
			// automatically by checkDeviceReady() the next time anything
			// wakes the radio.
			digitalWrite(wisblockRadioContext.pinAntPwr, LOW);
#endif
#ifndef WISBLOCK_RADIO_HAL_KEEP_SPI_ALWAYS_ON
			// HYPOTHESIS UNDER TEST (not yet confirmed - see the README's
			// "Persistent idle current floor" note): SPI.begin() enables
			// the underlying SPIM peripheral once in init() and nothing
			// ever calls SPI.end() afterward - beginTransaction()/
			// endTransaction() only arbitrate the bus for a single
			// transaction, they don't power the peripheral down between
			// them. If SPIM has non-trivial idle current simply from being
			// enabled (matching a real report of exactly this for a UART
			// peripheral on the same chip family), that would explain a
			// floor that no amount of radio-sleep or antenna-power
			// correctness could ever touch, since it's a separate MCU
			// peripheral, not the SX1262. Define
			// WISBLOCK_RADIO_HAL_KEEP_SPI_ALWAYS_ON to disable this and
			// A/B test against the previous behavior.
			SPI.end();
#endif
			return SX126X_HAL_STATUS_OK;
		}

		if (!WisBlockRadioHal::waitOnBusy(kBusyTimeoutMs))
		{
			return SX126X_HAL_STATUS_ERROR;
		}
		return SX126X_HAL_STATUS_OK;
	}

	sx126x_hal_status_t sx126x_hal_read(const void *context, const uint8_t *command,
										 const uint16_t command_length, uint8_t *data,
										 const uint16_t data_length)
	{
		(void)context; // see NOTE in sx126x_hal_reset() above

		checkDeviceReady();

		SPI.beginTransaction(spiSettings);
		digitalWrite(wisblockRadioContext.pinNss, LOW);

		for (uint16_t i = 0; i < command_length; i++)
		{
			SPI.transfer(command[i]);
		}
		for (uint16_t i = 0; i < data_length; i++)
		{
			data[i] = SPI.transfer(0x00); // NOP while clocking in the response
		}

		digitalWrite(wisblockRadioContext.pinNss, HIGH);
		SPI.endTransaction();

		if (!WisBlockRadioHal::waitOnBusy(kBusyTimeoutMs))
		{
			return SX126X_HAL_STATUS_ERROR;
		}

#ifdef WISBLOCK_RADIO_HAL_DEBUG
		// GetIrqStatus opcode (0x12): print the raw status bytes LBM/radio_planner
		// just read, so a premature sleep during an RX1/RX2 window can be
		// correlated against what the chip actually reported at that instant
		// (genuine early Timeout vs. something else being misread).
		if (command_length > 0 && command[0] == 0x12 && data_length > 0)
		{
			Serial.printf("[irq status] t=%lu ms:", millis());
			for (uint16_t i = 0; i < data_length; i++)
			{
				Serial.printf(" %02X", data[i]);
			}
			Serial.println();
		}
#endif

		return SX126X_HAL_STATUS_OK;
	}
}

#endif // ARDUINO_ARCH_NRF52
