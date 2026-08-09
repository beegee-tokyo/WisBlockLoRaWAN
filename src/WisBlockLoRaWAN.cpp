#include "WisBlockLoRaWAN.h"
#include "WisBlockLoRaBoards.h"
#include "WisBlockLoRaFlash.h"
#include "wisblock_lbm_port.h"
#include "wisblock_lbm_task.h"
#include "wisblock_radio_hal.h"
#include <Arduino.h>
#include <string.h>

#if defined(ARDUINO_ARCH_ESP32)
// esp_light_sleep_start()/gpio_wakeup_enable() - see sleep() below.
#include "driver/gpio.h"
#include "esp_sleep.h"
#endif

void WisBlockLoRaWAN::begin()
{
	// Must run before anything touches flash - on nRF52 this calls
	// InternalFS.begin(), which mounts LittleFS. Calling File read/write
	// before this (e.g. from wisblockConfigLoad() below) operates on an
	// unmounted filesystem context and crashes with a LittleFS internal
	// assertion ("block < lfs->cfg->block_count") rather than failing
	// gracefully.
	WisBlockLoRaFlash::init();

	wisblockConfigLoad(config); // falls back to factory defaults internally

	// Also calls WisBlockLoRaFlash::init() internally (for LBM's own
	// context store) - harmless/idempotent given the explicit call above,
	// kept there so wisblock_lbm_port.cpp doesn't depend on being
	// sequenced after this file's flash init.
	WisBlockLbmPort::init();

	// Sets up SPI + NSS/RESET/BUSY GPIOs and performs the initial hardware
	// reset (see wisblock_radio_hal_rak4631.cpp for the RAK4631 backend;
	// wisblock_radio_hal_<board>.cpp for the other two targets).
	WisBlockRadioHal::init();

	// Deliberately does NOT call lorawan.begin() here - see
	// ensureLoRaWANEngineStarted()'s doc comment in WisBlockLoRaWAN.h for
	// why starting the LoRaWAN engine (and, with it, LBM's relay WOR
	// configuration) unconditionally for every application, P2P-only ones
	// included, was actively fighting the P2P engine for control of the
	// radio.
	p2p.begin(config.p2p);

	began = true;
}

void WisBlockLoRaWAN::ensureLoRaWANEngineStarted()
{
	// See the doc comment on this method's declaration in
	// WisBlockLoRaWAN.h for the full "why" - short version: don't touch
	// LBM/smtc_modem_* at all until something LoRaWAN-specific is actually
	// requested, so a P2P-only application never pays for (or contends
	// with) it.
	if (!began || lorawanEngineStarted)
	{
		return;
	}
	lorawanEngineStarted = true;
	lorawan.begin(config.lorawan);
}

WisBlockLoRaWAN *WisBlockLoRaWAN::activeInstanceForTask = nullptr;

uint32_t WisBlockLoRaWAN::handleEventsStatic()
{
	// Calls the internal worker directly, bypassing handleEvents()'s
	// loop()-reentrancy guard below - that guard exists to stop the *main*
	// thread from double-processing once background task mode owns event
	// handling, but this static wrapper IS the background task calling in,
	// so applying the same guard here would make the task's own calls a
	// permanent no-op (which is exactly what happened before this fix -
	// the task would wake up correctly but then immediately bail out).
	if (activeInstanceForTask)
	{
		return activeInstanceForTask->handleEventsInternal();
	}
	return 60000; // no instance registered - shouldn't happen once enableBackgroundTask() succeeded
}

uint32_t WisBlockLoRaWAN::handleEvents()
{
	// Once the background task (see enableBackgroundTask()) owns event
	// processing, calling this from loop() too would let two different
	// task contexts call into LBM's smtc_modem_run_engine()/get_event()
	// concurrently - not reentrant-safe. Made a harmless no-op instead of
	// removing it outright so existing sketches don't need to delete their
	// loop()-level handleEvents() call when adopting background task mode.
	if (backgroundTaskActive)
	{
		return 60000;
	}
	return handleEventsInternal();
}

uint32_t WisBlockLoRaWAN::handleEventsInternal()
{
	WisBlockLbmPort::tick();
	if (config.workMode == WISBLOCK_MODE_LORAWAN)
	{
		// Defensive/idempotent: normally already started by whichever
		// LoRaWAN setter or setWorkMode(LORAWAN) call got here first (see
		// ensureLoRaWANEngineStarted()'s doc comment) - this just covers
		// the edge case of a restored/default config already being
		// WISBLOCK_MODE_LORAWAN with no explicit setter ever called before
		// the first handleEvents().
		ensureLoRaWANEngineStarted();
		return lorawan.handleEvents();
	}
	else
	{
		return p2p.handleEvents();
	}
}

