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
#include "WisBlockLoRaHwConfig.h"
#include "WisBlockLoRaWANConfig.h"
#include "WisBlockLoRaWANTypes.h"

/**
 * @brief Main class of the library (LoRaWAN and LoRa P2P)
 *
 * One object covers the radio, the stored configuration, the LoRaWAN engine and the LoRa P2P engine. See the API documentation for the typical call sequence.
 */
class WisBlockLoRaWAN
{
public:
	/**
	 * @brief Start the library with the compile-time selected RAKwireless board
	 *
	 * Loads saved config (or factory defaults), inits radio + board pins. Call once from setup().
	 * Deliberately does NOT start the LoRaWAN engine (smtc_modem_init() and everything that follows
	 * from it - region/class/ADR setup) here, even if that's the configured/default work mode
	 * - see ensureLoRaWANEngineStarted() below for why.
	 *
	 * Uses the compile-time-selected RAKwireless board preset (RAK4631/RAK3312/RAK11310 -
	 * see WisBlockLoRaHwConfig.h). For any other board, use the begin(const WisBlockLoRaHwConfig&)
	 * overload below instead.
	 */
	void begin();

#if defined(ARDUINO_ARCH_NRF52) || defined(NRF52840_XXAA) || defined(ARDUINO_ARCH_ESP32)
	/**
	 * @brief Start the library with an explicit board description
	 *
	 * Same as plain begin(), but takes an explicit board description instead of relying on the
	 * compile-time RAKwireless board preset - see WisBlockLoRaHwConfig.h's doc comment for what
	 * it describes and its wisblockLoRaHwConfigRAKxxxx() presets (including the new RAK3401 one)
	 * for real examples to start from. This is how a board other than RAK4631/RAK3312/RAK11310
	 * is supported: fill in a WisBlockLoRaHwConfig for its wiring (or copy a preset and override
	 * the fields that differ) and pass it here instead of calling plain begin().
	 *
	 * Only available on nRF52840/ESP32-S3 builds - RP2040 (RAK11310) doesn't have the full
	 * FreeRTOS support this flexible path assumes, see wisblock_radio_hal.h's doc comment.
	 *
	 * @param hwConfig Radio wiring (pins, SPI, TCXO) of the board
	 */
	void begin(const WisBlockLoRaHwConfig &hwConfig);
#endif

	/**
	 * @brief Process the LoRaWAN and P2P events, call it in every loop()
	 *
	 * Pumps LoRaWAN/P2P engines and low-power timer bookkeeping. Call every loop().
	 * Returns the ms budget before this must be called again (see LoRaWANEngine::handleEvents());
	 * only matters for WisBlockLbmTask's background task mode, safe to ignore otherwise.
	 *
	 * @return Time in ms until this function must be called again
	 */
	uint32_t handleEvents();

	// --- Work mode -----------------------------------------------------
	/**
	 * @brief Select the work mode (LoRaWAN or LoRa P2P)
	 *
	 * @param mode WISBLOCK_MODE_LORAWAN or WISBLOCK_MODE_LORA_P2P
	 */
	void setWorkMode(WisBlockWorkMode mode);
	/**
	 * @brief Get the current work mode
	 * @return Current work mode
	 */
	WisBlockWorkMode getWorkMode() const { return config.workMode; }

	// --- Device identity (RUI3-compatible AT+ALIAS) ---------------------
	/**
	 * @brief Set the device alias (AT+ALIAS)
	 *
	 * RUI3's AT+ALIAS: a free-form, user-settable device label, persisted alongside the rest
	 * of this library's config - unrelated to LoRaWAN/P2P operation, so no engine start is
	 * needed either way. Matches RUI3's own documented "<string, 16char>" limit for a *set*
	 * value: rejected (returns false, no change made) for a NULL pointer or a string longer
	 * than 16 characters, mirroring RUI3's own AT_PARAM_ERROR for a malformed AT+ALIAS= value -
	 * the AT layer reports that error code, this call just reports success/failure. A longer
	 * factory-default string is still fine to read back (see WisBlockPersistedConfig::alias's
	 * doc comment); it just can't be re-entered verbatim through this setter.
	 *
	 * @param alias New alias, at most 16 characters
	 * @return true if the alias was stored
	 */
	bool setAlias(const char *alias);
	/**
	 * @brief Get the device alias (AT+ALIAS)
	 * @return Zero terminated alias string
	 */
	const char *getAlias() const { return config.alias; }

