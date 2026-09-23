/**
 * @file WisBlockLoRaWAN.h
 * @brief Public API for the WisBlockLoRaWAN library.
 *
 * Single entry point for applications: pick a work mode, configure it,
 * register callbacks, call begin()/join() (LoRaWAN) or begin() (P2P), and
 * pump handleEvents() from loop(). The AT command layer (WisBlockLoRaAT.h)
 * is a thin wrapper over this exact same class, so AT and API usage always
 * stay in sync.
 */
#ifndef WISBLOCK_LORAWAN_H
#define WISBLOCK_LORAWAN_H

#include "LoRaP2PEngine.h"
#include "LoRaWANEngine.h"
#include "WisBlockLoRaWANConfig.h"
#include "WisBlockLoRaWANTypes.h"

class WisBlockLoRaWAN
{
public:
	/** Loads saved config (or factory defaults), inits radio + board pins. Call once from setup().
	 * Deliberately does NOT start the LoRaWAN engine (smtc_modem_init() and everything that follows
	 * from it - region/class/ADR setup) here, even if that's the configured/default work mode
	 * - see ensureLoRaWANEngineStarted() below for why. */
	void begin();

	/** Pumps LoRaWAN/P2P engines and low-power timer bookkeeping. Call every loop().
	 * Returns the ms budget before this must be called again (see LoRaWANEngine::handleEvents());
	 * only matters for WisBlockLbmTask's background task mode, safe to ignore otherwise. */
	uint32_t handleEvents();

	// --- Work mode -----------------------------------------------------
	void setWorkMode(WisBlockWorkMode mode);
	WisBlockWorkMode getWorkMode() const { return config.workMode; }

	// --- Device identity (RUI3-compatible AT+ALIAS) ---------------------
	/** RUI3's AT+ALIAS: a free-form, user-settable device label, persisted alongside the rest
	 * of this library's config - unrelated to LoRaWAN/P2P operation, so no engine start is
	 * needed either way. Matches RUI3's own documented "<string, 16char>" limit for a *set*
	 * value: rejected (returns false, no change made) for a NULL pointer or a string longer
	 * than 16 characters, mirroring RUI3's own AT_PARAM_ERROR for a malformed AT+ALIAS= value -
	 * the AT layer reports that error code, this call just reports success/failure. A longer
	 * factory-default string is still fine to read back (see WisBlockPersistedConfig::alias's
	 * doc comment); it just can't be re-entered verbatim through this setter.
	 */
	bool setAlias(const char *alias);
	const char *getAlias() const { return config.alias; }

	// --- Device firmware version (RUI3-compatible AT+FIRMWAREVER) ---------------------
	/** RUI3's AT+FIRMWAREVER: a free-form, user-settable device label, persisted alongside the rest
	 * of this library's config - unrelated to LoRaWAN/P2P operation, so no engine start is
	 * needed either way. Matches RUI3's own documented "<string, 32char>" limit for a *set*
	 * value: rejected (returns false, no change made) for a NULL pointer or a string longer
	 * than 32 characters, mirroring RUI3's own AT_PARAM_ERROR for a malformed AT+FIRMWAREVER= value -
	 * the AT layer reports that error code, this call just reports success/failure. A longer
	 * factory-default string is still fine to read back (see WisBlockPersistedConfig::alias's
	 * doc comment); it just can't be re-entered verbatim through this setter.
	 */
	bool setFirmwareVer(const char *firmwarever);
	const char *getFirmwareVer() const { return config.firmwarever; }