void WisBlockLoRaWAN::setWorkMode(WisBlockWorkMode mode)
{
	config.workMode = mode;
	if (mode == WISBLOCK_MODE_LORAWAN)
	{
		ensureLoRaWANEngineStarted();
	}
}

void WisBlockLoRaWAN::setOTAAKeys(const uint8_t devEui[8], const uint8_t joinEui[8], const uint8_t appKey[16])
{
	memcpy(config.lorawan.otaa.devEui, devEui, 8);
	memcpy(config.lorawan.otaa.joinEui, joinEui, 8);
	memcpy(config.lorawan.otaa.appKey, appKey, 16);
	config.lorawan.joinMode = WISBLOCK_JOIN_OTAA;
	applyLoRaWANSettings();
}

void WisBlockLoRaWAN::setABPKeys(uint32_t devAddr, const uint8_t nwkSKey[16], const uint8_t appSKey[16])
{
	config.lorawan.abp.devAddr = devAddr;
	memcpy(config.lorawan.abp.nwkSKey, nwkSKey, 16);
	memcpy(config.lorawan.abp.appSKey, appSKey, 16);
	config.lorawan.joinMode = WISBLOCK_JOIN_ABP;
	applyLoRaWANSettings();
}

void WisBlockLoRaWAN::setJoinMode(WisBlockJoinMode mode)
{
	config.lorawan.joinMode = mode;
	applyLoRaWANSettings();
}

void WisBlockLoRaWAN::setRegion(WisBlockRegion region)
{
	config.lorawan.region = region;
	applyLoRaWANSettings();
}

void WisBlockLoRaWAN::setDataRate(uint8_t dataRate)
{
	config.lorawan.dataRate = dataRate;
	ensureLoRaWANEngineStarted();
	lorawan.setDataRate(dataRate);
}

void WisBlockLoRaWAN::setDeviceClass(WisBlockDeviceClass deviceClass)
{
	config.lorawan.deviceClass = deviceClass;
	ensureLoRaWANEngineStarted();
	lorawan.setDeviceClass(deviceClass);
}

void WisBlockLoRaWAN::setADR(bool enabled)
{
	config.lorawan.adrEnabled = enabled;
	ensureLoRaWANEngineStarted();
	lorawan.setADR(enabled);
}

void WisBlockLoRaWAN::setTxPower(uint8_t txPowerIndex)
{
	config.lorawan.txPower = txPowerIndex;
	ensureLoRaWANEngineStarted();
	lorawan.setTxPower(txPowerIndex);
}

void WisBlockLoRaWAN::setConfirmedUplinks(bool confirmed)
{
	config.lorawan.confirmedUplinks = confirmed;
}

void WisBlockLoRaWAN::setRelayMode(WisBlockRelayMode mode)
{
	config.lorawan.relayMode = mode;
	ensureLoRaWANEngineStarted();
	lorawan.setRelayMode(mode);
}

void WisBlockLoRaWAN::join()
{
	ensureLoRaWANEngineStarted();
	lorawan.join();
	// FIX: see WisBlockLbmTask::notify()'s doc comment - without this, a
	// join() call queued while the background task is asleep waiting on
	// its own previously-computed deadline could sit untouched for a long
	// time before smtc_modem_run_engine() ever runs again to actually
	// start the join sequence.
	WisBlockLbmTask::notify();
}

bool WisBlockLoRaWAN::sendLoRaWAN(uint8_t port, const uint8_t *data, uint8_t length)
{
	ensureLoRaWANEngineStarted();
	bool queued = lorawan.send(port, data, length, config.lorawan.confirmedUplinks);
	if (queued)
	{
		// FIX (root cause of a queued uplink sitting for 30+ seconds before
		// actually transmitting, confirmed via a real log capture - see the
		// README's "Queued send not dispatched promptly" note): see
		// WisBlockLbmTask::notify()'s doc comment for the full mechanism.
		// sendLoRaWAN() is typically called from the application's own
		// task, not the background LBM task, so nothing was otherwise
		// prompting the background task to service this uplink before its
		// own previously-scheduled wake time.
		WisBlockLbmTask::notify();
	}
	return queued;
}

void WisBlockLoRaWAN::setP2PFrequency(uint32_t frequencyHz)
{
	config.p2p.frequencyHz = frequencyHz;
	applyP2PSettings();
}