	// --- Device firmware version (RUI3-compatible AT+FIRMWAREVER) ---------------------
	/**
	 * @brief Set the firmware version label (AT+FIRMWAREVER)
	 *
	 * RUI3's AT+FIRMWAREVER: a free-form, user-settable device/firmware label, persisted
	 * alongside the rest of this library's config - unrelated to LoRaWAN/P2P operation, so no
	 * engine start is needed either way. RUI3 documents this as a "<string, 32char>" limit;
	 * config.firmwarever is a 32-byte buffer, so the longest value this can actually hold
	 * (leaving room for the null terminator) is 31 characters - rejected (returns false, no
	 * change made) for a NULL pointer or anything longer than that, mirroring RUI3's own
	 * AT_PARAM_ERROR for a malformed AT+FIRMWAREVER= value (the AT layer reports that error
	 * code; this call just reports success/failure).
	 *
	 * @param firmwarever New label, at most 31 characters
	 * @return true if the label was stored
	 */
	bool setFirmwareVer(const char *firmwarever);
	/**
	 * @brief Get the firmware version label (AT+FIRMWAREVER)
	 * @return Zero terminated version string
	 */
	const char *getFirmwareVer() const { return config.firmwarever; }

	// --- LoRaWAN credentials & setup ------------------------------------
	/**
	 * @brief Set the OTAA credentials
	 *
	 * @param devEui Device EUI, 8 bytes, most significant byte first
	 * @param joinEui Join (application) EUI, 8 bytes
	 * @param appKey Application key, 16 bytes
	 */
	void setOTAAKeys(const uint8_t devEui[8], const uint8_t joinEui[8], const uint8_t appKey[16]);
	/**
	 * @brief Set the ABP credentials
	 *
	 * @param devAddr Device address
	 * @param nwkSKey Network session key, 16 bytes
	 * @param appSKey Application session key, 16 bytes
	 */
	void setABPKeys(uint32_t devAddr, const uint8_t nwkSKey[16], const uint8_t appSKey[16]);
	/**
	 * @brief Select OTAA or ABP activation
	 *
	 * @param mode WISBLOCK_JOIN_OTAA or WISBLOCK_JOIN_ABP
	 */
	void setJoinMode(WisBlockJoinMode mode);
	/**
	 * @brief Set the LoRaWAN region
	 *
	 * Sets the LoRaWAN region using RUI3's AT+BAND numbering (WisBlockRUI3Band) - the same
	 * enumeration AT+BAND itself uses, rather than the SWL2001/LBM-mirroring WisBlockRegion enum
	 * used internally and stored in getConfig().lorawan.region. Converts internally to
	 * WisBlockRegion before applying, via wisblockRUI3BandToRegion() - see its doc comment and
	 * WisBlockRUI3Band's for why these two numberings exist.
	 * @param band Region in RUI3 AT+BAND numbering
	 * @return false (no change made) for WISBLOCK_RUI3_BAND_LA915, or any other band with no
	 * WisBlockRegion equivalent in this vendored LBM build.
	 */
	bool setRegion(WisBlockRUI3Band band);
	/**
	 * @brief Get the LoRaWAN region
	 *
	 * Inverse of setRegion() - returns the currently configured region using RUI3's AT+BAND
	 * numbering (WisBlockRUI3Band), via wisblockRegionToRUI3Band().
	 * @return WISBLOCK_RUI3_BAND_UNKNOWN if the current region (getConfig().lorawan.region) has
	 * no RUI3 band index at all (WISBLOCK_REGION_CN470_RP_1_0, WISBLOCK_REGION_WW2G4) - read
	 * getConfig().lorawan.region directly for those.
	 */
	WisBlockRUI3Band getRegion() const;
	/**
	 * @brief Set the LoRaWAN data rate
	 *
	 * Returns true if this DR is actually active on the radio right now.
	 * A false return doesn't mean the request was rejected outright - see
	 * LoRaWANEngine::setADR()'s doc comment: the requested DR is stored
	 * either way and retried automatically after every uplink until the
	 * network's channel list allows it, most commonly right after a fresh
	 * join, before its post-join NewChannelReq MAC commands have landed.
	 *
	 * @param dataRate Data rate index (region specific)
	 * @return true if the data rate is active on the radio now
	 */
	bool setDataRate(uint8_t dataRate);
	/**
	 * @brief Set the LoRaWAN device class (A, B or C)
	 *
	 * See setDataRate()'s doc comment - same underlying mechanism and same meaning for the return value:
	 * fails (returns false) if the device isn't joined yet, and is retried automatically the moment it is
	 * - see LoRaWANEngine::setDeviceClass()'s doc comment.
	 *
	 * @param deviceClass Requested device class
	 * @return true if the class was applied, false if not joined yet
	 */
	bool setDeviceClass(WisBlockDeviceClass deviceClass);
	/**
	 * @brief Select the sub-band (channel mask) for many-channel regions
	 *
	 * Sub-band pre-selection for US915/AU915/CN470/CN470_RP_1_0; no effect elsewhere.
	 * See LoRaWANEngine::setChannelMask()'s doc comment for the encoding and why it matters -
	 * short version: it avoids wasting join attempts cycling through the wrong sub-band on
	 * these many-channel regions. Unlike setDeviceClass()/setADR(), safe to call before join().
	 *
	 * @param mask Channel mask, bit N enables sub-band N+1
	 * @return true if the mask was accepted
	 */
	bool setChannelMask(uint16_t mask);
	/**
	 * @brief Get the channel mask
	 * @return Current channel mask
	 */
	uint16_t getChannelMask() const { return lorawan.getChannelMask(); }
	/**
	 * @brief Enable or disable listen before talk (LBT)
	 *
	 * RUI3-compatible AT+LBT / AT+LBTRSSI / AT+LBTSCANTIME - see LoRaWANEngine::setLbtEnabled()'s/
	 * setLbtThreshold()'s doc comments for the full mechanism (support Korea, Japan) and an
	 * important finding about region selection *not* automatically applying a region-tuned
	 * threshold - if your target region's certification needs a specific value, set it here.
	 *
	 * @param enabled true to enable LBT
	 * @return true if the setting was applied
	 */
	bool setLbtEnabled(bool enabled) { ensureLoRaWANEngineStarted(); return lorawan.setLbtEnabled(enabled); }
	/**
	 * @brief Check if listen before talk (LBT) is enabled
	 * @return true if LBT is enabled
	 */
	bool getLbtEnabled() const { return lorawan.getLbtEnabled(); }
	/**
	 * @brief Set the LBT RSSI threshold
	 *
	 * @param thresholdDbm RSSI threshold in dBm
	 * @return true if the setting was applied
	 */
	bool setLbtThreshold(int16_t thresholdDbm) { ensureLoRaWANEngineStarted(); return lorawan.setLbtThreshold(thresholdDbm); }
	/**
	 * @brief Get the LBT RSSI threshold
	 * @return Threshold in dBm
	 */
	int16_t getLbtThreshold() const { return lorawan.getLbtThreshold(); }
	/**
	 * @brief Set the LBT channel scan time
	 *
	 * @param scanTimeMs Scan time in ms
	 * @return true if the setting was applied
	 */
	bool setLbtScanTime(uint32_t scanTimeMs) { ensureLoRaWANEngineStarted(); return lorawan.setLbtScanTime(scanTimeMs); }
	/**
	 * @brief Get the LBT channel scan time
	 * @return Scan time in ms
	 */
	uint32_t getLbtScanTime() const { return lorawan.getLbtScanTime(); }
	/**
	 * @brief Enable or disable adaptive data rate (ADR)
	 *
	 * See setDataRate()'s doc comment - same underlying mechanism and same meaning for the return value.
	 *
	 * @param enabled true to enable ADR
	 * @return true if the setting is active on the radio now
	 */
	bool setADR(bool enabled);
	/**
	 * @brief Set the LoRaWAN TX power
	 *
	 * @param txPowerIndex TX power index, region specific (0 = maximum power)
	 */
	void setTxPower(uint8_t txPowerIndex);
	/**
	 * @brief Select confirmed or unconfirmed uplinks
	 *
	 * @param confirmed true for confirmed uplinks
	 */
	void setConfirmedUplinks(bool confirmed);
	/**
	 * @brief Enable or disable automatic fetching of pending downlinks
	 *
	 * See WisBlockLoRaWANSettings::fetchPendingDownlinks's doc comment - default true (fixes
	 * the Class A "downlink queue never drains faster than my own send interval" bug).
	 *
	 * @param enabled true to fetch pending downlinks automatically
	 */
	void setFetchPendingDownlinks(bool enabled) { config.lorawan.fetchPendingDownlinks = enabled; ensureLoRaWANEngineStarted(); lorawan.setFetchPendingDownlinks(enabled); }
	/**
	 * @brief Check if pending downlinks are fetched automatically
	 * @return true if enabled
	 */
	bool getFetchPendingDownlinks() const { return config.lorawan.fetchPendingDownlinks; }

