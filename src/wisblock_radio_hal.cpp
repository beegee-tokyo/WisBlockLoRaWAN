#include "wisblock_radio_hal.h"

/*
 * Merged nRF52840/ESP32-S3 implementation - see this file's own doc comment
 * in wisblock_radio_hal.h ("Board flexibility") for why this replaces the
 * former wisblock_radio_hal_rak4631.cpp/_rak3312.cpp pair. Both boards'
 * logic was identical except for the SPI peripheral init call and a
 * yield()-in-the-busy-wait-loop for ESP32's watchdog - both are isolated to
 * small #if blocks below, everything else is driven by the WisBlockLoRaHwConfig
 * passed to init() instead of compile-time board macros.
 *
 * wisblock_radio_hal_rak11310.cpp (RP2040) is a separate file, unaffected by
 * this merge - see its own doc comment for why.
 */
#if defined(ARDUINO_ARCH_NRF52) || defined(NRF52840_XXAA) || defined(ARDUINO_ARCH_ESP32)

#include <Arduino.h>
#include <SPI.h>

#include "wisblock_radio_bsp_config.h"

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

// RF-switch/antenna-power settle time: how long to wait after driving the
// antenna-power pin(s) high before trusting the SPI bus / BUSY line again.
// 1 ms is a conservative default for a simple GPIO-driven load switch or LDO
// enable pin; TODO: verify against your specific module - tighten with a
// scope on the antenna-power pin vs. the first successful post-wake SPI
// transaction if you need faster wake latency than this costs you.
constexpr uint32_t kAntPwrSettleUs = 1000;

WisBlockLoRaHwConfig activeConfig;
WisBlockLoRaRadioBspConfig activeBspConfig;
SPIClass *activeSpi = nullptr;
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
// Antenna-switch power is tied to the same Awake/Asleep tracking this file
// already has to do for BUSY's sleep-mode behavior, so every caller that
// reaches SLEEP - LBM's radio_planner in LoRaWAN mode, or
// LoRaP2PEngine::sleep() in P2P mode - gets the antenna-power savings
// automatically, with no call-site changes needed anywhere else in the
// library. See WisBlockLoRaHwConfig.h's doc comment for the field
// combinations this covers (useRxenAntPwr and/or useDio3AntSwitch).
// ---------------------------------------------------------------------------

void driveAntennaPower(bool on)
{
	if (activeConfig.useRxenAntPwr && activeConfig.radioRxEn >= 0)
	{
		digitalWrite(activeConfig.radioRxEn, on ? HIGH : LOW);
	}
	// DIO3-as-antenna-switch is a radio-internal function (sx126x_set_dio3_as_ant_switch()
	// equivalent), not a GPIO this HAL drives directly - the driver/BSP layer
	// programs it once at init and the chip's own DIO3 pin then follows the
	// chip's own idle/active state automatically, so there's nothing to
	// toggle here for that case.
}

// If both radioTxEn and radioRxEn are wired as direct RF-switch steering
// lines (not just one of them as antenna-switch *power* - see
// WisBlockLoRaHwConfig.h's doc comment), the driver has to flip them itself
// before every RX or TX, matching SX126x-Arduino's SX126xRXena()/SX126xTXena().
// None of this library's own WisBlock presets need this (they all use
// useRxenAntPwr instead, with DIO2 doing the actual TX/RX steering), but a
// custom eByte E22-style board might.
bool usesManualTxRxSteering()
{
	return !activeConfig.useRxenAntPwr && activeConfig.radioTxEn >= 0 && activeConfig.radioRxEn >= 0;
}

void steerForRx()
{
	if (usesManualTxRxSteering())
	{
		digitalWrite(activeConfig.radioRxEn, HIGH);
		digitalWrite(activeConfig.radioTxEn, LOW);
	}
}