void WisBlockLoRaWAN::setP2PSpreadingFactor(uint8_t sf)
{
	config.p2p.spreadingFactor = sf;
	applyP2PSettings();
}

void WisBlockLoRaWAN::setP2PBandwidth(WisBlockP2PBandwidth bw)
{
	config.p2p.bandwidth = bw;
	applyP2PSettings();
}

void WisBlockLoRaWAN::setP2PCodingRate(WisBlockP2PCodingRate cr)
{
	config.p2p.codingRate = cr;
	applyP2PSettings();
}

void WisBlockLoRaWAN::setP2PPreambleLength(uint16_t symbols)
{
	config.p2p.preambleLength = symbols;
	applyP2PSettings();
}

void WisBlockLoRaWAN::setP2PTxPower(int8_t dbm)
{
	config.p2p.txPowerDbm = dbm;
	applyP2PSettings();
}

void WisBlockLoRaWAN::setP2PCad(bool enabled)
{
	config.p2p.cadEnabled = enabled;
}

void WisBlockLoRaWAN::setP2PRxBoostedGain(bool enabled)
{
	config.p2p.rxBoostedGainEnabled = enabled;
	applyP2PSettings();
}

bool WisBlockLoRaWAN::sendP2P(const uint8_t *data, uint8_t length)
{
	return p2p.send(data, length);
}

void WisBlockLoRaWAN::startP2PReceive(uint32_t timeoutMs)
{
	p2p.startReceive(timeoutMs);
}

void WisBlockLoRaWAN::startP2PReceiveDutyCycle(uint32_t rxTimeMs, uint32_t sleepTimeMs)
{
	p2p.startReceiveDutyCycle(rxTimeMs, sleepTimeMs);
}

void WisBlockLoRaWAN::stopP2PReceive()
{
	p2p.stopReceive();
}

void WisBlockLoRaWAN::startP2PCad()
{
	p2p.startCad();
}

void WisBlockLoRaWAN::sleepRadio()
{
	if (config.workMode == WISBLOCK_MODE_LORA_P2P)
	{
		p2p.sleep();
	}
	// No-op for LoRaWAN mode - radio_planner already puts the radio to
	// sleep automatically between scheduled tasks (see
	// src/lbm/smtc_modem_core/radio_planner/src/radio_planner.c's
	// ral_set_sleep() calls).
}

bool WisBlockLoRaWAN::saveConfig()
{
	return wisblockConfigSave(config);
}

bool WisBlockLoRaWAN::restoreConfig()
{
	bool ok = wisblockConfigLoad(config);
	applyLoRaWANSettings();
	applyP2PSettings();
	return ok;
}

bool WisBlockLoRaWAN::factoryReset()
{
	bool ok = wisblockConfigFactoryReset();
	wisblockConfigLoad(config);
	applyLoRaWANSettings();
	applyP2PSettings();
	return ok;
}