	/**
	 * @brief Start the join procedure (OTAA) or activate the session (ABP)
	 *
	 * See LoRaWANEngine::join()'s doc comment - in particular, do NOT call this from an
	 * onJoinFailed() callback when relying on AT+JOIN='s configured retry interval/max
	 * attempts; the library already retries failed joins on its own.
	 */
	void join();
	/**
	 * @brief Stop a running join procedure
	 */
	void stopJoin() { ensureLoRaWANEngineStarted(); lorawan.stopJoin(); }
	/**
	 * @brief Check if the device is joined to a network
	 * @return true if joined
	 */
	bool isJoined() const { return lorawan.isJoined(); }
	/**
	 * @brief Get the state of the join procedure
	 * @return Current join state
	 */
	WisBlockJoinState joinState() const { return lorawan.joinState(); }
	/**
	 * @brief Enable or disable automatic join at start
	 *
	 * See LoRaWANEngine::setAutoJoin()'s doc comment for the full mechanism these three cover
	 * (RUI3-compatible AT+JOIN parameters). Pure configuration - doesn't itself start the
	 * LoRaWAN engine or join anything; autoJoin is only consulted once the engine actually
	 * starts (via ensureLoRaWANEngineStarted()), and the other two only take effect on the
	 * next join()/retry. Safe to call before or after the engine has started either way.
	 *
	 * @param enabled true to join automatically
	 */
	void setAutoJoin(bool enabled)
	{
		config.lorawan.autoJoin = enabled;
		lorawan.setAutoJoin(enabled);
	}
	/**
	 * @brief Check if automatic join is enabled
	 * @return true if enabled
	 */
	bool getAutoJoin() const { return config.lorawan.autoJoin; }
	/**
	 * @brief Set the interval between join attempts
	 *
	 * @param seconds Interval in seconds (limited to 7 - 255)
	 */
	void setJoinReattemptInterval(uint8_t seconds)
	{
		lorawan.setJoinReattemptInterval(seconds); // clamps to RUI3's 7-255s range
		config.lorawan.joinReattemptIntervalS = lorawan.getJoinReattemptInterval();
	}
	/**
	 * @brief Get the interval between join attempts
	 * @return Interval in seconds
	 */
	uint8_t getJoinReattemptInterval() const { return config.lorawan.joinReattemptIntervalS; }
	/**
	 * @brief Set the maximum number of join attempts
	 *
	 * @param attempts Number of attempts, 0 = retry forever
	 */
	void setMaxJoinAttempts(uint8_t attempts)
	{
		config.lorawan.maxJoinAttempts = attempts;
		lorawan.setMaxJoinAttempts(attempts);
	}
	/**
	 * @brief Get the maximum number of join attempts
	 * @return Number of attempts, 0 = retry forever
	 */
	uint8_t getMaxJoinAttempts() const { return config.lorawan.maxJoinAttempts; }
	/**
	 * @brief Queue a LoRaWAN uplink
	 *
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
	 *
	 * @param port LoRaWAN port (1 - 223)
	 * @param data Payload
	 * @param length Payload length in bytes, 0 sends an empty uplink
	 * @return true if the uplink was accepted
	 */
	bool sendLoRaWAN(uint8_t port, const uint8_t *data, uint8_t length);
	/**
	 * @brief Request a link check from the network with the next uplink
	 */
	void requestLinkCheck() { ensureLoRaWANEngineStarted(); lorawan.requestLinkCheck(); }
	/**
	 * @brief Get the last link check result
	 *
	 * @param out Receives the result
	 * @return true if a result is available
	 */
	bool getLinkCheckResult(WisBlockLinkCheckResult &out) const { return lorawan.getLinkCheckResult(out); }
	/**
	 * @brief Set the link check mode (AT+LINKCHECK)
	 *
	 * See LoRaWANEngine::setLinkCheckMode()'s doc comment for the full RUI3-compatible behavior.
	 *
	 * @param mode 0 = off, 1 = once, 2 = with every uplink
	 */
	void setLinkCheckMode(uint8_t mode) { lorawan.setLinkCheckMode(mode); }
	/**
	 * @brief Get the link check mode
	 * @return Link check mode
	 */
	uint8_t getLinkCheckMode() const { return lorawan.getLinkCheckMode(); }
	/**
	 * @brief Request the network time with the next uplink
	 */
	void requestDeviceTime() { ensureLoRaWANEngineStarted(); lorawan.requestDeviceTime(); }
	/**
	 * @brief Set the Class B ping slot periodicity (AT+PGSLOT)
	 *
	 * RUI3-compatible AT+PGSLOT/AT+BFREQ/AT+BTIME - see LoRaWANEngine::setPingSlotPeriodicity()'s/
	 * getBeaconFrequencyAndDr()'s/getBeaconTime()'s doc comments. AT+BGW (gateway GPS/NetID/GwID
	 * from the beacon's GwSpecific field) is not implemented - it needs raw beacon payload
	 * decoding this library doesn't currently do (see LoRaWANEngine.h's Class B section for
	 * what is/isn't covered).
	 *
	 * @param periodicity Periodicity (0 - 7)
	 * @return true if the value was accepted
	 */
	bool setPingSlotPeriodicity(uint8_t periodicity);
	/**
	 * @brief Get the Class B ping slot periodicity
	 * @return Periodicity (0 - 7)
	 */
	uint8_t getPingSlotPeriodicity() const { return lorawan.getPingSlotPeriodicity(); }
	/**
	 * @brief Get the Class B beacon frequency and data rate (AT+BFREQ)
	 *
	 * @param frequencyHz Receives the beacon frequency in Hz
	 * @param dr Receives the beacon data rate
	 * @return true if the values are available
	 */
	bool getBeaconFrequencyAndDr(uint32_t &frequencyHz, uint8_t &dr) const { return lorawan.getBeaconFrequencyAndDr(frequencyHz, dr); }
	/**
	 * @brief Get the time of the last received Class B beacon (AT+BTIME)
	 * @return Beacon time, 0 if none was received
	 */
	uint32_t getBeaconTime() const { return lorawan.getBeaconTime(); }
	/**
	 * @brief Get the device address of the current session
	 *
	 * See LoRaWANEngine::getDevAddr()'s doc comment.
	 *
	 * @return Device address
	 */
	uint32_t getDevAddr() const { return lorawan.getDevAddr(); }