void steerForTx()
{
	if (usesManualTxRxSteering())
	{
		digitalWrite(activeConfig.radioRxEn, LOW);
		digitalWrite(activeConfig.radioTxEn, HIGH);
	}
}

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
	// alone) to leave the antenna-power pin(s) permanently high and skip
	// the settle delay entirely, isolating whether the antenna-power rail
	// is actually the dominant factor in whatever residual current or
	// timing issue you're chasing. Revert once you're done - this defeats
	// the RF-switch power savings documented earlier in this file.
	Serial.printf("[wake] t=%lu ms (ant pwr forced always-on, settle skipped)\n", millis());
#else
	driveAntennaPower(true);
	delayMicroseconds(kAntPwrSettleUs);
	Serial.printf("[wake] t=%lu ms (ant pwr restored, %lu us settle)\n", millis(), (unsigned long)kAntPwrSettleUs);
#endif
#else
#ifdef WISBLOCK_RADIO_HAL_KEEP_ANT_PWR_ALWAYS_ON
	// See the WISBLOCK_RADIO_HAL_DEBUG branch above for what this does.
#else
	driveAntennaPower(true);
	delayMicroseconds(kAntPwrSettleUs);
#endif
#endif

#ifndef WISBLOCK_RADIO_HAL_KEEP_SPI_ALWAYS_ON
	// Re-enable the SPI peripheral disabled in the SetSleep branch below.
#if defined(ARDUINO_ARCH_NRF52) || defined(NRF52840_XXAA)
	if (activeConfig.spiInstance == nullptr)
	{
		// Only remap pins on the global SPI object - a caller-supplied
		// SPIClass instance (e.g. RAK3401's second SPIM peripheral) already
		// fixes its pins in its own constructor.
		activeSpi->setPins(activeConfig.pinMiso, activeConfig.pinSck, activeConfig.pinMosi);
	}
	activeSpi->begin();
#elif defined(ARDUINO_ARCH_ESP32)
	activeSpi->begin(activeConfig.pinSck, activeConfig.pinMiso, activeConfig.pinMosi, activeConfig.pinNss);
#endif
#endif

	activeSpi->beginTransaction(spiSettings);
	digitalWrite(activeConfig.pinNss, LOW);
	WisBlockRadioHal::waitOnBusy(kBusyTimeoutMs);
	digitalWrite(activeConfig.pinNss, HIGH);
	activeSpi->endTransaction();
	radioMode = RadioMode::Awake;
}

void configurePins()
{
	pinMode(activeConfig.pinNss, OUTPUT);
	digitalWrite(activeConfig.pinNss, HIGH);

	pinMode(activeConfig.pinReset, OUTPUT);
	digitalWrite(activeConfig.pinReset, HIGH);

	pinMode(activeConfig.pinBusy, INPUT);
	// DIO1 pinMode/attachInterrupt is handled in wisblock_lbm_port.cpp,
	// since it's shared wake/IRQ infrastructure rather than pure SPI glue.

	// Pin setup mirrors SX126x-Arduino's SX126xIoInit() for the same three
	// RADIO_TXEN/RADIO_RXEN combinations - see WisBlockLoRaHwConfig.h's doc
	// comment.
	if (activeConfig.radioTxEn < 0 && activeConfig.radioRxEn >= 0)
	{
		// Single pin: antenna-switch power (useRxenAntPwr). Powered on here
		// for the initial reset/probe that follows in init(); from then on
		// it's tracked automatically alongside RadioMode (see "RF-switch
		// power tracking" below) - off while the radio is asleep, on
		// otherwise, matching radioMode's own Awake starting state.
		pinMode(activeConfig.radioRxEn, OUTPUT);
		digitalWrite(activeConfig.radioRxEn, HIGH);
	}
	else if (activeConfig.radioTxEn >= 0 && activeConfig.radioRxEn >= 0)
	{
		// Both pins: direct RF-switch steering (usesManualTxRxSteering()).
		// Start in RX direction, matching steerForRx()'s idle state.
		pinMode(activeConfig.radioTxEn, OUTPUT);
		pinMode(activeConfig.radioRxEn, OUTPUT);
		digitalWrite(activeConfig.radioTxEn, LOW);
		digitalWrite(activeConfig.radioRxEn, HIGH);
	}
	// radioTxEn >= 0 && radioRxEn < 0 (TX-only enable, no RX enable at all)
	// isn't a combination any known SX1262 module uses and is left
	// unconfigured, matching the reference implementation.
}

