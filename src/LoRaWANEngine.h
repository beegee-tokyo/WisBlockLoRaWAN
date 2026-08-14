/**
 * @file LoRaWANEngine.h
 * @brief Wraps Semtech LoRa Basics Modem v4.9.0's `smtc_modem_api` (vendored
 * at src/lbm/smtc_modem_api/) for join, uplink, class switching, ADR, relay,
 * link check and device-time request.
 *
 * Every public method here maps to real, verified `smtc_modem_api` /
 * `smtc_modem_utilities` calls - see LoRaWANEngine.cpp for the exact
 * function names and any real API limitations discovered while wiring this
 * up (e.g. v4.9.0 has no direct "set TX power" call; see setTxPower()).
 */
#ifndef LORAWAN_ENGINE_H
#define LORAWAN_ENGINE_H

#include "LoRaWANRelay.h"
#include "WisBlockLoRaWANTypes.h"

class LoRaWANEngine
{
public:
	using JoinSuccessCb = void (*)();
	using JoinFailedCb = void (*)();
	using TxFinishedCb = void (*)(const WisBlockTxResult &);
	using RxFinishedCb = void (*)(const WisBlockRxResult &);
	using TimeRequestCb = void (*)(bool success, const WisBlockTimeAnswer &);
	using LinkCheckCb = void (*)(bool success, const WisBlockLinkCheckResult &);

	void begin(const WisBlockLoRaWANSettings &settings);
	void applySettings(const WisBlockLoRaWANSettings &settings);

	void join();
	bool isJoined() const;
	WisBlockJoinState joinState() const;

	/**
	 * Queues an uplink with LBM. Unlike a bare pass-through to
	 * smtc_modem_request_uplink(), this refuses (returns false, no LBM call
	 * made at all) if a previous send() is still in flight - LBM holds at
	 * most one pending uplink, and calling this again before the previous
	 * one has actually been dispatched silently discards it (still
	 * reported honestly afterward via onTxFinished() with success=false
	 * and SMTC_MODEM_EVENT_TXDONE_NOT_SENT - that part of LBM's behavior
	 * was always correct; nothing was being mis-reported). See the
	 * "First send after join lost" / uplinkPending README notes for the
	 * real log capture that prompted this - a full Class A TX+RX1+RX2
	 * cycle, especially amid post-join MAC command negotiation, routinely
	 * took longer than a naive fixed-interval application timer expected,
	 * so periodic sends were racing (and losing to) the send already in
	 * flight, wasting a frame counter each time.
	 */
	bool send(uint8_t port, const uint8_t *data, uint8_t length, bool confirmed);

	void setDeviceClass(WisBlockDeviceClass deviceClass);
	/**
	 * FIX (root cause of a confirmed, reproducible bug: setADR(false) with
	 * a fixed DR "silently" not taking effect - the frame's ADR bit stayed
	 * 1, the network's own ADR engine kept issuing LinkADRReq, and the
	 * device kept obeying them, all despite the app correctly calling
	 * this. Traced to LBM's smtc_modem.c: smtc_modem_adr_set_profile()
	 * builds its custom single-DR distribution by intersecting the
	 * requested DR against smtc_modem_custom_dr_distribution_to_tab()'s
	 * mask_dr_allowed - the union of DR ranges supported by every
	 * *currently enabled* uplink channel. Right after a fresh join, only
	 * the region's default join channels are enabled; if the requested DR
	 * isn't in their range (common - e.g. AS923's default channels don't
	 * cover every DR the network's later NewChannelReq-added channels do),
	 * the call fails outright with SMTC_MODEM_RC_INVALID and LBM's own
	 * trace prints "ADR with a bad DataRate value" - and silently leaves
	 * the ADR profile exactly as it was before the call (still
	 * NETWORK_CONTROLLED, LBM's own default), not CUSTOM. This function
	 * used to discard that return code entirely - applyAdrProfile() was a
	 * bare void, so this failure was invisible even in normal (non-debug)
	 * builds.
	 *
	 * Returns false if the requested DR isn't currently achievable given
	 * the presently-enabled channels - config.lorawan.adrEnabled/dataRate
	 * still reflect what you asked for (so a later retry will use the
	 * right values), but the radio is NOT yet running with ADR actually
	 * off. See handleEvents()'s TXDONE case for the automatic retry this
	 * triggers - once the network's post-join channel-widening downlinks
	 * (visible in your own log as "Cmd new_channel_parser") land and
	 * enable wider-range channels, a retry after the next uplink will
	 * very likely succeed on its own with no application action needed -
	 * but until it does, don't assume ADR is actually off just because
	 * you called this.
	 */
	bool setADR(bool enabled);
	void setTxPower(uint8_t txPowerIndex);
	/** See setADR()'s doc comment - same underlying mechanism and same meaning for the return value. */
	bool setDataRate(uint8_t dataRate);