	// --- LoRaWAN multicast groups (RUI3-compatible AT+ADDMULC/AT+RMVMULC/AT+LSTMULC) ------
	/**
	 * @brief Configure a multicast group
	 *
	 * See LoRaWANEngine::setMulticastGroup()'s doc comment for the full picture - this is a
	 * thin pass-through, group-ID-keyed the same way.
	 *
	 * @param groupId Group ID (0 - 3)
	 * @param deviceClass Class B or C
	 * @param devAddr Multicast address
	 * @param nwkSKey Multicast network session key, 16 bytes
	 * @param appSKey Multicast application session key, 16 bytes
	 * @param frequencyHz Frequency in Hz
	 * @param dataRate Data rate
	 * @param periodicity Ping slot periodicity, Class B only
	 * @return true if the group was configured
	 */
	bool setMulticastGroup(uint8_t groupId, WisBlockDeviceClass deviceClass, uint32_t devAddr,
							const uint8_t nwkSKey[16], const uint8_t appSKey[16], uint32_t frequencyHz,
							uint8_t dataRate, uint8_t periodicity = 0)
	{
		ensureLoRaWANEngineStarted();
		return lorawan.setMulticastGroup(groupId, deviceClass, devAddr, nwkSKey, appSKey, frequencyHz, dataRate,
										  periodicity);
	}
	/**
	 * @brief Remove a multicast group
	 *
	 * @param groupId Group ID (0 - 3)
	 * @return true if the group was removed
	 */
	bool removeMulticastGroup(uint8_t groupId) { return lorawan.removeMulticastGroup(groupId); }
	/**
	 * @brief Get the configuration of a multicast group
	 *
	 * @param groupId Group ID (0 - 3)
	 * @return Pointer to the group, or null if not configured
	 */
	const WisBlockMulticastGroup *getMulticastGroup(uint8_t groupId) const { return lorawan.getMulticastGroup(groupId); }
	/**
	 * @brief Find a multicast group by its address
	 *
	 * @param devAddr Multicast address
	 * @return Group ID, or a negative value if not found
	 */
	int findMulticastGroupByDevAddr(uint32_t devAddr) const { return lorawan.findMulticastGroupByDevAddr(devAddr); }