void setActiveConfig(const WisBlockLoRaHwConfig &hwConfig)
{
	activeConfig = hwConfig;
	activeBspConfig.useLdo = hwConfig.useLdo;
	activeBspConfig.dio2AntSwitch = hwConfig.useDio2AntSwitch;
	activeBspConfig.dio3Tcxo = hwConfig.useDio3Tcxo;
	activeBspConfig.tcxoVoltage = hwConfig.tcxoCtrlVoltage;
	// Native driver units are 15.625us steps (1/64 ms) - see
	// ral_sx126x_bsp_get_xosc_cfg()'s doc comment in wisblock_ral_sx126x_bsp.c.
	activeBspConfig.tcxoStartupTimeInTick = (uint32_t)((uint64_t)hwConfig.tcxoStartupTimeUs * 64ULL / 1000ULL);

#if defined(ARDUINO_ARCH_NRF52) || defined(NRF52840_XXAA) || defined(ARDUINO_ARCH_ESP32)
	activeSpi = hwConfig.spiInstance != nullptr ? hwConfig.spiInstance : &SPI;
#endif
	spiSettings = SPISettings(hwConfig.spiHz, MSBFIRST, SPI_MODE0);
}
} // namespace

namespace WisBlockRadioHal
{
void init(const WisBlockLoRaHwConfig &hwConfig)
{
	setActiveConfig(hwConfig);
	configurePins();

#if defined(ARDUINO_ARCH_NRF52) || defined(NRF52840_XXAA)
	if (activeConfig.spiInstance == nullptr)
	{
		// Adafruit nRF52 core: remap the global SPI object's pins before
		// begin() if they don't already match its default pinout. A
		// caller-supplied SPIClass instance is assumed to already be wired
		// to the right pins via its own constructor (see
		// wisblockLoRaHwConfigRAK3401() for a real example) and is left
		// alone here.
		activeSpi->setPins(activeConfig.pinMiso, activeConfig.pinSck, activeConfig.pinMosi);
	}
	activeSpi->begin();
#elif defined(ARDUINO_ARCH_ESP32)
	activeSpi->begin(activeConfig.pinSck, activeConfig.pinMiso, activeConfig.pinMosi, activeConfig.pinNss);
	// NSS is driven manually around each transaction below rather than left
	// to SPI's automatic CS, since the SX126x needs NSS held low across
	// multi-byte command+data phases that a single SPI.transfer() call
	// doesn't span.
	pinMode(activeConfig.pinNss, OUTPUT);
	digitalWrite(activeConfig.pinNss, HIGH);
#endif

	sx126x_hal_reset(nullptr); // see NOTE below: this port ignores the context arg entirely
}

void init()
{
#if defined(ARDUINO_ARCH_NRF52) || defined(NRF52840_XXAA)
	init(wisblockLoRaHwConfigRAK4631());
#elif defined(ARDUINO_ARCH_ESP32)
	init(wisblockLoRaHwConfigRAK3312());
#endif
	// ARDUINO_ARCH_RP2040 (RAK11310) is handled entirely by
	// wisblock_radio_hal_rak11310.cpp's own WisBlockRadioHal::init() - this
	// whole file is not compiled for that target (see the #if guard at the
	// top), so there's no conflicting definition here.
}

const void *context()
{
	return &activeConfig;
}

int8_t dio1Pin()
{
	return activeConfig.pinDio1;
}

bool isBusy()
{
	return digitalRead(activeConfig.pinBusy) == HIGH;
}

bool waitOnBusy(uint32_t timeoutMs)
{
	uint32_t start = millis();
	while (digitalRead(activeConfig.pinBusy) == HIGH)
	{
		if (millis() - start > timeoutMs)
		{
			return false;
		}
#if defined(ARDUINO_ARCH_ESP32)
		// yield() lets the ESP32 core service WiFi/BT/RTOS housekeeping
		// during longer waits (e.g. the post-reset boot wait) instead of
		// starving the idle task and tripping the task watchdog.
		yield();
#endif
	}
	return true;
}

void setAntennaPower(bool on)
{
	driveAntennaPower(on);
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

const WisBlockLoRaRadioBspConfig *wisblock_radio_hal_get_bsp_config(void)
{
	return &activeBspConfig;
}

extern "C"
{
	sx126x_hal_status_t sx126x_hal_reset(const void *context)
	{
		// NOTE: LBM instantiates its radio as RALF_SX126X_INSTANTIATE(NULL)
		// (see src/lbm/smtc_modem_core/smtc_modem.c), so `context` is always
		// NULL when LBM itself calls into this function - never dereference
		// it. This port only supports one radio, so it always operates on
		// the static activeConfig above instead of trusting the incoming
		// pointer.
		(void)context;

		digitalWrite(activeConfig.pinReset, LOW);
		delayMicroseconds(kResetPulseUs);
		digitalWrite(activeConfig.pinReset, HIGH);
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

		// SetRx (0x82) / SetRxDutyCycle (0x94): about to receive - only
		// matters for a board with separate direct-steering TXEN/RXEN lines
		// (usesManualTxRxSteering()); a no-op otherwise (WisBlock's own
		// presets let DIO2 do this instead).
		if (command_length > 0 && (command[0] == 0x82 || command[0] == 0x94))
		{
			steerForRx();
		}
		// SetTx (0x83): about to transmit - same caveat as above.
		if (command_length > 0 && command[0] == 0x83)
		{
			steerForTx();
		}

		activeSpi->beginTransaction(spiSettings);
		digitalWrite(activeConfig.pinNss, LOW);

		for (uint16_t i = 0; i < command_length; i++)
		{
			activeSpi->transfer(command[i]);
		}
		for (uint16_t i = 0; i < data_length; i++)
		{
			activeSpi->transfer(data[i]);
		}

		digitalWrite(activeConfig.pinNss, HIGH);
		activeSpi->endTransaction();

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
			driveAntennaPower(false);
#endif
#ifndef WISBLOCK_RADIO_HAL_KEEP_SPI_ALWAYS_ON
			// HYPOTHESIS UNDER TEST (not yet confirmed - see the README's
			// "Persistent idle current floor" note): SPI.begin() enables
			// the underlying SPI peripheral once in init() and nothing
			// ever calls SPI.end() afterward - beginTransaction()/
			// endTransaction() only arbitrate the bus for a single
			// transaction, they don't power the peripheral down between
			// them. Define WISBLOCK_RADIO_HAL_KEEP_SPI_ALWAYS_ON to
			// disable this and A/B test against the previous behavior.
			activeSpi->end();
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

		activeSpi->beginTransaction(spiSettings);
		digitalWrite(activeConfig.pinNss, LOW);

		for (uint16_t i = 0; i < command_length; i++)
		{
			activeSpi->transfer(command[i]);
		}
		for (uint16_t i = 0; i < data_length; i++)
		{
			data[i] = activeSpi->transfer(0x00); // NOP while clocking in the response
		}

		digitalWrite(activeConfig.pinNss, HIGH);
		activeSpi->endTransaction();

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

#endif // ARDUINO_ARCH_NRF52 || NRF52840_XXAA || ARDUINO_ARCH_ESP32