	// --- LoRaWAN credentials & setup ------------------------------------
	void setOTAAKeys(const uint8_t devEui[8], const uint8_t joinEui[8], const uint8_t appKey[16]);
	void setABPKeys(uint32_t devAddr, const uint8_t nwkSKey[16], const uint8_t appSKey[16]);
	void setJoinMode(WisBlockJoinMode mode);
	void setRegion(WisBlockRegion region);
	/**
	 * Returns true if this DR is actually active on the radio right now.
	 * A false return doesn't mean the request was rejected outright - see
	 * LoRaWANEngine::setADR()'s doc comment: the requested DR is stored
	 * either way and retried automatically after every uplink until the
	 * network's channel list allows it, most commonly right after a fresh
	 * join, before its post-join NewChannelReq MAC commands have landed.
	 */
	bool setDataRate(uint8_t dataRate);
	/** See setDataRate()'s doc comment - same underlying mechanism and same meaning for the return value:
	 * fails (returns false) if the device isn't joined yet, and is retried automatically the moment it is
	 * - see LoRaWANEngine::setDeviceClass()'s doc comment. */
	bool setDeviceClass(WisBlockDeviceClass deviceClass);
	/** Sub-band pre-selection for US915/AU915/CN470/CN470_RP_1_0; no effect elsewhere.
	 * See LoRaWANEngine::setChannelMask()'s doc comment for the encoding and why it matters -
	 * short version: it avoids wasting join attempts cycling through the wrong sub-band on
	 * these many-channel regions. Unlike setDeviceClass()/setADR(), safe to call before join(). */
	bool setChannelMask(uint16_t mask);
	uint16_t getChannelMask() const { return lorawan.getChannelMask(); }
	/** RUI3-compatible AT+LBT / AT+LBTRSSI / AT+LBTSCANTIME - see LoRaWANEngine::setLbtEnabled()'s/
	 * setLbtThreshold()'s doc comments for the full mechanism (support Korea, Japan) and an
	 * important finding about region selection *not* automatically applying a region-tuned
	 * threshold - if your target region's certification needs a specific value, set it here. */
	bool setLbtEnabled(bool enabled) { ensureLoRaWANEngineStarted(); return lorawan.setLbtEnabled(enabled); }
	bool getLbtEnabled() const { return lorawan.getLbtEnabled(); }
	bool setLbtThreshold(int16_t thresholdDbm) { ensureLoRaWANEngineStarted(); return lorawan.setLbtThreshold(thresholdDbm); }
	int16_t getLbtThreshold() const { return lorawan.getLbtThreshold(); }
	bool setLbtScanTime(uint32_t scanTimeMs) { ensureLoRaWANEngineStarted(); return lorawan.setLbtScanTime(scanTimeMs); }
	uint32_t getLbtScanTime() const { return lorawan.getLbtScanTime(); }
	/** See setDataRate()'s doc comment - same underlying mechanism and same meaning for the return value. */
	bool setADR(bool enabled);
	void setTxPower(uint8_t txPowerIndex);
	void setConfirmedUplinks(bool confirmed);
	/** See WisBlockLoRaWANSettings::fetchPendingDownlinks's doc comment - default true (fixes
	 * the Class A "downlink queue never drains faster than my own send interval" bug). */
	void setFetchPendingDownlinks(bool enabled) { config.lorawan.fetchPendingDownlinks = enabled; ensureLoRaWANEngineStarted(); lorawan.setFetchPendingDownlinks(enabled); }
	bool getFetchPendingDownlinks() const { return config.lorawan.fetchPendingDownlinks; }

