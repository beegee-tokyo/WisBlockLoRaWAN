#include "LoRaWANEngine.h"
#include <Arduino.h> // millis() - used for the custom join-reattempt-interval timer
#include <string.h>

#include "smtc_modem_api.h"	  // vendored: src/lbm/smtc_modem_api/smtc_modem_api.h
#include "smtc_modem_utilities.h" // vendored: smtc_modem_run_engine(), smtc_modem_init()
#include "lorawan_api.h"	  // vendored: src/lbm/smtc_modem_core/lorawan_api/lorawan_api.h - lorawan_api_next_dr_get()

namespace
{
constexpr uint8_t kStackId = 0; // single-stack device; LBM supports multi-stack, unused here

// smtc_modem_init() must be called exactly once per process lifetime (its
// doc comment: "Init the soft modem..."), even though LoRaWANEngine::begin()
// itself may run more than once (e.g. after a config change that needs the
// engine re-initialized) - this guard keeps a second begin() from calling it
// twice.
bool lbmInitialized = false;

// smtc_modem_init()'s callback contract (smtc_modem_utilities.h): "The
// callback will be called each time a modem event is raised internally" -
// it carries no event data itself, it's purely a notification hook meant to
// wake whatever task drains smtc_modem_get_event() (relevant under an RTOS;
// LBM's own examples use it to set an event flag for their main loop). This
// bare-metal Arduino port already drains events unconditionally every
// loop() via LoRaWANEngine::handleEvents(), so there's nothing useful to do
// here - the callback only needs to exist because smtc_modem_init() requires
// a non-null function pointer.
void onModemEventNotify(void)
{
}

// FIX (root cause of a persistent, 100%-reproducible bug: setADR(false)
// with a fixed DR failing every single time with LBM's own
// "ADR with a bad DataRate value" trace, confirmed by tracing the actual
// failure into smtc_modem_custom_dr_distribution_to_tab() in the vendored
// smtc_modem.c): dr_custom_distribution_data is NOT a one-hot table
// indexed by DR (weight at index N meaning "use DR N") - it's a flat list
// of SMTC_MODEM_CUSTOM_ADR_DATA_LENGTH (16) literal DR *values*, one per
// retry-attempt slot, and LBM validates/counts each slot's value directly
// against the current channel mask (each entry must itself be a
// currently-allowed DR, not an index into anything). The old
// implementation here did exactly the one-hot thing the array's name
// invites you to assume: out[dataRate] = 1, leaving the other 15 of 16
// slots at value 0 - meaning "15 of 16 attempts should use DR0, 1 attempt
// should use DR1" was being requested, regardless of what dataRate the
// caller actually wanted. On any region where dwell time or the channel
// mask excludes DR0/DR1 (AS923's dwell-time floor is DR2 - see
// MIN_TX_DR_LIMIT_AS_923 in region_as_923_defs.h), *every* slot fails
// LBM's validation and the call is rejected outright with
// SMTC_MODEM_RC_INVALID - independent of the requested DR, independent of
// the channel mask ever widening, which is why retrying after every
// uplink (see applyAdrProfile()'s caller in handleEvents()) never helped:
// there was nothing time-dependent to wait out.
//
// Fixed by filling every slot with the literal requested DR value, which
// is what correctly expresses "always use this DR" to LBM's own
// validation and runtime selection logic (smtc_real_get_next_tx_dr() in
// smtc_real.c counts occurrences per DR value across surviving slots to
// build its actual weighted-random selection table - see
// Creation-Log-From-Claude-AI.md's note on this for the full trace).
void buildSingleDrDistribution(uint8_t dataRate, uint8_t out[SMTC_MODEM_CUSTOM_ADR_DATA_LENGTH])
{
	for (size_t i = 0; i < SMTC_MODEM_CUSTOM_ADR_DATA_LENGTH; i++)
	{
		out[i] = dataRate;
	}
}
// Explicit WisBlockRegion -> smtc_modem_region_t mapping. NOT a direct cast
// - the two enums have completely different numeric values AND a
// different ordering (LBM's real smtc_modem_region_t interleaves the
// AS923 groups among other regions rather than grouping them together
// the way WisBlockRegion does), so casting between them silently selects
// the wrong region entirely. Verified against the real values in
// src/lbm/smtc_modem_api/smtc_modem_api.h.
smtc_modem_region_t toSmtcModemRegion(WisBlockRegion region)
{
	switch (region)
	{
	case WISBLOCK_REGION_EU868:
		return SMTC_MODEM_REGION_EU_868;
	case WISBLOCK_REGION_US915:
		return SMTC_MODEM_REGION_US_915;
	case WISBLOCK_REGION_AU915:
		return SMTC_MODEM_REGION_AU_915;
	case WISBLOCK_REGION_AS923_1:
		return SMTC_MODEM_REGION_AS_923_GRP1;
	case WISBLOCK_REGION_AS923_2:
		return SMTC_MODEM_REGION_AS_923_GRP2;
	case WISBLOCK_REGION_AS923_3:
		return SMTC_MODEM_REGION_AS_923_GRP3;
	case WISBLOCK_REGION_AS923_4:
		return SMTC_MODEM_REGION_AS_923_GRP4;
	case WISBLOCK_REGION_KR920:
		return SMTC_MODEM_REGION_KR_920;
	case WISBLOCK_REGION_IN865:
		return SMTC_MODEM_REGION_IN_865;
	case WISBLOCK_REGION_RU864:
		return SMTC_MODEM_REGION_RU_864;
	case WISBLOCK_REGION_CN470:
		return SMTC_MODEM_REGION_CN_470;
	case WISBLOCK_REGION_CN470_RP_1_0:
		return SMTC_MODEM_REGION_CN_470_RP_1_0;
	case WISBLOCK_REGION_WW2G4:
	default:
		// WW2G4 (2.4GHz, LR11xx/SX128x only) isn't vendored in this build
		// (see README "Patches made to vendored LBM source" -
		// region_ww_2g4.c was removed as not applicable to SX1262) and
		// isn't a valid smtc_modem_region_t target here. Falling through
		// to EU868 as a safe default rather than casting garbage - if you
		// select this value expect the join to fail, not a garbage
		// frequency like the bug this table replaces.
		return SMTC_MODEM_REGION_EU_868;
	}
}

// FIX: WisBlockTxResult::airtimeMs has been reporting a hardcoded 0 for
// every LoRaWAN uplink since onLoRaWANTxFinished()'s TXDONE handler was
// first written - SMTC_MODEM_EVENT_TXDONE's own event data carries only a
// status enum (smtc_modem_api.h), no airtime figure, so nothing was ever
// filling this field in. Computed here instead using the standard LoRa
// airtime formula (Semtech AN1200.13 - the same one LoRaP2PEngine's
// computeAirtimeMs() already uses and this project's own hardware traces
// have confirmed accurate: predicted 102.9ms vs measured 103ms toa for a
// real SF8/BW125/22-byte PHY frame from a device log captured earlier in
// this project).
//
// Maps a LoRaWAN data rate index to SF/BW for regions with a verified,
// stable Regional Parameters DR table. Returns false (no computation) for
// FSK data rates (not a LoRa airtime formula at all) and for US915/AU915's
// DR8-13 500kHz-channel range and DR5-7 RFU range - less commonly hit by
// a device's own uplinks, and this project has no hardware trace to
// verify those specific entries against the way the EU-like table above
// was verified, so airtimeMs is left at 0 there rather than reporting an
// unverified number as if it were confirmed.
bool drToSfBw(WisBlockRegion region, uint8_t dr, uint8_t &sf, uint32_t &bwHz)
{
	switch (region)
	{
	case WISBLOCK_REGION_EU868:
	case WISBLOCK_REGION_AS923_1:
	case WISBLOCK_REGION_AS923_2:
	case WISBLOCK_REGION_AS923_3:
	case WISBLOCK_REGION_AS923_4:
	case WISBLOCK_REGION_RU864:
		// DR0-DR5 identical across every one of these regions; DR6
		// (SF7/BW250) is also defined for this specific group. DR7 is FSK.
		switch (dr)
		{
		case 0:
			sf = 12;
			bwHz = 125000;
			return true;
		case 1:
			sf = 11;
			bwHz = 125000;
			return true;
		case 2:
			sf = 10;
			bwHz = 125000;
			return true;
		case 3:
			sf = 9;
			bwHz = 125000;
			return true;
		case 4:
			sf = 8;
			bwHz = 125000;
			return true;
		case 5:
			sf = 7;
			bwHz = 125000;
			return true;
		case 6:
			sf = 7;
			bwHz = 250000;
			return true;
		default:
			return false; // DR7 = FSK
		}
	case WISBLOCK_REGION_IN865:
	case WISBLOCK_REGION_KR920:
	case WISBLOCK_REGION_CN470:
	case WISBLOCK_REGION_CN470_RP_1_0:
		// Same DR0-DR5 SF/BW pairs as the group above, but these regions
		// don't define DR6 (SF7/BW250) the same way - IN865's DR7 is FSK,
		// KR920/CN470 stop at DR5. Treat anything above DR5 as
		// unimplemented here rather than guess.
		switch (dr)
		{
		case 0:
			sf = 12;
			bwHz = 125000;
			return true;
		case 1:
			sf = 11;
			bwHz = 125000;
			return true;
		case 2:
			sf = 10;
			bwHz = 125000;
			return true;
		case 3:
			sf = 9;
			bwHz = 125000;
			return true;
		case 4:
			sf = 8;
			bwHz = 125000;
			return true;
		case 5:
			sf = 7;
			bwHz = 125000;
			return true;
		default:
			return false;
		}
	case WISBLOCK_REGION_US915:
	case WISBLOCK_REGION_AU915:
		// 125kHz uplink sub-band only (DR0-DR4) - the far more commonly
		// used range for a device's own uplinks. DR5-7 (RFU) and DR8-13
		// (500kHz channels) intentionally not implemented - see this
		// function's own doc comment above.
		switch (dr)
		{
		case 0:
			sf = 10;
			bwHz = 125000;
			return true;
		case 1:
			sf = 9;
			bwHz = 125000;
			return true;
		case 2:
			sf = 8;
			bwHz = 125000;
			return true;
		case 3:
			sf = 7;
			bwHz = 125000;
			return true;
		case 4:
			sf = 8;
			bwHz = 500000;
			return true;
		default:
			return false;
		}
	default:
		return false;
	}
}

uint32_t computeLoRaWANAirtimeMs(WisBlockRegion region, uint8_t dr, uint8_t phyPayloadLen)
{
	uint8_t sf;
	uint32_t bwHz;
	if (!drToSfBw(region, dr, sf, bwHz))
	{
		return 0; // FSK, or a region/DR combination not implemented above
	}

	double tSymMs = (double)(1u << sf) / (double)bwHz * 1000.0;
	// Low data rate optimization - same 16ms symbol-duration threshold
	// LoRaP2PEngine::computeLdro() already uses for P2P.
	uint8_t de = (tSymMs > 16.0) ? 1 : 0;

	constexpr uint8_t kPreambleSymbols = 8; // fixed by the LoRaWAN Regional Parameters spec
	constexpr uint8_t kCodingRate = 1;		 // 4/5 - the only CR LoRaWAN uses
	constexpr uint8_t kCrcOn = 1;			 // uplinks always have CRC on
	constexpr uint8_t kExplicitHeader = 0;	 // LoRaWAN always uses explicit header (H=0 in the formula's own convention)

	double tPreambleMs = (kPreambleSymbols + 4.25) * tSymMs;

	int32_t numerator = 8 * (int32_t)phyPayloadLen - 4 * sf + 28 + 16 * kCrcOn - 20 * kExplicitHeader;
	int32_t denominator = 4 * (sf - 2 * de);
	int32_t payloadSymbNb = 8;
	if (numerator > 0)
	{
		payloadSymbNb += ((numerator + denominator - 1) / denominator) * (kCodingRate + 4); // ceil division
	}

	double tPayloadMs = payloadSymbNb * tSymMs;
	return (uint32_t)(tPreambleMs + tPayloadMs + 0.5);
}
} // namespace