	/** Requests link check; answer arrives later as an SMTC_MODEM_EVENT_LINK_CHECK event. */
	void requestLinkCheck();
	/**
	 * Pull-style alternative to onLinkCheckAnswer(): fetches the most
	 * recently received link check answer directly from LBM
	 * (smtc_modem_get_lorawan_link_check_data), rather than waiting for the
	 * push callback. Returns false if no link check has ever been answered
	 * yet (LBM has nothing cached to return). Safe to call at any time,
	 * not just right after a request - the value stays cached until the
	 * next successful link check answer overwrites it.
	 */
	bool getLinkCheckResult(WisBlockLinkCheckResult &out) const;
	/**
	 * RUI3-compatible AT+LINKCHECK mode: 0 = disabled, 1 = request a link
	 * check on the very next uplink only (auto-reverts to 0 once that
	 * uplink is queued), 2 = request one automatically on every uplink
	 * from here on, until set back to 0. Checked and acted on inside
	 * send() - see its doc comment - so it applies uniformly regardless of
	 * whether the uplink was triggered via the AT layer or a direct
	 * WisBlockLoRaWAN::sendLoRaWAN() call, matching RUI3's own behavior.
	 */
	void setLinkCheckMode(uint8_t mode) { linkCheckMode = mode; }
	uint8_t getLinkCheckMode() const { return linkCheckMode; }
	/** Requests device time; answer arrives later as an SMTC_MODEM_EVENT_LORAWAN_MAC_TIME event. */
	void requestDeviceTime();

	void setRelayMode(WisBlockRelayMode mode); // see LoRaWANRelay.h
	void configureRelayED(const WisBlockRelayEDConfig &cfg);
	void configureRelayServing(const WisBlockRelayServingConfig &cfg);
	bool addRelayTrustedDevice(const WisBlockRelayTrustedDevice &device);
	bool removeRelayTrustedDevice(uint8_t index);

	/** Pumps smtc_modem_run_engine() + drains smtc_modem_get_event(). Call every loop().
	 * Returns the ms budget smtc_modem_run_engine() itself reports before it must be
	 * called again - required for background task mode (WisBlockLbmTask) to self-schedule
	 * its next wake; safe to ignore in loop()-polled usage. */
	uint32_t handleEvents();

	void onJoinSuccess(JoinSuccessCb cb) { joinSuccessCb = cb; }
	void onJoinFailed(JoinFailedCb cb) { joinFailedCb = cb; }
	void onTxFinished(TxFinishedCb cb) { txFinishedCb = cb; }
	void onRxFinished(RxFinishedCb cb) { rxFinishedCb = cb; }
	void onTimeRequestAnswer(TimeRequestCb cb) { timeRequestCb = cb; }
	void onLinkCheckAnswer(LinkCheckCb cb) { linkCheckCb = cb; }

private:
	WisBlockLoRaWANSettings settings;
	WisBlockJoinState currentJoinState = WISBLOCK_JOIN_IDLE;
	// FIX: see send()'s doc comment - tracks whether an uplink is currently
	// queued/in-flight with LBM. Set true the moment send() successfully
	// calls smtc_modem_request_uplink(); cleared unconditionally the
	// moment SMTC_MODEM_EVENT_TXDONE fires (success or not - either way
	// LBM is no longer holding a pending uplink for us afterward). Reset
	// to false in begin(), since a fresh smtc_modem_init() starts with
	// nothing pending regardless of whatever this flag happened to be
	// left at from a previous run.
	bool uplinkPending = false;
	uint8_t linkCheckMode = 0; // see setLinkCheckMode()'s doc comment
	// FIX: see setADR()'s doc comment. Set false whenever applyAdrProfile()
	// fails to actually push the requested CUSTOM (ADR-off) profile to LBM
	// - checked and retried once per uplink in handleEvents()'s TXDONE
	// case until it succeeds. Left true (a harmless no-op retry condition)
	// when ADR is on, since NETWORK_CONTROLLED essentially never fails
	// this validation the same way.
	bool adrProfileApplied = true;

	JoinSuccessCb joinSuccessCb = nullptr;
	JoinFailedCb joinFailedCb = nullptr;
	TxFinishedCb txFinishedCb = nullptr;
	RxFinishedCb rxFinishedCb = nullptr;
	TimeRequestCb timeRequestCb = nullptr;
	LinkCheckCb linkCheckCb = nullptr;

	bool applyAdrProfile(); // builds the custom dr_custom_distribution_data table when ADR is off; see setADR()'s doc comment for the return value
};

#endif // LORAWAN_ENGINE_H