	// --- LoRa P2P setup --------------------------------------------------
	/**
	 * @brief Set the P2P frequency
	 *
	 * @param frequencyHz Frequency in Hz
	 */
	void setP2PFrequency(uint32_t frequencyHz);
	/**
	 * @brief Set the P2P spreading factor
	 *
	 * @param sf Spreading factor (7 - 12)
	 */
	void setP2PSpreadingFactor(uint8_t sf);
	/**
	 * @brief Set the P2P bandwidth
	 *
	 * @param bw Bandwidth (WISBLOCK_BW_125 ...)
	 */
	void setP2PBandwidth(WisBlockP2PBandwidth bw);
	/**
	 * @brief Set the P2P coding rate
	 *
	 * @param cr Coding rate (WISBLOCK_CR_4_5 ...)
	 */
	void setP2PCodingRate(WisBlockP2PCodingRate cr);
	/**
	 * @brief Set the P2P preamble length
	 *
	 * @param symbols Preamble length in symbols
	 */
	void setP2PPreambleLength(uint16_t symbols);
	/**
	 * @brief Set the P2P TX power
	 *
	 * @param dbm TX power in dBm
	 */
	void setP2PTxPower(int8_t dbm);
	/**
	 * @brief Enable or disable CAD before sending
	 *
	 * @param enabled true to run a channel activity detection before every transmission
	 */
	void setP2PCad(bool enabled);
	/**
	 * @brief Enable or disable the boosted RX gain
	 *
	 * Trades RX current for sensitivity - see WisBlockP2PSettings::rxBoostedGainEnabled's
	 * doc comment for the ~4-5mA-vs-a-few-dB tradeoff. Takes effect on the
	 * next CAD/RX/TX (applied via LoRaP2PEngine::applyRadioParams(), same
	 * as every other P2P radio parameter).
	 *
	 * @param enabled true for boosted gain (more sensitivity, more current)
	 */
	void setP2PRxBoostedGain(bool enabled);
	/**
	 * @brief Enable or disable IQ inversion
	 *
	 * Invert IQ on TX and RX (RUI3 AT+IQINVER). Both ends of a link must use the same value.
	 *
	 * @param enabled true to invert the IQ signals
	 */
	void setP2PIqInversion(bool enabled);
	/**
	 * @brief Set the P2P sync word
	 *
	 * 16-bit LoRa sync word (RUI3 AT+SYNCWORD): 0x1424 private (default), 0x3444 public. Both ends must match.
	 *
	 * @param syncWord 16 bit sync word
	 */
	void setP2PSyncWord(uint16_t syncWord);
	/**
	 * @brief Send a P2P packet
	 *
	 * @param data Payload
	 * @param length Payload length in bytes
	 * @return true if the transmission was started
	 */
	bool sendP2P(const uint8_t *data, uint8_t length);
	/**
	 * @brief Start receiving P2P packets
	 *
	 * @param timeoutMs Receive timeout in ms, 0 = continuous
	 */
	void startP2PReceive(uint32_t timeoutMs = 0);
	/**
	 * @brief Start receiving with the RX duty cycle mode
	 *
	 * See LoRaP2PEngine::startReceiveDutyCycle()'s doc comment for the full picture.
	 *
	 * @param rxTimeMs Receive window in ms
	 * @param sleepTimeMs Sleep time between windows in ms
	 */
	void startP2PReceiveDutyCycle(uint32_t rxTimeMs, uint32_t sleepTimeMs);
	/**
	 * @brief Get the P2P radio settings
	 *
	 * Read-only access to the currently applied P2P radio settings - frequency, SF, bandwidth, preamble length, etc.
	 *
	 * @return Current P2P settings
	 */
	const WisBlockP2PSettings &getP2PSettings() const { return config.p2p; }
	/**
	 * @brief Calculate the RX duty cycle timing
	 *
	 * See LoRaP2PEngine::computeRxDutyCycleTiming()'s doc comment for the full picture.
	 *
	 * @param rxTimeMs Receives the RX window in ms
	 * @param sleepTimeMs Receives the sleep time in ms
	 * @param marginSymbols Safety margin in symbols
	 * @return true if a usable timing exists
	 */
	bool computeP2PRxDutyCycleTiming(uint32_t &rxTimeMs, uint32_t &sleepTimeMs, uint8_t marginSymbols = 5) const
	{
		return p2p.computeRxDutyCycleTiming(rxTimeMs, sleepTimeMs, marginSymbols);
	}
	/**
	 * @brief Calculate the RX duty cycle timing for a known transmitter preamble
	 *
	 * Prefer this overload over the one above whenever you know the
	 * transmitting node's actual preamble length - which is effectively
	 * always, since it's usually a compile-time constant on the sending
	 * side too. See LoRaP2PEngine::computeRxDutyCycleTiming()'s doc
	 * comment for why the receiver's own configured preamble length isn't
	 * a reliable substitute for it.
	 *
	 * @param txPreambleLengthSymbols Preamble length of the transmitting node
	 * @param rxTimeMs Receives the RX window in ms
	 * @param sleepTimeMs Receives the sleep time in ms
	 * @param marginSymbols Safety margin in symbols
	 * @return true if a usable timing exists
	 */
	bool computeP2PRxDutyCycleTiming(uint16_t txPreambleLengthSymbols, uint32_t &rxTimeMs, uint32_t &sleepTimeMs,
									  uint8_t marginSymbols = 5) const
	{
		return p2p.computeRxDutyCycleTiming(txPreambleLengthSymbols, rxTimeMs, sleepTimeMs, marginSymbols);
	}
	/**
	 * @brief Stop receiving and put the radio into standby
	 */
	void stopP2PReceive();
	/**
	 * @brief Start a channel activity detection (CAD), the result comes through onP2PCadResult()
	 */
	void startP2PCad();
	/**
	 * @brief Put the radio into sleep mode
	 *
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
	/**
	 * @brief Save the current configuration to flash
	 *
	 * Persists the current live config to the regular *user* flash slot. Returns false on write failure.
	 *
	 * @return true if the data was written
	 */
	bool saveConfig();
	/**
	 * @brief Reload the saved configuration from flash
	 *
	 * Reloads the *user* slot from flash into the current live config,
	 * discarding any unsaved in-RAM changes since the last saveConfig() -
	 * see restoreFactoryDefaults() below for reaching further back, to the
	 * factory backup instead. Returns false (defaults loaded) if nothing
	 * has ever been saved.
	 *
	 * @return true if a saved configuration was found, false if the defaults were loaded
	 */
	bool restoreConfig();
	/**
	 * @brief Save the current configuration as factory defaults
	 *
	 * Snapshots the CURRENT live configuration (not compiled-in struct
	 * defaults) into a separate "factory" flash slot, untouched by
	 * ordinary saveConfig()/restoreConfig() traffic - see AT+FACTORY in
	 * WisBlockLoRaAT.cpp for the intended one-time production flow this is
	 * part of (set a unique DevEUI, then call this, then reboot).
	 * Returns false on flash write failure.
	 *
	 * @return true if the data was written
	 */
	bool saveFactoryDefaults();
	/**
	 * @brief Restore the factory defaults
	 *
	 * Loads the factory-slot config saved by saveFactoryDefaults(), applies
	 * it as the current live configuration, and persists it into the
	 * regular user flash slot too - so it's what restoreConfig()/
	 * AT+RESTORE reload from now on, not just a one-off in-RAM change. See
	 * ATR in WisBlockLoRaAT.cpp. Returns false (config left untouched) if
	 * no factory backup has ever been saved.
	 *
	 * @return true if the factory defaults were restored
	 */
	bool restoreFactoryDefaults();
	/**
	 * @brief Check if a valid saved configuration is in use
	 *
	 * Tells whether the library is running on a valid saved configuration (TRUE), or on the
	 * built-in defaults (FALSE). Use it right after begin() to decide whether the application has
	 * to set up the configuration (and then call saveConfig()) or can simply use what is stored:
	 *
	 *   lora.begin();
	 *   if (!lora.hasValidConfig()) { ...set everything up...; lora.saveConfig(); }
	 *
	 * FALSE after begin() means: nothing was ever saved (new or chip-erased device), the stored
	 * data is corrupted, or it was written by an incompatible library version. TRUE means begin()
	 * found and loaded a valid user slot. The result follows the user slot afterwards:
	 * saveConfig() and restoreFactoryDefaults() that succeed make it TRUE, restoreConfig() sets it
	 * to what it returns. Changing a setting does not change it, only saving does. It says nothing
	 * about the content: a saved configuration can still lack the keys you need.
	 *
	 * @return true if a valid saved configuration was loaded
	 */
	bool hasValidConfig() const { return configFromFlash; }
	/**
	 * @brief Get the complete live configuration
	 * @return Current configuration (read only)
	 */
	const WisBlockPersistedConfig &getConfig() const { return config; }