	void join();
	void stopJoin() { ensureLoRaWANEngineStarted(); lorawan.stopJoin(); }
	bool isJoined() const { return lorawan.isJoined(); }
	WisBlockJoinState joinState() const { return lorawan.joinState(); }
	/** See LoRaWANEngine::setAutoJoin()'s doc comment for the full mechanism these three cover
	 * (RUI3-compatible AT+JOIN parameters). Pure configuration - doesn't itself start the
	 * LoRaWAN engine or join anything; autoJoin is only consulted once the engine actually
	 * starts (via ensureLoRaWANEngineStarted()), and the other two only take effect on the
	 * next join()/retry. Safe to call before or after the engine has started either way. */
	void setAutoJoin(bool enabled)
	{
		config.lorawan.autoJoin = enabled;
		lorawan.setAutoJoin(enabled);
	}
	bool getAutoJoin() const { return config.lorawan.autoJoin; }
	void setJoinReattemptInterval(uint8_t seconds)
	{
		lorawan.setJoinReattemptInterval(seconds); // clamps to RUI3's 7-255s range
		config.lorawan.joinReattemptIntervalS = lorawan.getJoinReattemptInterval();
	}
	uint8_t getJoinReattemptInterval() const { return config.lorawan.joinReattemptIntervalS; }
	void setMaxJoinAttempts(uint8_t attempts)
	{
		config.lorawan.maxJoinAttempts = attempts;
		lorawan.setMaxJoinAttempts(attempts);
	}
	uint8_t getMaxJoinAttempts() const { return config.lorawan.maxJoinAttempts; }
	/**
	 * Queues an uplink with LBM. The return value only reflects whether the
	 * request itself was valid and got accepted onto the queue (joined,
	 * port in range, not already pending) - it is NOT a transmission
	 * confirmation; watch for onLoRaWANTxFinished() / onLoRaWANRxFinished()
	 * for that. LBM holds at most one pending outbound uplink at a time;
	 * this now returns false immediately (no LBM call made, no frame
	 * counter spent) if a previous send() is still in flight, rather than
	 * silently queuing a replacement - see LoRaWANEngine::send()'s own doc
	 * comment for the real log capture that prompted this. A false return
	 * means "try again later" - either wait for onLoRaWANTxFinished() or
	 * space out your own periodic send interval further, since a full
	 * Class A cycle (especially amid post-join MAC command negotiation)
	 * can take longer than a fixed interval expects.
	 */
	bool sendLoRaWAN(uint8_t port, const uint8_t *data, uint8_t length);
	void requestLinkCheck() { ensureLoRaWANEngineStarted(); lorawan.requestLinkCheck(); }
	bool getLinkCheckResult(WisBlockLinkCheckResult &out) const { return lorawan.getLinkCheckResult(out); }
	/** See LoRaWANEngine::setLinkCheckMode()'s doc comment for the full RUI3-compatible behavior. */
	void setLinkCheckMode(uint8_t mode) { lorawan.setLinkCheckMode(mode); }
	uint8_t getLinkCheckMode() const { return lorawan.getLinkCheckMode(); }
	void requestDeviceTime() { ensureLoRaWANEngineStarted(); lorawan.requestDeviceTime(); }
	/** RUI3-compatible AT+PGSLOT/AT+BFREQ/AT+BTIME - see LoRaWANEngine::setPingSlotPeriodicity()'s/
	 * getBeaconFrequencyAndDr()'s/getBeaconTime()'s doc comments. AT+BGW (gateway GPS/NetID/GwID
	 * from the beacon's GwSpecific field) is not implemented - it needs raw beacon payload
	 * decoding this library doesn't currently do (see LoRaWANEngine.h's Class B section for
	 * what is/isn't covered). */
	bool setPingSlotPeriodicity(uint8_t periodicity);
	uint8_t getPingSlotPeriodicity() const { return lorawan.getPingSlotPeriodicity(); }
	bool getBeaconFrequencyAndDr(uint32_t &frequencyHz, uint8_t &dr) const { return lorawan.getBeaconFrequencyAndDr(frequencyHz, dr); }
	uint32_t getBeaconTime() const { return lorawan.getBeaconTime(); }

	// --- LoRa P2P setup --------------------------------------------------
	void setP2PFrequency(uint32_t frequencyHz);
	void setP2PSpreadingFactor(uint8_t sf);
	void setP2PBandwidth(WisBlockP2PBandwidth bw);
	void setP2PCodingRate(WisBlockP2PCodingRate cr);
	void setP2PPreambleLength(uint16_t symbols);
	void setP2PTxPower(int8_t dbm);
	void setP2PCad(bool enabled);
	/**
	 * Trades RX current for sensitivity - see WisBlockP2PSettings::rxBoostedGainEnabled's
	 * doc comment for the ~4-5mA-vs-a-few-dB tradeoff. Takes effect on the
	 * next CAD/RX/TX (applied via LoRaP2PEngine::applyRadioParams(), same
	 * as every other P2P radio parameter).
	 */
	void setP2PRxBoostedGain(bool enabled);
	bool sendP2P(const uint8_t *data, uint8_t length);
	void startP2PReceive(uint32_t timeoutMs = 0);
	/** See LoRaP2PEngine::startReceiveDutyCycle()'s doc comment for the full picture. */
	void startP2PReceiveDutyCycle(uint32_t rxTimeMs, uint32_t sleepTimeMs);
	/** Read-only access to the currently applied P2P radio settings - frequency, SF, bandwidth, preamble length, etc. */
	const WisBlockP2PSettings &getP2PSettings() const { return config.p2p; }
	/** See LoRaP2PEngine::computeRxDutyCycleTiming()'s doc comment for the full picture. */
	bool computeP2PRxDutyCycleTiming(uint32_t &rxTimeMs, uint32_t &sleepTimeMs, uint8_t marginSymbols = 5) const
	{
		return p2p.computeRxDutyCycleTiming(rxTimeMs, sleepTimeMs, marginSymbols);
	}
	/**
	 * Prefer this overload over the one above whenever you know the
	 * transmitting node's actual preamble length - which is effectively
	 * always, since it's usually a compile-time constant on the sending
	 * side too. See LoRaP2PEngine::computeRxDutyCycleTiming()'s doc
	 * comment for why the receiver's own configured preamble length isn't
	 * a reliable substitute for it.
	 */
	bool computeP2PRxDutyCycleTiming(uint16_t txPreambleLengthSymbols, uint32_t &rxTimeMs, uint32_t &sleepTimeMs,
									  uint8_t marginSymbols = 5) const
	{
		return p2p.computeRxDutyCycleTiming(txPreambleLengthSymbols, rxTimeMs, sleepTimeMs, marginSymbols);
	}
	void stopP2PReceive();
	void startP2PCad();
	/**
	 * Puts the SX1262 into low-power sleep - only meaningful in P2P mode
	 * (LoRaWAN mode's radio_planner already sleeps the radio automatically
	 * between scheduled tasks; this is a no-op there). Call whenever your
	 * application knows it has no immediate P2P radio activity coming up.
	 * See LoRaP2PEngine::sleep() for why this exists - it was previously
	 * missing entirely, leaving the radio in STANDBY (several mA with the
	 * TCXO active) instead of SLEEP (~1.5uA) whenever idle.
	 */
	void sleepRadio();