void LoRaWANEngine::begin(const WisBlockLoRaWANSettings &initial)
{
	currentJoinState = WISBLOCK_JOIN_IDLE;
	uplinkPending = false; // fresh smtc_modem_init() below has nothing queued
	// FIX: see setDeviceClass()'s doc comment - a fresh smtc_modem_init()
	// always comes up in Class A regardless of what applySettings() (below)
	// is about to (unsuccessfully, pre-join) ask for, so this always needs
	// re-applying once the device actually joins.
	classProfileApplied = false;

	if (!lbmInitialized)
	{
		smtc_modem_init(&onModemEventNotify);
		lbmInitialized = true;
	}

	// Delegates the rest to applySettings() rather than duplicating it here
	// - see applySettings()'s own doc comment for why it needs to push
	// region/OTAA credentials too, not just device class/ADR.
	applySettings(initial);
}

void LoRaWANEngine::applySettings(const WisBlockLoRaWANSettings &newSettings)
{
	settings = newSettings;

	// FIX: this used to only touch device class/ADR - region and
	// OTAA credentials were pushed to LBM exactly once, from begin(), and
	// never again. That's harmless as long as begin() happens to run after
	// setRegion()/setOTAAKeys() already populated the settings it's
	// called with - which used to be guaranteed (WisBlockLoRaWAN::begin()
	// called lorawan.begin() eagerly and unconditionally, near the very
	// end of its own setup work). It stopped being guaranteed once LoRaWAN
	// engine startup became lazy (see ensureLoRaWANEngineStarted()'s doc
	// comment in WisBlockLoRaWAN.h): begin() can now legitimately run from
	// setWorkMode(WISBLOCK_MODE_LORAWAN), before the application has called
	// setOTAAKeys()/setRegion() at all - the exact order every example this
	// library ships actually uses. On a device with a previously-saved
	// config already on flash this goes unnoticed, since the reloaded
	// values already match what the sketch would set anyway - but on a
	// genuinely first boot, or right after factoryReset(), begin() would
	// push whatever blank/default region and all-zero keys the settings
	// struct starts with, and the *later* setRegion()/setOTAAKeys() calls
	// would silently never reach LBM at all, right up until the next
	// reboot loads a saved config that happens to already be correct.
	// Pushing them here too, every time settings are (re)applied, closes
	// that gap regardless of call order.
	smtc_modem_set_region(kStackId, toSmtcModemRegion(settings.region));
	// FIX: must be pushed before join(), not after like setDeviceClass()/setADR() -
	// this is exactly the point (see setChannelMask()'s doc comment): picking the
	// sub-band the join request itself will use, not reconfiguring after the fact.
	// lorawan_api_set_channel_mask() itself is a no-op on regions that don't need
	// this (EU868, AS923, ...), so it's safe to always call unconditionally here.
	lorawan_api_set_channel_mask(kStackId, settings.channelMask);
	// See WisBlockLoRaWANSettings::pingSlotPeriodicity's doc comment - harmless for Class A/C,
	// and this way it's already correct the moment an application does switch to Class B.
	smtc_modem_class_b_set_ping_slot_periodicity(
		kStackId, (smtc_modem_class_b_ping_slot_periodicity_t)settings.pingSlotPeriodicity);
	if (settings.joinMode == WISBLOCK_JOIN_OTAA)
	{
		smtc_modem_set_deveui(kStackId, settings.otaa.devEui);
		smtc_modem_set_joineui(kStackId, settings.otaa.joinEui);
		smtc_modem_set_nwkkey(kStackId, settings.otaa.appKey);
	}
	// ABP credentials aren't pushed here - smtc_modem_debug_connect_with_abp()
	// (called from join(), see below) takes devAddr/nwkSKey/appSKey
	// directly and connects immediately, rather than going through a
	// separate "set credentials then join" flow like OTAA does.

	setDeviceClass(settings.deviceClass);
	applyAdrProfile();
}