void WisBlockLoRaWAN::sleep(uint32_t maxDurationMs)
{
	// NOTE: if you've called enableBackgroundTask(), you likely don't need
	// this explicit sleep() at all - structure your own application task
	// (or just loop()) around blocking waits instead of polling, and
	// FreeRTOS's own tickless idle behavior (built into both the Adafruit
	// nRF52 core and the ESP32 Arduino core) will automatically drop the
	// MCU into a low-power idle state whenever no task is ready to run.
	// This explicit sleep() is for bare-metal (non-task-mode) builds, where
	// nothing else provides that automatic idle behavior.
	//
	// This only parks the MCU - it doesn't touch the radio. In LoRaWAN mode
	// radio_planner already sleeps the radio on its own; in P2P mode call
	// sleepRadio() yourself first if you want the radio asleep too (see its
	// doc comment). Waking the radio back up (and re-powering the RF switch
	// - see wisblock_radio_hal_*.cpp's "RF-switch power tracking") happens
	// automatically on whatever SPI transaction it needs next; nothing
	// extra to do here for that.
	//
	// maxDurationMs == 0 means "wait indefinitely for DIO1" on every branch
	// below.

#if defined(ARDUINO_ARCH_NRF52)
	// Adafruit nRF52 core: waitForEvent() (declared in the core's own
	// wiring.h, pulled in transitively by Arduino.h - no extra include
	// needed) parks the CPU in a low-power WFE wait, using
	// sd_app_evt_wait() if the SoftDevice/BLE stack is active or a raw
	// __WFE() spin otherwise (see cores/nRF5/wiring.c in
	// Adafruit_nRF52_Arduino). DIO1's GPIOTE interrupt - already attached
	// in WisBlockLbmPort::init() - generates a genuine hardware event on
	// every rising edge, which is exactly what wakes this; no separate
	// registration needed. RTC-driven millis() keeps advancing through the
	// wait, so the timeout below is still accurate.
	uint32_t start = millis();
	while (true)
	{
		if (WisBlockLbmPort::consumeRadioIrqFlag())
		{
			break;
		}
		if (maxDurationMs != 0 && (millis() - start) >= maxDurationMs)
		{
			break;
		}
		waitForEvent();
	}

#elif defined(ARDUINO_ARCH_ESP32)
	// ESP32-S3: light sleep (not deep sleep - deep sleep wipes RAM, which
	// would lose LBM's in-memory state and this library's config). GPIO
	// wakeup (rather than ext0/ext1) is used deliberately: it works on any
	// digital IO, not just RTC-capable pins, so it doesn't constrain which
	// GPIO LORA_DIO1 can be wired to on this board.
	gpio_wakeup_enable((gpio_num_t)LORA_DIO1, GPIO_INTR_HIGH_LEVEL);
	esp_sleep_enable_gpio_wakeup();
	if (maxDurationMs != 0)
	{
		esp_sleep_enable_timer_wakeup((uint64_t)maxDurationMs * 1000ULL);
	}
	else
	{
		esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
	}
	esp_light_sleep_start();
	gpio_wakeup_disable((gpio_num_t)LORA_DIO1);

#elif defined(ARDUINO_ARCH_RP2040)
	// RP2040 (RAK11310, plain arduino-pico core): true dormant-mode sleep
	// needs pico-extras' pico_sleep component, which arduino-pico doesn't
	// ship or expose without rebuilding the core's libpico.a and extending
	// its include path (see the arduino-pico project's own "Looking for
	// dormant mode" issue - there's no supported way to pull it in from a
	// normal sketch/library). Without that dependency, __wfi() is the best
	// available primitive: it halts the CPU clock until the next interrupt
	// or exception (DIO1's GPIO IRQ included), which is real but shallower
	// power savings than dormant mode - the rest of the chip (clocks,
	// peripherals) stays powered. If you rebuild libpico.a with pico-extras
	// per that project's instructions, swap this loop for
	// sleep_goto_dormant_until_edge_high(LORA_DIO1) for a deeper sleep.
	uint32_t start = millis();
	while (true)
	{
		if (WisBlockLbmPort::consumeRadioIrqFlag())
		{
			break;
		}
		if (maxDurationMs != 0 && (millis() - start) >= maxDurationMs)
		{
			break;
		}
		__wfi();
	}
#else
	(void)maxDurationMs;
#endif
}

bool WisBlockLoRaWAN::enableBackgroundTask()
{
	if (!began)
	{
		return false; // must call begin() first
	}

	activeInstanceForTask = this;
	backgroundTaskActive = WisBlockLbmTask::start(&WisBlockLoRaWAN::handleEventsStatic);

	if (!backgroundTaskActive)
	{
		// FreeRTOS unavailable on this platform/build (e.g. plain RP2040
		// without a FreeRTOS-Kernel port added) - fall back to requiring
		// loop()-level handleEvents() polling exactly as before.
		activeInstanceForTask = nullptr;
	}
	return backgroundTaskActive;
}

void WisBlockLoRaWAN::lockLbm()
{
	WisBlockLbmTask::lock();
}

void WisBlockLoRaWAN::unlockLbm()
{
	WisBlockLbmTask::unlock();
}

void WisBlockLoRaWAN::applyLoRaWANSettings()
{
	// Gated on work mode (not just `began`) so that restoreConfig() /
	// factoryReset() - which call this unconditionally as part of a
	// blanket config resync, regardless of which mode is active - can't
	// reintroduce the same problem ensureLoRaWANEngineStarted() exists to
	// avoid: starting the LoRaWAN engine for an application that's
	// actually running in P2P mode. Explicit LoRaWAN setters (setOTAAKeys()
	// etc.) still update config.lorawan either way - they just won't take
	// live effect on a not-yet-started engine until something (typically
	// setWorkMode(WISBLOCK_MODE_LORAWAN)) actually starts it, at which
	// point lorawan.begin(config.lorawan) picks up everything set here in
	// the meantime.
	if (began && config.workMode == WISBLOCK_MODE_LORAWAN)
	{
		ensureLoRaWANEngineStarted();
		lorawan.applySettings(config.lorawan);
	}
}

void WisBlockLoRaWAN::applyP2PSettings()
{
	if (began)
	{
		p2p.applySettings(config.p2p);
	}
}