	// --- Persistence ------------------------------------------------------
	/** Persists the current live config to the regular *user* flash slot. Returns false on write failure. */
	bool saveConfig();
	/**
	 * Reloads the *user* slot from flash into the current live config,
	 * discarding any unsaved in-RAM changes since the last saveConfig() -
	 * see restoreFactoryDefaults() below for reaching further back, to the
	 * factory backup instead. Returns false (defaults loaded) if nothing
	 * has ever been saved.
	 */
	bool restoreConfig();
	/**
	 * Snapshots the CURRENT live configuration (not compiled-in struct
	 * defaults) into a separate "factory" flash slot, untouched by
	 * ordinary saveConfig()/restoreConfig() traffic - see AT+FACTORY in
	 * WisBlockLoRaAT.cpp for the intended one-time production flow this is
	 * part of (set a unique DevEUI, then call this, then reboot).
	 * Returns false on flash write failure.
	 */
	bool saveFactoryDefaults();
	/**
	 * Loads the factory-slot config saved by saveFactoryDefaults(), applies
	 * it as the current live configuration, and persists it into the
	 * regular user flash slot too - so it's what restoreConfig()/
	 * AT+RESTORE reload from now on, not just a one-off in-RAM change. See
	 * ATR in WisBlockLoRaAT.cpp. Returns false (config left untouched) if
	 * no factory backup has ever been saved.
	 */
	bool restoreFactoryDefaults();
	const WisBlockPersistedConfig &getConfig() const { return config; }

	// --- Low power ----------------------------------------------------
	void setLowPowerEnabled(bool enabled) { config.lowPowerEnabled = enabled; }
	bool isLowPowerEnabled() const { return config.lowPowerEnabled; }
	/**
	 * Parks the MCU in a low-power wait, per-platform (see WisBlockLoRaWAN.cpp
	 * for exactly what each target does): waits on the Adafruit nRF52 core's
	 * waitForEvent() on RAK4631, esp_light_sleep_start() on RAK3312, and a
	 * __wfi() loop on RAK11310 (true dormant sleep there needs a
	 * pico-extras-enabled core build - see the comment at the call site).
	 * Wakes on the SX1262 DIO1 IRQ or `maxDurationMs` elapsing (0 = wait
	 * indefinitely for DIO1), whichever comes first. Doesn't touch the radio
	 * itself - call sleepRadio() first in P2P mode if you also want that
	 * asleep (LoRaWAN mode's radio_planner already handles it). Not meant to
	 * be combined with enableBackgroundTask() - see that method's own doc
	 * comment for why you generally don't need this once it's active.
	 */
	void sleep(uint32_t maxDurationMs = 0);

	/**
	 * Starts a FreeRTOS background task that drives handleEvents()
	 * automatically - once this returns true, loop() no longer needs to
	 * call handleEvents() at all (it becomes a harmless no-op if you do
	 * anyway, so existing sketches don't break if adapted incrementally).
	 * See wisblock_lbm_task.h for the full explanation and platform
	 * availability notes (works out of the box on RAK4631/RAK3312; RAK11310
	 * needs a FreeRTOS-Kernel port added to the project first).
	 *
	 * Must be called after begin(). Returns false if FreeRTOS isn't
	 * available on this platform/build - keep calling handleEvents() from
	 * loop() yourself in that case, exactly as before.
	 */
	bool enableBackgroundTask();

	/**
	 * Guards any code that calls into this library's API (and therefore
	 * into LBM) from a task/context other than whichever one owns
	 * background task mode - needed by WisBlockLoRaAT::enableBackgroundRx(),
	 * since AT commands like AT+SEND touch the same LBM engine state the
	 * background task does, from a different task (the USB CDC RX
	 * callback's context). No-op if enableBackgroundTask() was never
	 * called successfully.
	 */
	void lockLbm();
	void unlockLbm();