void LoRaWANEngine::join()
{
	currentJoinState = WISBLOCK_JOIN_IN_PROGRESS;
	// FIX: see setMaxJoinAttempts()'s doc comment - a fresh join cycle (this call), whether
	// application- or auto-triggered, always starts the attempt count over; only the internal
	// retry path in handleEvents() continues counting within the same cycle.
	joinAttemptCount = 0;
	joinRetryScheduled = false;

	if (settings.joinMode == WISBLOCK_JOIN_OTAA)
	{
		smtc_modem_join_network(kStackId);
		// Asynchronous: currentJoinState transitions to SUCCEEDED/FAILED
		// later in handleEvents() when SMTC_MODEM_EVENT_JOINED /
		// _JOINFAIL arrives.
	}
	else
	{
		// smtc_modem_debug_connect_with_abp() ("debug purpose" in
		// smtc_modem_api.h, but it's the only ABP path v4.9.0 exposes)
		// connects synchronously - there's no OTAA-style handshake to wait
		// for, so the join result is known immediately from its return
		// code rather than from a later SMTC_MODEM_EVENT_JOINED event.
		smtc_modem_return_code_t rc = smtc_modem_debug_connect_with_abp(
			kStackId, settings.abp.devAddr, settings.abp.nwkSKey, settings.abp.appSKey);

		if (rc == SMTC_MODEM_RC_OK)
		{
			currentJoinState = WISBLOCK_JOIN_SUCCEEDED;
			// FIX: see setDeviceClass()'s doc comment - smtc_modem_set_class()
			// requires JOINED status, which (unlike OTAA) is already true by
			// the time this synchronous call returns, so this is the right
			// place to retry it for the ABP path rather than waiting for an
			// SMTC_MODEM_EVENT_JOINED that ABP never actually raises.
			if (!classProfileApplied)
			{
				setDeviceClass(settings.deviceClass);
			}
			if (joinSuccessCb)
			{
				joinSuccessCb();
			}
		}
		else
		{
			currentJoinState = WISBLOCK_JOIN_FAILED;
			if (joinFailedCb)
			{
				joinFailedCb();
			}
		}
	}
}

void LoRaWANEngine::stopJoin()
{
	// RUI3's AT+JOIN=0:... ("stop joining"). smtc_modem_leave_network() itself: "Leave an
	// already joined network or cancels an ongoing join process" - covers both an in-progress
	// join and this library's own custom-interval retry timer (cleared below) in one call.
	smtc_modem_leave_network(kStackId);
	joinRetryScheduled = false;
	currentJoinState = WISBLOCK_JOIN_IDLE;
}

