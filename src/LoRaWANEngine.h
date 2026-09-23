/**
 * @file LoRaWANEngine.h
 * @brief Wraps Semtech LoRa Basics Modem v4.9.0's `smtc_modem_api` (vendored
 * at src/lbm/smtc_modem_api/) for join, uplink, class switching, ADR,
 * link check and device-time request.
 *
 * Every public method here maps to real, verified `smtc_modem_api` /
 * `smtc_modem_utilities` calls - see LoRaWANEngine.cpp for the exact
 * function names and any real API limitations discovered while wiring this
 * up (e.g. v4.9.0 has no direct "set TX power" call; see setTxPower()).
 */
#ifndef LORAWAN_ENGINE_H
#define LORAWAN_ENGINE_H

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
	/** RUI3's AT+JOIN=0:... ("stop joining"). See this method's implementation for what it
	 * actually cancels - both an in-progress OTAA join and this library's own custom-interval
	 * retry timer, if either is active. */
	void stopJoin();
	bool isJoined() const;
	WisBlockJoinState joinState() const;
	/**
	 * RUI3-compatible AT+JOIN / api.lorawan.join parameters. See
	 * WisBlockLoRaWANSettings' doc comments for the persisted fields these
	 * setters write - all three are stored and take effect on the next
	 * join()/retry, matching RUI3's own "configure then join" pattern
	 * (AT+JOIN's parameters are set and used together, not applied
	 * retroactively to a join already in progress).
	 *
	 * Reattempt interval and max attempts are both new mechanisms this
	 * library didn't have before: LBM's own join task (lorawan_join_management.c)
	 * retries automatically forever on failure, using its own region-
	 * appropriate, spec-compliant backoff timing - which this library
	 * previously just left alone entirely. That backoff is not something
	 * this library should override downward (it exists for good regulatory
	 * reasons), but a longer, fixed interval - exactly what "extend the
	 * time between attempts to save battery" asks for - is a legitimate
	 * choice to make instead of it. When either setting is at its default
	 * (interval 8s, matching RUI3's own default, or maxAttempts 0), this
	 * library's behavior is unchanged from before - LBM's automatic retry
	 * runs as it always has. As soon as either is set to a non-default
	 * value, this library takes over scheduling retries itself: each
	 * SMTC_MODEM_EVENT_JOINFAIL cancels LBM's own auto-scheduled next
	 * attempt (smtc_modem_leave_network() - see its own doc comment,
	 * "...or cancels an ongoing join process") and instead waits exactly
	 * joinReattemptIntervalS seconds (tracked in handleEvents(), including
	 * clamping its own returned sleep duration so a background-task-mode
	 * application actually wakes up in time - see the FIX comment there)
	 * before calling smtc_modem_join_network() again itself. maxJoinAttempts
	 * counts these attempts and, once reached, stops retrying entirely
	 * (also via smtc_modem_leave_network()) and reports WISBLOCK_JOIN_GAVE_UP
	 * from joinState() - joinFailedCb() itself still fires on every
	 * individual failed attempt as before, so existing applications relying
	 * on that aren't affected; checking joinState() from inside that
	 * callback is how to tell whether this specific failure was the final
	 * one.
	 */
	void setAutoJoin(bool enabled) { settings.autoJoin = enabled; }
	bool getAutoJoin() const { return settings.autoJoin; }
	/** Clamped to RUI3's own valid range (7-255s) - a value outside it is clamped rather than
	 * rejected, since "closest valid value" is more useful here than refusing the call outright. */
	void setJoinReattemptInterval(uint8_t seconds);
	uint8_t getJoinReattemptInterval() const { return settings.joinReattemptIntervalS; }
	void setMaxJoinAttempts(uint8_t attempts) { settings.maxJoinAttempts = attempts; }
	uint8_t getMaxJoinAttempts() const { return settings.maxJoinAttempts; }

	/**
	 * Queues an uplink with LBM. Unlike a bare pass-through to
	 * smtc_modem_request_uplink(), this refuses outright (returns false, no
	 * LBM call made at all) only when a second send() arrives while one is
	 * already deferred (see below) - a true "nothing more can be done right
	 * now" case. LBM holds at most one pending uplink, and calling this
	 * again before the previous one has actually been dispatched silently
	 * discards it (still reported honestly afterward via onTxFinished()
	 * with success=false and SMTC_MODEM_EVENT_TXDONE_NOT_SENT - that part
	 * of LBM's behavior was always correct; nothing was being mis-reported).
	 * See the "First send after join lost" / uplinkPending README notes for
	 * the real log capture that prompted the original version of this
	 * guard - a full Class A TX+RX1+RX2 cycle, especially amid post-join
	 * MAC command negotiation, routinely took longer than a naive fixed-
	 * interval application timer expected, so periodic sends were racing
	 * (and losing to) the send already in flight, wasting a frame counter
	 * each time.
	 *
	 * FIX: a send() arriving while the uplink slot is already occupied -
	 * by this library's own FPending auto-fetch (see
	 * autoFetchUplinkPending's doc comment), by a real application send
	 * still in flight, or by an earlier deferred send only now being
	 * replayed - is deferred rather than refused, and still returns true:
	 * it genuinely will be transmitted, automatically, the moment whatever
	 * is currently in flight finishes (see deferredSend's doc comment and
	 * the SMTC_MODEM_EVENT_TXDONE case in handleEvents()). Only one level
	 * of deferral is ever held; a second send() arriving before the first
	 * deferred one has gone out still gets the original, unconditional
	 * refusal. Confirmed against two real device logs on a duty-cycle-
	 * constrained region: without this - or with an earlier, narrower
	 * version of this fix that only deferred when specifically the auto-
	 * fetch was in flight - an application's own regularly-scheduled send
	 * could land during any of several several-second delays this feature
	 * can introduce and be indistinguishable from a genuine double-send
	 * race, dropped even though the application did nothing wrong.
	 */
	bool send(uint8_t port, const uint8_t *data, uint8_t length, bool confirmed);

	bool setDeviceClass(WisBlockDeviceClass deviceClass);
	/**
	 * Pre-selects a sub-band for regions with more channels than a typical 8-channel gateway
	 * supports (US915, AU915, CN470, CN470_RP_1_0) - no effect elsewhere (EU868, AS923, ...).
	 * Mirrors RUI3's AT+MASK / api.lorawan.mask: bit N (0-indexed) enables sub-band N+1 (8
	 * channels each); 0 means no restriction (all channels enabled).
	 *
	 * Without this, a device joining on one of these regions has to try every sub-band the
	 * region defines in turn before it happens to land on the one the gateway actually
	 * listens on - each failed attempt is a wasted join request and, on a duty-cycled or
	 * battery-powered device, real airtime and time-to-first-join. Setting this to the known
	 * sub-band before the first join() call goes straight to it instead.
	 *
	 * Safe to call before joining (in fact that's the intended use) - unlike setDeviceClass()/
	 * setADR(), this does not require the device to already be joined, since it only affects
	 * which channels this device itself considers when choosing one to transmit on.
	 */
	bool setChannelMask(uint16_t mask);
	uint16_t getChannelMask() const;
	/**
	 * RUI3-compatible AT+LBT / AT+LBTRSSI / AT+LBTSCANTIME (support Korea, Japan). The
	 * underlying Listen-Before-Talk mechanism itself (a sniff-before-transmit check on the
	 * radio) is already fully implemented in the vendored LBM stack and needs nothing added
	 * here to function - per smtc_modem_lbt_set_state()'s own doc comment, it is silently
	 * enabled automatically for any region where it's regulatorily mandatory (LBM's own CSMA
	 * mechanism is the equivalent silent-default for regions where LBT is not mandatory).
	 * These calls only expose control over it: turning it on/off explicitly, and adjusting the
	 * RSSI threshold and scan (listen) duration LBM was otherwise left to its own generic
	 * defaults for (-80 dBm, ~5ms) - see setLbtThreshold()'s doc comment for why those defaults
	 * are worth overriding for a real deployment rather than assumed correct as-is.
	 *
	 * Safe to call before joining, like setChannelMask() - LBT is a local transmit-gating
	 * decision this device makes for itself, not something that requires having joined first.
	 */
	bool setLbtEnabled(bool enabled);
	bool getLbtEnabled() const;
	/**
	 * FIX/finding worth knowing, not a bug: every region in this vendored LBM tree defines its
	 * own "region-appropriate" LBT threshold constant (e.g. LBT_THRESHOLD_DBM_KR_920), but none
	 * of them are ever actually read by anything in LBM itself (confirmed - no caller anywhere
	 * for smtc_real_get_lbt_threshold_dbm()) - and every single one of those constants is -80
	 * dBm anyway, identical to smtc_lbt_init()'s own generic fallback, so this has no practical
	 * effect today regardless. In short: selecting KR920/a Japan-targeting AS923 variant does
	 * NOT itself apply any region-tuned LBT threshold - whatever LBM's generic default is (or
	 * whatever this call sets) is what every region actually uses. If your target region's
	 * certification requires a specific, different threshold, this is the call that needs to
	 * carry it - nothing in region selection will do it automatically.
	 *
	 * dBm, signed - e.g. -80. Threshold and scan time share one underlying LBM call
	 * (smtc_modem_lbt_set_parameters(), which also takes an RSSI measurement bandwidth this
	 * library doesn't expose - RUI3 doesn't either, so it stays at LBM's own default), so
	 * changing one here reads the other back from LBM first rather than risking clobbering it
	 * with a stale cached value.
	 */
	bool setLbtThreshold(int16_t thresholdDbm);
	int16_t getLbtThreshold() const;
	/** Listen duration in ms before deciding a channel is clear - LBM's own generic default is
	 * ~5ms. See setLbtThreshold()'s doc comment for why this and the threshold share one call. */
	bool setLbtScanTime(uint32_t scanTimeMs);
	uint32_t getLbtScanTime() const;
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
	/** See WisBlockLoRaWANSettings::fetchPendingDownlinks's doc comment. Takes effect
	 * immediately (just a locally-read behavior flag, not something pushed to LBM). */
	void setFetchPendingDownlinks(bool enabled) { settings.fetchPendingDownlinks = enabled; }
	bool getFetchPendingDownlinks() const { return settings.fetchPendingDownlinks; }
	/** Requests device time; answer arrives later as an SMTC_MODEM_EVENT_LORAWAN_MAC_TIME event. */
	void requestDeviceTime();

	/**
	 * RUI3-compatible AT+PGSLOT / Class B unicast ping slot periodicity, 0-7. Matches RUI3's own
	 * numbering exactly (0 = ~1s period, 7 = 128s, the maximum) - LBM's own
	 * smtc_modem_class_b_ping_slot_periodicity_t enum already uses this identical ordering, so
	 * the value passed through unchanged. Also triggers a PingSlotInfoReq MAC command
	 * (SMTC_MODEM_LORAWAN_MAC_REQ_PING_SLOT_INFO) so the network is actually told about the
	 * change - setting this locally without informing the network would leave the network
	 * scheduling downlinks for the old periodicity, breaking Class B reception rather than
	 * just being a no-op. Values above 7 are clamped rather than rejected, matching this
	 * library's convention elsewhere for simple numeric range setters.
	 */
	bool setPingSlotPeriodicity(uint8_t periodicity);
	uint8_t getPingSlotPeriodicity() const;
	/**
	 * RUI3-compatible AT+BFREQ (read-only): the data rate and frequency (Hz) of the next Class B
	 * beacon reception opportunity for the current region. Resolved via
	 * smtc_real_get_beacon_dr()/smtc_real_get_beacon_frequency() using the last valid received
	 * beacon's own embedded GPS time as the reference instant those functions need (some
	 * regions, e.g. US915/AU915, hop the beacon frequency over time; most others use one fixed
	 * frequency and the reference instant doesn't change the answer). Returns 0/0 before any
	 * beacon has ever been received.
	 */
	bool getBeaconFrequencyAndDr(uint32_t &frequencyHz, uint8_t &dr) const;
	/** RUI3-compatible AT+BTIME (read-only): seconds since the GPS epoch, taken from the last
	 * valid received beacon's own embedded time field - not this device's local clock, and not
	 * updated at all until at least one beacon has actually been received. 0 if none yet. */
	uint32_t getBeaconTime() const;

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
	// FIX (downlink-queue drain bug - confirmed against two real device
	// logs): tracks whether uplinkPending is true specifically because of
	// this library's own FPending auto-fetch uplink (see
	// SMTC_MODEM_EVENT_DOWNDATA in handleEvents()), as opposed to a real
	// application send. No longer used to gate whether a colliding send()
	// gets deferred (see send()'s doc comment - that's now unconditional
	// regardless of what's occupying the slot); kept so
	// SMTC_MODEM_EVENT_TXDONE can suppress the onTxFinished() callback for
	// the auto-fetch's own completions specifically - an application never
	// asked for those empty uplinks and has no reason to hear about them,
	// unlike a real send that merely had to wait its turn.
	bool autoFetchUplinkPending = false;
	// FIX: see send()'s doc comment. A single-slot queue - deliberately not
	// more than one - for an application send() that arrived while the
	// uplink slot was already occupied by anything else: this library's own
	// auto-fetch, a real send still in flight, or an earlier deferred send
	// only now being replayed. Replayed automatically via dispatchUplink()
	// the moment whatever was ahead of it clears uplinkPending on
	// SMTC_MODEM_EVENT_TXDONE, so the application's own uplink still goes
	// out - just delayed by however many duty-cycle-limited hops it took to
	// get there - instead of being silently dropped and requiring the
	// application to notice the false return and retry itself.
	//
	// CORRECTION: originally only accepted a deferral when
	// autoFetchUplinkPending was specifically true, on the theory that two
	// genuine application sends racing each other should still fail as
	// before. On a duty-cycle/LBT-constrained region, a single deferral can
	// itself take long enough to transmit that the application's *next*
	// regularly-scheduled send arrives before that replay's own TXDONE -
	// confirmed against a real device log where exactly this happened one
	// step later than the original fix accounted for. Deferring
	// unconditionally (still only ever one level deep) closes that gap
	// rather than just moving it.
	struct
	{
		bool valid = false;
		uint8_t port = 0;
		uint8_t data[242] = {0}; // SMTC_MODEM_MAX_LORAWAN_PAYLOAD_LENGTH - hardcoded rather than
								 // pulling smtc_modem_api.h into this header; matches
								 // WisBlockRxResult::data's own sizing in WisBlockLoRaWANTypes.h
		uint8_t length = 0;
		bool confirmed = false;
	} deferredSend;
	// FIX (downlink-queue-drain bug #4 - confirmed against two more real
	// device logs, one with fetchPendingDownlinks on and one off): the
	// auto-fetch above assumed SMTC_MODEM_EVENT_TXDONE for the uplink that
	// solicited a downlink is always processed before
	// SMTC_MODEM_EVENT_DOWNDATA for that same downlink, so uplinkPending
	// would already be false by the time this fires. That ordering is NOT
	// guaranteed - LBM can deliver them either way - and when DOWNDATA
	// arrives first, uplinkPending is still true (from the very uplink that
	// just solicited this downlink), so the auto-fetch's own `!uplinkPending`
	// check silently skipped it every time that ordering occurred, with no
	// deferral and no way to retry - unlike a real application send(), which
	// already had deferredSend to fall back on. When that happens, the
	// fetch is remembered here instead of just being dropped, and serviced
	// from SMTC_MODEM_EVENT_TXDONE the moment uplinkPending actually does
	// clear - which is correct regardless of which order the two events
	// arrived in, since TXDONE is unambiguous about when the slot is free.
	bool pendingDownlinkFetchRequested = false;
	uint8_t pendingDownlinkFetchPort = 1;
	uint8_t linkCheckMode = 0; // see setLinkCheckMode()'s doc comment
	// FIX: see setADR()'s doc comment. Set false whenever applyAdrProfile()
	// fails to actually push the requested CUSTOM (ADR-off) profile to LBM
	// - checked and retried once per uplink in handleEvents()'s TXDONE
	// case until it succeeds. Left true (a harmless no-op retry condition)
	// when ADR is on, since NETWORK_CONTROLLED essentially never fails
	// this validation the same way.
	bool adrProfileApplied = true;
	// FIX: see setDeviceClass()'s doc comment - smtc_modem_set_class()
	// requires the device to already be joined, so the very first call
	// (from applySettings(), which runs from begin(), before join())
	// always fails. Starts false after every begin() and is retried at
	// the moment the device actually becomes joined (SMTC_MODEM_EVENT_JOINED
	// / the synchronous ABP success path), with a TXDONE-time fallback
	// retry mirroring adrProfileApplied's, in case that first retry
	// somehow still didn't land (e.g. RETURN_BUSY_IF_TEST_MODE).
	bool classProfileApplied = false;
	// FIX: see setJoinReattemptInterval()'s/setMaxJoinAttempts()'s doc comments. Tracks failed
	// OTAA join attempts within the current join "cycle" - reset to 0 by join() itself (a fresh,
	// application-or-auto-triggered join request), NOT by the internal retry path in
	// handleEvents(), so it correctly counts across every automatic retry in between.
	uint8_t joinAttemptCount = 0;
	// FIX: when set, handleEvents() calls smtc_modem_join_network() again once millis() reaches
	// this deadline, instead of relying on LBM's own auto-scheduled retry (already cancelled via
	// smtc_modem_leave_network() when this was set - see the SMTC_MODEM_EVENT_JOINFAIL case).
	bool joinRetryScheduled = false;
	uint32_t nextJoinRetryAtMs = 0;
	// FIX: see computeLoRaWANAirtimeMs()'s doc comment in LoRaWANEngine.cpp
	// - captured in send() for whichever uplink is currently in flight,
	// consumed by the TXDONE handler in handleEvents() to fill in
	// WisBlockTxResult::airtimeMs, which previously always reported 0.
	uint8_t lastTxDr = 0;
	uint8_t lastTxPhyPayloadLen = 0;

	JoinSuccessCb joinSuccessCb = nullptr;
	JoinFailedCb joinFailedCb = nullptr;
	TxFinishedCb txFinishedCb = nullptr;
	RxFinishedCb rxFinishedCb = nullptr;
	TimeRequestCb timeRequestCb = nullptr;
	LinkCheckCb linkCheckCb = nullptr;

	bool applyAdrProfile(); // builds the custom dr_custom_distribution_data table when ADR is off; see setADR()'s doc comment for the return value
	// FIX: shared by send() and the deferred-send replay in handleEvents()'s SMTC_MODEM_EVENT_TXDONE
	// case (see deferredSend's doc comment) - the actual smtc_modem_request_uplink() call plus the
	// uplinkPending/lastTxDr/lastTxPhyPayloadLen bookkeeping that has to happen identically either way.
	// Doesn't repeat send()'s isJoined()/data-null/uplinkPending guards - callers are expected to have
	// already established it's safe to actually dispatch.
	bool dispatchUplink(uint8_t port, const uint8_t *data, uint8_t length, bool confirmed);
};

#endif // LORAWAN_ENGINE_H