	// --- Low power ----------------------------------------------------
	/**
	 * @brief Enable or disable low power mode
	 *
	 * @param enabled true to enable low power mode
	 */
	void setLowPowerEnabled(bool enabled) { config.lowPowerEnabled = enabled; }
	/**
	 * @brief Check if low power mode is enabled
	 * @return true if enabled
	 */
	bool isLowPowerEnabled() const { return config.lowPowerEnabled; }
	/**
	 * @brief Sleep the MCU
	 *
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
	 *
	 * @param maxDurationMs Maximum sleep time in ms
	 */
	void sleep(uint32_t maxDurationMs = 0);

	/**
	 * @brief Start the background task that handles the LoRa events
	 *
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
	 *
	 * @return true if the task is running, false if FreeRTOS is not available
	 */
	bool enableBackgroundTask();

	/**
	 * @brief Lock the LoRa Basics Modem against access from other tasks
	 *
	 * Guards any code that calls into this library's API (and therefore
	 * into LBM) from a task/context other than whichever one owns
	 * background task mode - needed by WisBlockLoRaAT::enableBackgroundRx(),
	 * since AT commands like AT+SEND touch the same LBM engine state the
	 * background task does, from a different task (the USB CDC RX
	 * callback's context). No-op if enableBackgroundTask() was never
	 * called successfully.
	 */
	void lockLbm();
	/**
	 * @brief Release the lock taken with lockLbm()
	 */
	void unlockLbm();