bool LoRaWANEngine::isJoined() const
{
	smtc_modem_status_mask_t status = 0;
	smtc_modem_get_status(kStackId, &status);
	return (status & SMTC_MODEM_STATUS_JOINED) != 0;
}

WisBlockJoinState LoRaWANEngine::joinState() const
{
	return currentJoinState;
}

bool LoRaWANEngine::send(uint8_t port, const uint8_t *data, uint8_t length, bool confirmed)
{
	if (!isJoined())
	{
		return false;
	}
	// FIX (reported directly: "Can't send manually on fPort 0 an empty
	// packet. It throws an error" - a workaround attempt for exactly the
	// FPending-drain issue above, needed when fetchPendingDownlinks is
	// disabled, or just wanted for manual control). A 0-length uplink is a
	// legitimate LoRaWAN operation - it's exactly what this library's own
	// auto-fetch sends - so length == 0 is no longer rejected here. A NULL
	// data pointer is still rejected when length > 0 (a real payload was
	// promised but not provided), but is allowed - and substituted with a
	// real, unused buffer internally, since smtc_modem_request_uplink()
	// itself rejects a literal NULL even at length 0 - when length == 0,
	// so callers can simply pass nullptr for an empty uplink rather than
	// needing to keep a dummy buffer of their own around.
	//
	// FPort 0 is a separate, unavoidable restriction, not something this
	// library imposes: LoRaWAN reserves it for MAC-only frames, and
	// smtc_modem_send_tx() itself refuses application uplinks on it - this
	// still fails, correctly, for FPort 0 regardless of length. Use any
	// other valid FPort (e.g. the same one the application normally sends
	// on) for a manual empty "fetch the next pending downlink" uplink.
	if (length > 0 && data == nullptr)
	{
		return false;
	}
	static const uint8_t kEmptyPayload[1] = {0};
	if (length == 0)
	{
		data = kEmptyPayload;
	}
	if (uplinkPending)
	{
		// FIX: see deferredSend's doc comment in LoRaWANEngine.h. Regardless
		// of *why* the slot is currently occupied - this library's own
		// FPending auto-fetch, a real application send already in flight,
		// or an earlier deferred send that's only just now being replayed -
		// defer this one instead of refusing it outright, as long as
		// nothing is already waiting in the single deferred slot. It will
		// actually be transmitted the moment whatever's currently in flight
		// finishes (see the SMTC_MODEM_EVENT_TXDONE case below), however
		// many further duty-cycle-limited hops that takes.
		//
		// CORRECTION: this used to only defer when the in-flight uplink was
		// specifically the auto-fetch (autoFetchUplinkPending), on the
		// theory that two genuine application sends racing each other
		// should keep failing as before. In practice, on a duty-cycle- or
		// LBT-constrained region, a single auto-fetch/deferral can itself
		// take long enough to actually transmit that the application's
		// *next* regularly-scheduled send lands before it's done too -
		// confirmed against a real device log where the first collision
		// deferred correctly, but the application's following send then hit
		// the old, narrower check (the thing in flight by then was the
		// *replayed* application send, not the auto-fetch) and was refused
		// again. Deferring unconditionally here - still only ever one level
		// deep - closes that gap instead of just moving it one uplink later.
		if (!deferredSend.valid)
		{
			deferredSend.port = port;
			deferredSend.length = length > sizeof(deferredSend.data) ? sizeof(deferredSend.data) : length;
			memcpy(deferredSend.data, data, deferredSend.length);
			deferredSend.confirmed = confirmed;
			deferredSend.valid = true;
			return true;
		}
		// A second send() arriving before the first deferred one has even
		// gone out yet - still refused, same as it always was for two
		// genuine application sends racing each other. This remains a
		// single deferred slot, not a general uplink queue.
		return false;
	}

	// RUI3-compatible AT+LINKCHECK mode - see setLinkCheckMode()'s doc
	// comment. Piggybacks a LinkCheckReq MAC command onto this uplink via
	// the same requestLinkCheck() mechanism AT+LINKCHECK used to trigger
	// directly; mode 1 consumes itself after one use, mode 2 persists.
	if (linkCheckMode != 0)
	{
		requestLinkCheck();
		if (linkCheckMode == 1)
		{
			linkCheckMode = 0;
		}
	}

	return dispatchUplink(port, data, length, confirmed);
}

bool LoRaWANEngine::dispatchUplink(uint8_t port, const uint8_t *data, uint8_t length, bool confirmed)
{
	smtc_modem_return_code_t rc = smtc_modem_request_uplink(kStackId, port, confirmed, data, length);
	if (rc == SMTC_MODEM_RC_OK)
	{
		uplinkPending = true;
		// FIX: see computeLoRaWANAirtimeMs()'s doc comment above - captured
		// here, right after this specific uplink was accepted, since the
		// active DR can legitimately differ between one uplink and the
		// next (ADR, LinkADRReq, retries). "next" in LBM's own naming
		// means "the DR about to be used for the upcoming transmission" -
		// exactly this one. phyPayloadLen is an estimate, not exact:
		// application length plus the standard 13-byte MHDR+FHDR+FPort+MIC
		// overhead, assuming no MAC commands happen to be piggybacked in
		// FOpts on this specific frame (that length isn't knowable from
		// here) - close enough for an informational airtime figure, not
		// meant to be byte-perfect.
		lastTxDr = lorawan_api_next_dr_get(kStackId);
		uint32_t phyLen = (uint32_t)length + 13;
		lastTxPhyPayloadLen = (phyLen > 255) ? 255 : (uint8_t)phyLen;
		return true;
	}
	return false;
}

