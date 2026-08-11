#include "LoRaWANEngine.h"
#include "LoRaWANRelay.h"
#include <string.h>

#include "smtc_modem_api.h"	  // vendored: src/lbm/smtc_modem_api/smtc_modem_api.h
#include "smtc_modem_utilities.h" // vendored: smtc_modem_run_engine(), smtc_modem_init()

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

// Builds a dr_custom_distribution_data table (SMTC_MODEM_CUSTOM_ADR_DATA_LENGTH
// = 16 bytes, one weight per possible DR index) with all weight on a single
// DR - this is the only way v4.9.0 lets you pin a specific data rate when
// ADR is off; see smtc_modem_adr_set_profile()'s doc comment in
// smtc_modem_api.h for the "custom data" semantics.
void buildSingleDrDistribution(uint8_t dataRate, uint8_t out[SMTC_MODEM_CUSTOM_ADR_DATA_LENGTH])
{
	memset(out, 0, SMTC_MODEM_CUSTOM_ADR_DATA_LENGTH);
	if (dataRate < SMTC_MODEM_CUSTOM_ADR_DATA_LENGTH)
	{
		out[dataRate] = 1;
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
} // namespace

void LoRaWANEngine::begin(const WisBlockLoRaWANSettings &initial)
{
	currentJoinState = WISBLOCK_JOIN_IDLE;
	uplinkPending = false; // fresh smtc_modem_init() below has nothing queued

	if (!lbmInitialized)
	{
		smtc_modem_init(&onModemEventNotify);
		lbmInitialized = true;
	}

	// Delegates the rest to applySettings() rather than duplicating it here
	// - see applySettings()'s own doc comment for why it needs to push
	// region/OTAA credentials too, not just device class/ADR/relay.
	applySettings(initial);
}

void LoRaWANEngine::applySettings(const WisBlockLoRaWANSettings &newSettings)
{
	settings = newSettings;

	// FIX: this used to only touch device class/ADR/relay - region and
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
	LoRaWANRelay::configureED(settings.relayEDConfig);
	LoRaWANRelay::configureServing(settings.relayServingConfig);
	setRelayMode(settings.relayMode);
}

void LoRaWANEngine::join()
{
	currentJoinState = WISBLOCK_JOIN_IN_PROGRESS;

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
	if (!isJoined() || data == nullptr || length == 0)
	{
		return false;
	}
	if (uplinkPending)
	{
		// FIX: previously fell through to smtc_modem_request_uplink()
		// unconditionally here, which LBM happily accepts even with a
		// send already queued - it just silently discards whichever one
		// was still pending. Refusing immediately, before ever calling
		// into LBM, means the caller's false return value now actually
		// means something actionable ("try again next cycle") instead of
		// this always returning true right up until the eventual
		// TXDONE_NOT_SENT arrives several seconds later - and no frame
		// counter gets burned on an uplink that was never going anywhere.
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

	smtc_modem_return_code_t rc = smtc_modem_request_uplink(kStackId, port, confirmed, data, length);
	if (rc == SMTC_MODEM_RC_OK)
	{
		uplinkPending = true;
		return true;
	}
	return false;
}

void LoRaWANEngine::setDeviceClass(WisBlockDeviceClass deviceClass)
{
	settings.deviceClass = deviceClass;
	smtc_modem_set_class(kStackId, (smtc_modem_class_t)deviceClass);
	// Class B additionally requires a ping slot periodicity request; see
	// smtc_modem_class_b_set_ping_slot_periodicity() in smtc_modem_api.h
	// and the SMTC_MODEM_LORAWAN_MAC_REQ_PING_SLOT_INFO mac request - not
	// wired up here yet since it needs an app-chosen periodicity value.
}

void LoRaWANEngine::setADR(bool enabled)
{
	settings.adrEnabled = enabled;
	applyAdrProfile();
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

void LoRaWANEngine::setDataRate(uint8_t dataRate)
{
	settings.dataRate = dataRate;
	applyAdrProfile();
}

void LoRaWANEngine::applyAdrProfile()
{
	if (settings.adrEnabled)
	{
		uint8_t unused[SMTC_MODEM_CUSTOM_ADR_DATA_LENGTH] = {0};
		smtc_modem_adr_set_profile(kStackId, SMTC_MODEM_ADR_PROFILE_NETWORK_CONTROLLED, unused);
	}
	else
	{
		uint8_t distribution[SMTC_MODEM_CUSTOM_ADR_DATA_LENGTH];
		buildSingleDrDistribution(settings.dataRate, distribution);
		smtc_modem_adr_set_profile(kStackId, SMTC_MODEM_ADR_PROFILE_CUSTOM, distribution);
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

void LoRaWANEngine::setRelayMode(WisBlockRelayMode mode)
{
	settings.relayMode = mode;
	LoRaWANRelay::configure(mode);
}

void LoRaWANEngine::configureRelayED(const WisBlockRelayEDConfig &cfg)
{
	LoRaWANRelay::configureED(cfg);
}

void LoRaWANEngine::configureRelayServing(const WisBlockRelayServingConfig &cfg)
{
	LoRaWANRelay::configureServing(cfg);
}

bool LoRaWANEngine::addRelayTrustedDevice(const WisBlockRelayTrustedDevice &device)
{
	return LoRaWANRelay::addTrustedDevice(device);
}

bool LoRaWANEngine::removeRelayTrustedDevice(uint8_t index)
{
	return LoRaWANRelay::removeTrustedDevice(index);
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
			if (joinSuccessCb)
			{
				joinSuccessCb();
			}
			break;

		case SMTC_MODEM_EVENT_JOINFAIL:
			currentJoinState = WISBLOCK_JOIN_FAILED;
			if (joinFailedCb)
			{
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
			if (txFinishedCb)
			{
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

	return sleep_time_ms;
}