	// --- Callback registration (LoRaWAN) ------------------------------
	/**
	 * @brief Register the join success callback
	 *
	 * @param cb Function called after a successful join
	 */
	void onJoinSuccess(LoRaWANEngine::JoinSuccessCb cb) { lorawan.onJoinSuccess(cb); }
	/**
	 * @brief Register the join failed callback
	 *
	 * @param cb Function called after a failed join attempt
	 */
	void onJoinFailed(LoRaWANEngine::JoinFailedCb cb) { lorawan.onJoinFailed(cb); }
	/**
	 * @brief Register the LoRaWAN TX finished callback
	 *
	 * @param cb Function called after an uplink
	 */
	void onLoRaWANTxFinished(LoRaWANEngine::TxFinishedCb cb) { lorawan.onTxFinished(cb); }
	/**
	 * @brief Register the LoRaWAN RX callback
	 *
	 * @param cb Function called for every received downlink
	 */
	void onLoRaWANRxFinished(LoRaWANEngine::RxFinishedCb cb) { lorawan.onRxFinished(cb); }
	/**
	 * @brief Register the network time answer callback
	 *
	 * @param cb Function called with the answer to requestDeviceTime()
	 */
	void onTimeRequestAnswer(LoRaWANEngine::TimeRequestCb cb) { lorawan.onTimeRequestAnswer(cb); }
	/**
	 * @brief Register the link check answer callback
	 *
	 * @param cb Function called with the answer to requestLinkCheck()
	 */
	void onLinkCheckAnswer(LoRaWANEngine::LinkCheckCb cb) { lorawan.onLinkCheckAnswer(cb); }