// FIX (root cause of the Class B/C bug where the device visibly requests a
// class switch but keeps behaving like Class A forever - RX1/RX2 opening
// with a timeout and closing again after every uplink, with no continuous
// RXC/ping-slot window ever appearing in between, confirmed against a real
// device trace): smtc_modem_set_class() (vendored in smtc_modem.c) checks
// SMTC_MODEM_STATUS_JOINED first and returns SMTC_MODEM_RC_FAIL outright,
// with the class left unchanged, if the device isn't joined yet - the exact
// same "must be joined" gate smtc_modem_adr_set_profile() has (see
// setADR()'s doc comment). setDeviceClass() is called from applySettings(),
// which runs from begin() - i.e. before join() is ever called - so this
// call was *guaranteed* to fail every single time on a fresh boot,
// regardless of which class was actually requested. Unlike the ADR path,
// this return code used to be discarded entirely, so the failure was
// completely silent: no trace, no retry, nothing - the device just quietly
// stayed on Class A no matter what the application asked for.
// Fixed the same way as setADR(): the return code is now checked and
// remembered (classProfileApplied), and the request is retried once the
// device is actually joined - from the SMTC_MODEM_EVENT_JOINED handler and
// the synchronous ABP-success path in join() - with a TXDONE-time fallback
// retry mirroring applyAdrProfile()'s, in case that first retry somehow
// still didn't land.
bool LoRaWANEngine::setDeviceClass(WisBlockDeviceClass deviceClass)
{
	settings.deviceClass = deviceClass;
	smtc_modem_return_code_t rc = smtc_modem_set_class(kStackId, (smtc_modem_class_t)deviceClass);
	classProfileApplied = (rc == SMTC_MODEM_RC_OK);
	// Class B additionally requires a ping slot periodicity request; see
	// smtc_modem_class_b_set_ping_slot_periodicity() in smtc_modem_api.h
	// and the SMTC_MODEM_LORAWAN_MAC_REQ_PING_SLOT_INFO mac request - not
	// wired up here yet since it needs an app-chosen periodicity value.
	return classProfileApplied;
}

bool LoRaWANEngine::setChannelMask(uint16_t mask)
{
	settings.channelMask = mask;
	// Effective immediately (no joined-state gate, unlike setDeviceClass()/setADR() -
	// see this method's doc comment in LoRaWANEngine.h). lorawan_api_set_channel_mask()
	// itself has no failure return - it's a no-op on regions that don't support
	// sub-band selection, matching AT+MASK's documented region scoping - so this
	// always reports success; getChannelMask() reads back what's actually taken effect.
	lorawan_api_set_channel_mask(kStackId, mask);
	return true;
}

uint16_t LoRaWANEngine::getChannelMask() const
{
	return lorawan_api_get_channel_mask(kStackId);
}

bool LoRaWANEngine::setLbtEnabled(bool enabled)
{
	return smtc_modem_lbt_set_state(kStackId, enabled) == SMTC_MODEM_RC_OK;
}

bool LoRaWANEngine::getLbtEnabled() const
{
	bool enabled = false;
	smtc_modem_lbt_get_state(kStackId, &enabled);
	return enabled;
}

bool LoRaWANEngine::setLbtThreshold(int16_t thresholdDbm)
{
	// See this method's doc comment in LoRaWANEngine.h - threshold and scan time share one
	// underlying smtc_modem_lbt_set_parameters() call (which also carries an RSSI measurement
	// bandwidth this library doesn't expose), so the current duration/bandwidth are read back
	// first rather than risking clobbering either with a stale cached value.
	uint32_t durationMs = 0;
	uint32_t bwHz = 0;
	int16_t currentThresholdDbm = 0;
	smtc_modem_lbt_get_parameters(kStackId, &durationMs, &currentThresholdDbm, &bwHz);
	return smtc_modem_lbt_set_parameters(kStackId, durationMs, thresholdDbm, bwHz) == SMTC_MODEM_RC_OK;
}

int16_t LoRaWANEngine::getLbtThreshold() const
{
	uint32_t durationMs = 0;
	uint32_t bwHz = 0;
	int16_t thresholdDbm = 0;
	smtc_modem_lbt_get_parameters(kStackId, &durationMs, &thresholdDbm, &bwHz);
	return thresholdDbm;
}

bool LoRaWANEngine::setLbtScanTime(uint32_t scanTimeMs)
{
	uint32_t currentDurationMs = 0;
	uint32_t bwHz = 0;
	int16_t thresholdDbm = 0;
	smtc_modem_lbt_get_parameters(kStackId, &currentDurationMs, &thresholdDbm, &bwHz);
	return smtc_modem_lbt_set_parameters(kStackId, scanTimeMs, thresholdDbm, bwHz) == SMTC_MODEM_RC_OK;
}

uint32_t LoRaWANEngine::getLbtScanTime() const
{
	uint32_t durationMs = 0;
	uint32_t bwHz = 0;
	int16_t thresholdDbm = 0;
	smtc_modem_lbt_get_parameters(kStackId, &durationMs, &thresholdDbm, &bwHz);
	return durationMs;
}

void LoRaWANEngine::setJoinReattemptInterval(uint8_t seconds)
{
	// RUI3's own AT+JOIN documents this same 7-255s range; clamped rather than rejected,
	// matching setTxPower()/similar setters elsewhere in this file that take "closest valid
	// value" over an outright failure for a simple numeric range.
	if (seconds < 7)
	{
		seconds = 7;
	}
	settings.joinReattemptIntervalS = seconds;
}

bool LoRaWANEngine::setADR(bool enabled)
{
	settings.adrEnabled = enabled;
	return applyAdrProfile();
}

void LoRaWANEngine::setTxPower(uint8_t txPowerIndex)
{
	settings.txPower = txPowerIndex;
	// v4.9.0's public smtc_modem_api has no direct "set TX power" call -
	// TX power is network-controlled via LinkADRReq when ADR is on, and
	// otherwise follows the region's default/max EIRP table. This value is
	// stored (and round-trips through AT+STATUS / the API getter) but
	// currently has no radio-level effect. If your network server supports
	// a custom downlink MAC command for fixed power, or a future LBM
	// release exposes one, wire it here.
}

bool LoRaWANEngine::setDataRate(uint8_t dataRate)
{
	settings.dataRate = dataRate;
	return applyAdrProfile();
}