	// --- Callback registration (LoRaWAN) ------------------------------
	void onJoinSuccess(LoRaWANEngine::JoinSuccessCb cb) { lorawan.onJoinSuccess(cb); }
	void onJoinFailed(LoRaWANEngine::JoinFailedCb cb) { lorawan.onJoinFailed(cb); }
	void onLoRaWANTxFinished(LoRaWANEngine::TxFinishedCb cb) { lorawan.onTxFinished(cb); }
	void onLoRaWANRxFinished(LoRaWANEngine::RxFinishedCb cb) { lorawan.onRxFinished(cb); }
	void onTimeRequestAnswer(LoRaWANEngine::TimeRequestCb cb) { lorawan.onTimeRequestAnswer(cb); }
	void onLinkCheckAnswer(LoRaWANEngine::LinkCheckCb cb) { lorawan.onLinkCheckAnswer(cb); }

	// --- Callback registration (LoRa P2P) -----------------------------
	void onP2PTxFinished(LoRaP2PEngine::TxFinishedCb cb) { p2p.onTxFinished(cb); }
	void onP2PRxFinished(LoRaP2PEngine::RxFinishedCb cb) { p2p.onRxFinished(cb); }
	void onP2PCadResult(LoRaP2PEngine::CadResultCb cb) { p2p.onCadResult(cb); }

private:
	WisBlockPersistedConfig config;
	LoRaWANEngine lorawan;
	LoRaP2PEngine p2p;
	bool began = false;
	bool backgroundTaskActive = false;
	bool lorawanEngineStarted = false;

	void applyLoRaWANSettings();
	void applyP2PSettings();

	/**
	 * Lazily runs lorawan.begin() (smtc_modem_init() + region/class/ADR
	 * setup) the first time anything LoRaWAN-specific is actually touched,
	 * instead of begin() doing it unconditionally for every application
	 * regardless of work mode.
	 *
	 * FIX: begin() used to call lorawan.begin() unconditionally, every time,
	 * for every application - including pure P2P sketches that call
	 * setWorkMode(WISBLOCK_MODE_LORA_P2P) right afterward and never touch
	 * a single LoRaWAN API again. That's not just wasted flash-load/region-
	 * table setup: starting the LoRaWAN engine also hands LBM's radio
	 * planner ownership of the *same physical radio* the P2P engine then
	 * tries to run, before the application ever calls setWorkMode(P2P) to
	 * say it doesn't want that. p2p.sleep()'s plain SX126x SetSleep command
	 * doesn't know anything about LBM's own scheduling and can't cancel it -
	 * LBM's radio planner still considers itself the owner. That
	 * contention - not a HAL-level bug - is the source of the residual
	 * elevated idle current on top of the antenna-power/DIO1 fixes: the two
	 * engines were fighting over the same SX1262 the whole time.
	 *
	 * Now nothing calls this until something actually needs it - the
	 * LoRaWAN-only setters/actions below, or setWorkMode(WISBLOCK_MODE_LORAWAN)
	 * itself. A P2P-only application that calls setWorkMode(LORA_P2P) before
	 * ever calling any LoRaWAN API never starts the LoRaWAN engine at all,
	 * so there's no smtc_modem_init(), and nothing
	 * else contending with p2p's ownership of the radio.
	 *
	 * No-op before begin() (mirrors applyLoRaWANSettings()'s existing
	 * `began` guard) and idempotent after - safe to call from every
	 * LoRaWAN-facing entry point unconditionally.
	 */
	void ensureLoRaWANEngineStarted();

	// Does the actual event-processing work; handleEvents() (loop()-facing,
	// guarded against double-processing once background task mode is
	// active) and handleEventsStatic() (background-task-facing, always
	// calls this directly) both funnel through here - see the .cpp for why
	// they can't just both call handleEvents() itself.
	uint32_t handleEventsInternal();

	// WisBlockLbmTask's background task calls a plain C-style function
	// pointer (no captures/context), so a single static instance pointer
	// bridges that back to this instance's handleEvents(). Only one
	// WisBlockLoRaWAN instance can use background task mode at a time -
	// consistent with the rest of this library, which only ever supports
	// one radio/one stack.
	static WisBlockLoRaWAN *activeInstanceForTask;
	static uint32_t handleEventsStatic();
};

#endif // WISBLOCK_LORAWAN_H