	// --- Callback registration (LoRa P2P) -----------------------------
	/**
	 * @brief Register the P2P TX finished callback
	 *
	 * @param cb Function called after a P2P transmission
	 */
	void onP2PTxFinished(LoRaP2PEngine::TxFinishedCb cb) { p2p.onTxFinished(cb); }
	/**
	 * @brief Register the P2P RX callback
	 *
	 * @param cb Function called for every received P2P packet, and with length 0 on an RX timeout
	 */
	void onP2PRxFinished(LoRaP2PEngine::RxFinishedCb cb) { p2p.onRxFinished(cb); }
	/**
	 * @brief Register the P2P CAD callback
	 *
	 * @param cb Function called with the result of a CAD
	 */
	void onP2PCadResult(LoRaP2PEngine::CadResultCb cb) { p2p.onCadResult(cb); }

private:
	WisBlockPersistedConfig config;
	LoRaWANEngine lorawan;
	LoRaP2PEngine p2p;
	bool began = false;
	bool configFromFlash = false; // see hasValidConfig()
	bool backgroundTaskActive = false;
	bool lorawanEngineStarted = false;

	/**
	 * @brief Apply the stored LoRaWAN settings to the LoRaWAN engine
	 */
	void applyLoRaWANSettings();
	/**
	 * @brief Apply the stored P2P settings to the P2P engine
	 */
	void applyP2PSettings();

	/**
	 * @brief Start the LoRaWAN engine if this was not done yet
	 *
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

	/**
	 * @brief Process the LoRaWAN and P2P events
	 *
	 * Does the actual event-processing work; handleEvents() (loop()-facing,
	 * guarded against double-processing once background task mode is
	 * active) and handleEventsStatic() (background-task-facing, always
	 * calls this directly) both funnel through here - see the .cpp for why
	 * they can't just both call handleEvents() itself.
	 *
	 * @return Time in ms until this function must be called again
	 */
	uint32_t handleEventsInternal();

	// WisBlockLbmTask's background task calls a plain C-style function
	// pointer (no captures/context), so a single static instance pointer
	// bridges that back to this instance's handleEvents(). Only one
	// WisBlockLoRaWAN instance can use background task mode at a time -
	// consistent with the rest of this library, which only ever supports
	// one radio/one stack.
	static WisBlockLoRaWAN *activeInstanceForTask;
	/**
	 * @brief Background task entry point for handleEvents()
	 * @return Time in ms until this function must be called again
	 */
	static uint32_t handleEventsStatic();
};

#endif // WISBLOCK_LORAWAN_H