bool LoRaWANEngine::applyAdrProfile()
{
	if (settings.adrEnabled)
	{
		uint8_t unused[SMTC_MODEM_CUSTOM_ADR_DATA_LENGTH] = {0};
		smtc_modem_adr_set_profile(kStackId, SMTC_MODEM_ADR_PROFILE_NETWORK_CONTROLLED, unused);
		adrProfileApplied = true;
		return true;
	}
	else
	{
		uint8_t distribution[SMTC_MODEM_CUSTOM_ADR_DATA_LENGTH];
		buildSingleDrDistribution(settings.dataRate, distribution);
		// FIX: see setADR()'s doc comment for the full mechanism - this
		// call fails outright (SMTC_MODEM_RC_INVALID, LBM's own trace
		// prints "ADR with a bad DataRate value") if settings.dataRate
		// isn't in the union of DR ranges every *currently enabled*
		// uplink channel supports, which right after a fresh join is only
		// the region's default join channels - often narrower than what
		// the network's later NewChannelReq-added channels support. LBM
		// leaves the ADR profile exactly as it was before this call on
		// failure (still NETWORK_CONTROLLED, its own default) - never
		// silently discard this return code, or the app has no way to
		// know CUSTOM/ADR-off never actually took effect.
		smtc_modem_return_code_t rc =
			smtc_modem_adr_set_profile(kStackId, SMTC_MODEM_ADR_PROFILE_CUSTOM, distribution);
		adrProfileApplied = (rc == SMTC_MODEM_RC_OK);
		return adrProfileApplied;
	}
}

void LoRaWANEngine::requestLinkCheck()
{
	smtc_modem_trig_lorawan_mac_request(kStackId, SMTC_MODEM_LORAWAN_MAC_REQ_LINK_CHECK);
}

bool LoRaWANEngine::getLinkCheckResult(WisBlockLinkCheckResult &out) const
{
	uint8_t margin = 0;
	uint8_t gwCount = 0;
	// SMTC_MODEM_RC_FAIL specifically means "no data available" (verified
	// against smtc_modem_api.h's doc comment on this function) - i.e. no
	// link check has ever been answered yet, not a transient/retry-able
	// error, so a plain bool is enough here rather than surfacing the
	// return code.
	if (smtc_modem_get_lorawan_link_check_data(kStackId, &margin, &gwCount) != SMTC_MODEM_RC_OK)
	{
		return false;
	}
	out.demodMargin = margin;
	out.gatewayCount = gwCount;
	return true;
}

void LoRaWANEngine::requestDeviceTime()
{
	smtc_modem_trig_lorawan_mac_request(kStackId, SMTC_MODEM_LORAWAN_MAC_REQ_DEVICE_TIME);
}

bool LoRaWANEngine::setPingSlotPeriodicity(uint8_t periodicity)
{
	if (periodicity > 7)
	{
		periodicity = 7;
	}
	settings.pingSlotPeriodicity = periodicity;
	bool ok = smtc_modem_class_b_set_ping_slot_periodicity(
				  kStackId, (smtc_modem_class_b_ping_slot_periodicity_t)periodicity) == SMTC_MODEM_RC_OK;
	// See this method's doc comment in LoRaWANEngine.h - the network needs to be told about
	// this too, not just this device's own local scheduling.
	smtc_modem_trig_lorawan_mac_request(kStackId, SMTC_MODEM_LORAWAN_MAC_REQ_PING_SLOT_INFO);
	return ok;
}

uint8_t LoRaWANEngine::getPingSlotPeriodicity() const
{
	smtc_modem_class_b_ping_slot_periodicity_t periodicity = SMTC_MODEM_CLASS_B_PINGSLOT_1_S;
	smtc_modem_class_b_get_ping_slot_periodicity(kStackId, &periodicity);
	return (uint8_t)periodicity;
}

bool LoRaWANEngine::getBeaconFrequencyAndDr(uint32_t &frequencyHz, uint8_t &dr) const
{
	uint32_t epochTimeS = lorawan_api_get_beacon_epoch_time(kStackId);
	dr = lorawan_api_get_beacon_dr(kStackId);
	frequencyHz = lorawan_api_get_beacon_frequency(kStackId, epochTimeS);
	return epochTimeS != 0;
}

uint32_t LoRaWANEngine::getBeaconTime() const
{
	return lorawan_api_get_beacon_epoch_time(kStackId);
}

uint32_t LoRaWANEngine::handleEvents()
{
	// smtc_modem_run_engine()'s own doc comment (smtc_modem_utilities.h):
	// "This function must be called in main loop. It returns an amount of
	// ms after which the function must at least be called again." This
	// return value is not optional/advisory - it's how the caller (in
	// background task mode, WisBlockLbmTask's event task) knows how soon
	// to re-arm its wait even if no external event (DIO1 IRQ, our own
	// scheduled timer) fires first. Discarding it (as this used to do)
	// works fine under loop()-polled handleEvents(), which calls this
	// unconditionally and frequently regardless - but starves the engine
	// in background task mode, where nothing else guarantees a timely
	// re-call.
	uint32_t sleep_time_ms = smtc_modem_run_engine();

	smtc_modem_event_t event;
	uint8_t pending = 0;
	do
	{
		if (smtc_modem_get_event(&event, &pending) != SMTC_MODEM_RC_OK)
		{
			break;
		}

		switch (event.event_type)
		{
		case SMTC_MODEM_EVENT_JOINED:
			currentJoinState = WISBLOCK_JOIN_SUCCEEDED;
			// FIX: see setDeviceClass()'s doc comment - this is the actual
			// moment the device becomes joined for the OTAA path, and thus
			// the first point at which smtc_modem_set_class() can possibly
			// succeed. The applySettings()-time call from begin() was
			// guaranteed to fail (device wasn't joined yet); this retry is
			// what actually gets Class B/C to take effect.
			if (!classProfileApplied)
			{
				setDeviceClass(settings.deviceClass);
			}
			if (joinSuccessCb)
			{
				joinSuccessCb();
			}
			break;

		case SMTC_MODEM_EVENT_JOINFAIL:
			// FIX: see setJoinReattemptInterval()'s/setMaxJoinAttempts()'s doc comments.
			// When both are at their defaults (8s interval, 0 = unlimited attempts), this
			// is a no-op change from before: LBM's own join task has already scheduled its
			// own next attempt by the time this event fires, and joinAttemptCount/
			// maxJoinAttempts never trips at 0. Only takes over scheduling when the
			// application has actually asked for non-default behavior here.
			joinAttemptCount++;
			if (settings.maxJoinAttempts != 0 && joinAttemptCount >= settings.maxJoinAttempts)
			{
				currentJoinState = WISBLOCK_JOIN_GAVE_UP;
				smtc_modem_leave_network(kStackId); // cancels LBM's own auto-scheduled next attempt
				joinRetryScheduled = false;
			}
			else
			{
				currentJoinState = WISBLOCK_JOIN_FAILED;
				if (settings.joinReattemptIntervalS != 8)
				{
					// Non-default interval requested - take over scheduling entirely rather
					// than letting LBM's own auto-retry race with ours.
					smtc_modem_leave_network(kStackId);
					nextJoinRetryAtMs = millis() + ( uint32_t ) settings.joinReattemptIntervalS * 1000;
					joinRetryScheduled = true;
				}
				// else: leave LBM's own auto-scheduled retry alone (default interval).
			}
			if (joinFailedCb)
			{
				// Fires on every individual failed attempt, same as always - check
				// joinState() from inside this callback for WISBLOCK_JOIN_GAVE_UP to tell
				// whether this specific failure was the final one.
				joinFailedCb();
			}
			break;

		case SMTC_MODEM_EVENT_TXDONE:
		{
			// LBM no longer holds a pending uplink for us either way -
			// see send()'s doc comment / uplinkPending's declaration.
			uplinkPending = false;
			WisBlockTxResult r;
			r.success = (event.event_data.txdone.status != SMTC_MODEM_EVENT_TXDONE_NOT_SENT);
			// FIX: see computeLoRaWANAirtimeMs()'s doc comment - this used
			// to be left at its default-initialized 0 unconditionally.
			// Only meaningful on an actual send (NOT_SENT means nothing
			// went out, so there's no real airtime to report for it).
			// Computed here, before any deferred-send replay below, since
			// dispatchUplink() overwrites lastTxDr/lastTxPhyPayloadLen with
			// the replayed uplink's own values - this figure needs to
			// describe the uplink this TXDONE actually just completed, not
			// whatever gets dispatched next.
			if (r.success)
			{
				r.airtimeMs = computeLoRaWANAirtimeMs(settings.region, lastTxDr, lastTxPhyPayloadLen);
			}
			// FIX: see deferredSend's doc comment in LoRaWANEngine.h and
			// send()'s updated doc comment for why this is now
			// unconditional (any TXDONE frees the single deferred slot,
			// not just one specifically for the auto-fetch) - a real
			// application send that had to be deferred is replayed the
			// moment whatever was ahead of it finishes, however many
			// duty-cycle-limited hops that took to get here.
			bool wasAutoFetch = autoFetchUplinkPending;
			autoFetchUplinkPending = false;
			if (deferredSend.valid)
			{
				deferredSend.valid = false;
				dispatchUplink(deferredSend.port, deferredSend.data, deferredSend.length, deferredSend.confirmed);
			}
			else if (pendingDownlinkFetchRequested)
			{
				// FIX: see pendingDownlinkFetchRequested's doc comment in LoRaWANEngine.h -
				// services a fetch that couldn't be dispatched immediately from
				// SMTC_MODEM_EVENT_DOWNDATA because the slot wasn't free yet at that moment
				// (an ordering race, not a real collision - this TXDONE is the proof the
				// slot is free now). Only reached when nothing real was deferred above,
				// so application data always wins the slot over this library's own
				// keep-alive traffic.
				pendingDownlinkFetchRequested = false;
				static const uint8_t kEmptyUplinkPayload[1] = {0};
				if (dispatchUplink(pendingDownlinkFetchPort, kEmptyUplinkPayload, 0, false))
				{
					autoFetchUplinkPending = true;
				}
			}
			// FIX: see setADR()'s doc comment for the full mechanism this
			// closes the loop on. A TXDONE means at least one uplink has
			// gone out since the last attempt, which is exactly when the
			// network's channel-widening MAC commands (NewChannelReq etc.)
			// are most likely to have just been processed on a prior RX
			// window - the best available moment to cheaply retry, without
			// needing to parse which specific MAC command was received.
			// Retrying with adrEnabled still true is a harmless no-op
			// (see applyAdrProfile()); this doesn't fire at all once a
			// retry succeeds.
			if (!adrProfileApplied)
			{
				applyAdrProfile();
			}
			// FIX: see setDeviceClass()'s doc comment - belt-and-suspenders
			// fallback alongside the JOINED/ABP-success retries above, in
			// case classProfileApplied is still false for some other
			// reason (e.g. RETURN_BUSY_IF_TEST_MODE) by the time an uplink
			// actually completes. Harmless no-op once it's already true.
			if (!classProfileApplied)
			{
				setDeviceClass(settings.deviceClass);
			}
			if (txFinishedCb && !wasAutoFetch)
			{
				// FIX: an application never asked for this library's own
				// empty FPending auto-fetch uplinks, so it shouldn't see a
				// TX-finished callback for one either - only for uplinks it
				// actually requested itself (including one that had to be
				// deferred and is only completing now).
				txFinishedCb(r);
			}
			break;
		}

		case SMTC_MODEM_EVENT_DOWNDATA:
		{
			WisBlockRxResult r;
			uint8_t remaining = 0;
			smtc_modem_dl_metadata_t meta;
			uint8_t length = 0;
			uint8_t buffer[SMTC_MODEM_MAX_LORAWAN_PAYLOAD_LENGTH];

			if (smtc_modem_get_downlink_data(buffer, &length, &meta, &remaining) == SMTC_MODEM_RC_OK)
			{
				r.port = meta.fport;
				r.length = length > sizeof(r.data) ? sizeof(r.data) : length;
				memcpy(r.data, buffer, r.length);
				r.rssi = (int16_t)meta.rssi - 64; // rssi field is dBm + 64 per smtc_modem_api.h
				r.snr = meta.snr;				   // 0.25 dB steps, per smtc_modem_api.h comment
				r.fpending = meta.fpending_bit != 0;

				// FIX (Class A pending-downlink bug - confirmed against a real device log plus
				// the matching LNS downlink log showing f_pending=true): a Class A device can only
				// receive downlinks in RX1/RX2 after an uplink. If the network has more downlinks
				// queued, it sets FPending on this one to say so - per LoRaWAN 1.0.4 section 5.1
				// the device is expected to respond with another uplink soon, not wait for its
				// next regular scheduled transmission, so the queue drains promptly rather than
				// one downlink per whatever the application's own send interval happens to be.
				// See WisBlockLoRaWANSettings::fetchPendingDownlinks's doc comment for the opt-out
				// and why this is Class-A-only.
				//
				// !uplinkPending: tries to fetch immediately if the slot happens to be free
				// already; if not - see pendingDownlinkFetchRequested's doc comment in
				// LoRaWANEngine.h for why this can legitimately happen even for the very
				// first attempt, not just a busy retry - the request is remembered instead
				// of dropped, and serviced from SMTC_MODEM_EVENT_TXDONE the moment the slot
				// actually does free up. Real application data (deferredSend) still takes
				// priority there over this remembered fetch if both are waiting.
				if (r.fpending && settings.fetchPendingDownlinks && settings.deviceClass == WISBLOCK_CLASS_A)
				{
					// smtc_modem_request_uplink() rejects a NULL payload pointer even for a
					// 0-length send (RETURN_INVALID_IF_NULL), so a real (unused) buffer is
					// needed here despite length being 0. FPort 0 is reserved for MAC-only
					// frames and smtc_modem_send_tx() forbids it for application uplinks - if
					// the pending-flagged downlink itself had no application payload (fport 0),
					// fall back to FPort 1 rather than failing the request outright.
					uint8_t fetchPort = (meta.fport == 0) ? 1 : meta.fport;
					if (!uplinkPending)
					{
						static const uint8_t kEmptyUplinkPayload[1] = {0};
						if (dispatchUplink(fetchPort, kEmptyUplinkPayload, 0, false))
						{
							autoFetchUplinkPending = true; // see this flag's doc comment in LoRaWANEngine.h
						}
					}
					else
					{
						pendingDownlinkFetchRequested = true;
						pendingDownlinkFetchPort = fetchPort;
					}
				}
			}
			if (rxFinishedCb)
			{
				rxFinishedCb(r);
			}
			break;
		}

		case SMTC_MODEM_EVENT_LINK_CHECK:
		{
			WisBlockLinkCheckResult r;
			bool answered = (event.event_data.link_check.status == SMTC_MODEM_EVENT_MAC_REQUEST_ANSWERED);
			if (answered)
			{
				// event itself only carries answered/not-answered status;
				// the actual margin/gateway count come from
				// getLinkCheckResult() (same pattern as downlink payload
				// needing smtc_modem_get_downlink_data()).
				getLinkCheckResult(r);
			}
			if (linkCheckCb)
			{
				linkCheckCb(answered, r);
			}
			break;
		}

		case SMTC_MODEM_EVENT_LORAWAN_MAC_TIME:
		{
			WisBlockTimeAnswer r;
			bool answered = (event.event_data.lorawan_mac_time.status == SMTC_MODEM_EVENT_MAC_REQUEST_ANSWERED);
			if (answered)
			{
				uint32_t gpsTimeS = 0;
				uint32_t fractionalS = 0;
				if (smtc_modem_get_lorawan_mac_time(kStackId, &gpsTimeS, &fractionalS) == SMTC_MODEM_RC_OK)
				{
					r.gpsEpochSeconds = gpsTimeS;
					r.fractionalSeconds = fractionalS;
				}
			}
			if (timeRequestCb)
			{
				timeRequestCb(answered, r);
			}
			break;
		}

		default:
			break;
		}
	} while (pending > 0);

	// FIX (the real root cause behind the whole FPending-drain saga -
	// confirmed against multiple real device logs across both AS923 and
	// AU915, on both ChirpStack and TTN, all showing a consistent ~50-60s
	// delay between a fetch/deferred uplink being queued and it actually
	// transmitting, regardless of how reliably the queueing itself had
	// already been fixed): sleep_time_ms above was computed by
	// smtc_modem_run_engine() at the very top of this function, before any
	// of the event handling below - including every dispatchUplink() call
	// this function itself can make (the FPending auto-fetch, a deferred
	// send being replayed, a deferred fetch being serviced) - had a chance
	// to queue anything new with LBM. That return value is stale the moment
	// this function queues a fresh uplink during its own event processing,
	// but was still being returned unconditionally.
	//
	// In loop()-polled mode this never mattered (the doc comment above
	// already covered why: the next handleEvents() call, and therefore the
	// next smtc_modem_run_engine() call, was always imminent regardless of
	// what this function returned). In background task mode
	// (WisBlockLbmTask), this return value directly becomes how long the
	// FreeRTOS task sleeps before calling smtc_modem_run_engine() again
	// (see eventTask() in wisblock_lbm_task.cpp) - so a freshly-queued
	// uplink would sit completely idle, un-acted-on by LBM, for however
	// long the *stale* pre-event-processing value happened to be. Every
	// log across this investigation showed "Background task active" at
	// startup and a suspiciously consistent delay in that ~50-60s range -
	// this is why: LBM's own idle/maintenance scheduling interval, computed
	// before anything new was queued, was what the background task kept
	// sleeping for, every single time, regardless of how many collision/
	// ordering fixes were made to the queueing logic itself.
	//
	// smtc_modem_run_engine()'s own doc comment says it must be called
	// again within its last reported budget - it does not say it can only
	// be called once per external trigger, and re-deriving that budget
	// after this function's own event handling has possibly changed the
	// state is exactly the documented "call again" contract, just invoked
	// proactively rather than waiting for the next external wake-up.
	sleep_time_ms = smtc_modem_run_engine();

	// FIX: see setJoinReattemptInterval()'s/setMaxJoinAttempts()'s doc comments - services the
	// custom join-retry timer set up in the SMTC_MODEM_EVENT_JOINFAIL case above, in place of
	// LBM's own (deliberately cancelled, in that case) auto-retry. Placed after the
	// smtc_modem_run_engine() refresh above so a retry fired here is accounted for by a
	// subsequent call, same reasoning as that fix.
	if (joinRetryScheduled)
	{
		int32_t remainingMs = ( int32_t ) ( nextJoinRetryAtMs - millis() );
		if (remainingMs <= 0)
		{
			joinRetryScheduled = false;
			currentJoinState = WISBLOCK_JOIN_IN_PROGRESS;
			smtc_modem_join_network(kStackId);
			sleep_time_ms = smtc_modem_run_engine();
		}
		else if (( uint32_t ) remainingMs < sleep_time_ms)
		{
			// Background-task-mode correctness (same class of bug as the fix above): don't
			// let this function report a sleep duration longer than its own pending retry,
			// or the FreeRTOS task would sleep past the deadline this function itself set.
			sleep_time_ms = ( uint32_t ) remainingMs;
		}
	}

	return sleep_time_ms;
}
