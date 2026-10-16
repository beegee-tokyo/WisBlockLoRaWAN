/**
 * @file WisBlockLoRaWANTypes.h
 * @brief Shared enums/structs for the WisBlockLoRaWAN library.
 *
 * These types are used by the public API (WisBlockLoRaWAN.h), the AT command
 * layer (WisBlockLoRaAT.h) and the persisted config store
 * (WisBlockLoRaWANConfig.h) so there is exactly one definition of "what a
 * setting is" in the whole library.
 */
#ifndef WISBLOCK_LORAWAN_TYPES_H
#define WISBLOCK_LORAWAN_TYPES_H

#include <stdint.h>

/** Top level operating mode. */
enum WisBlockWorkMode : uint8_t
{
	WISBLOCK_MODE_LORAWAN = 0,
	WISBLOCK_MODE_LORA_P2P = 1,
	WISBLOCK_MODE_FSK_P2P = 2, // GFSK point to point, RUI3 AT+NWM=2
};

/** Modulation used by the P2P engine, follows the work mode (see WisBlockLoRaWAN::setWorkMode()). */
enum WisBlockP2PModulation : uint8_t
{
	WISBLOCK_P2P_MOD_LORA = 0,
	WISBLOCK_P2P_MOD_FSK = 1,
};

/**
 * Special values of the P2P receive "timeout", the same as RUI3 AT+PRECV / api.lora.precv().
 * The parameter is a uint32_t, but the special values are 16 bit values (the AT command accepts 0 to 65535).
 * - WISBLOCK_P2P_RX_STOP: stop receiving, the radio goes to standby
 * - 1 to WISBLOCK_P2P_RX_TIMEOUT_MAX: receive for this time in ms, then the receive stops (timeout callback)
 * - WISBLOCK_P2P_RX_CONTINUOUS_TX: receive continuously and still allow sending. After a TX or a CAD the
 *   receive is started again automatically
 * - WISBLOCK_P2P_RX_CONTINUOUS: receive continuously, sending is refused (busy) until the receive is
 *   stopped with WISBLOCK_P2P_RX_STOP. Another receive command is refused as well
 * - WISBLOCK_P2P_RX_SINGLE: receive without timeout until one packet is received, then the receive stops
 * Values above 65535 are a timeout in ms as well (not in RUI3), limited to the maximum of the radio (262 s).
 */
#define WISBLOCK_P2P_RX_STOP 0UL
#define WISBLOCK_P2P_RX_TIMEOUT_MAX 65532UL
#define WISBLOCK_P2P_RX_CONTINUOUS_TX 65533UL
#define WISBLOCK_P2P_RX_CONTINUOUS 65534UL
#define WISBLOCK_P2P_RX_SINGLE 65535UL

/**
 * Battery level of the LoRaWAN DevStatusAns (LoRaWAN 1.0.4 specification), the value returned by the
 * device status callback registered with WisBlockLoRaWAN::onDeviceStatusRequest().
 * - WISBLOCK_BATTERY_EXTERNAL_POWER (0): the device is connected to an external power source
 * - 1 to 254: battery level, 1 is the minimum and 254 the maximum (see wisblockBatteryLevelFromPercent())
 * - WISBLOCK_BATTERY_UNKNOWN (255): the device cannot measure the battery level (default)
 */
#define WISBLOCK_BATTERY_EXTERNAL_POWER 0
#define WISBLOCK_BATTERY_MIN 1
#define WISBLOCK_BATTERY_MAX 254
#define WISBLOCK_BATTERY_UNKNOWN 255

/**
 * @brief Device status callback, returns the battery level for the DevStatusAns
 *
 * @return WISBLOCK_BATTERY_EXTERNAL_POWER, 1 to 254 or WISBLOCK_BATTERY_UNKNOWN
 */
typedef uint8_t (*WisBlockDeviceStatusCb)(void);

/**
 * @brief Convert a battery charge in percent into the battery level of the LoRaWAN specification
 *
 * @param percent Charge in percent, 0 to 100 (higher values count as 100)
 * @return 1 (empty) to 254 (full), never 0 (external power) or 255 (unknown)
 */
inline uint8_t wisblockBatteryLevelFromPercent(uint8_t percent)
{
	if (percent > 100)
	{
		percent = 100;
	}
	return (uint8_t)(WISBLOCK_BATTERY_MIN + ((uint16_t)percent * (WISBLOCK_BATTERY_MAX - WISBLOCK_BATTERY_MIN)) / 100);
}

/** Limits of the SX126x GFSK modem. */
#define WISBLOCK_FSK_BITRATE_MIN 600UL
#define WISBLOCK_FSK_BITRATE_MAX 300000UL
#define WISBLOCK_FSK_FDEV_MIN 600UL
#define WISBLOCK_FSK_FDEV_MAX 200000UL
#define WISBLOCK_FSK_BW_MIN 4800UL // RX bandwidth (double side band), RUI3 AT+PBW in FSK mode
#define WISBLOCK_FSK_BW_MAX 467000UL
#define WISBLOCK_FSK_SYNCWORD_MAX_LEN 8

/** LoRaWAN regional parameters. Mirrors smtc_modem_region_t from LBM. */
enum WisBlockRegion : uint8_t
{
	WISBLOCK_REGION_EU868 = 0,
	WISBLOCK_REGION_US915 = 1,
	WISBLOCK_REGION_AU915 = 2,
	WISBLOCK_REGION_AS923_1 = 3,
	WISBLOCK_REGION_AS923_2 = 4,
	WISBLOCK_REGION_AS923_3 = 5,
	WISBLOCK_REGION_AS923_4 = 6,
	WISBLOCK_REGION_KR920 = 7,
	WISBLOCK_REGION_IN865 = 8,
	WISBLOCK_REGION_RU864 = 9,
	WISBLOCK_REGION_CN470 = 10,
	WISBLOCK_REGION_CN470_RP_1_0 = 11,
	WISBLOCK_REGION_WW2G4 = 12, /* 2.4 GHz worldwide, if radio variant supports it */
	/** RP002-1.0.4 EU433 (433.05-434.79 MHz) - dropped from upstream LBM (Semtech no longer
	 * ships region_eu_433.c/.h/_defs.h), re-vendored in this library's src/lbm since
	 * RAKwireless WisBlock/WisCore modules still need it - see region_eu_433_defs.h's doc
	 * comment for the parameter sourcing. */
	WISBLOCK_REGION_EU433 = 13,
};

/**
 * LoRaWAN region, using RUI3's AT+BAND numbering (see the RUI3 AT command manual) rather than
 * WisBlockRegion's own SWL2001/LBM-mirroring order - this is the enumeration
 * WisBlockLoRaWAN::setRegion()/getRegion() and AT+BAND use, precisely so callers never have to
 * juggle two different "which region is this" numberings for the same setting.
 *
 * RUI3's LA915 (12) is part of RUI3's documented numbering but has no WisBlockRegion equivalent
 * at all in this vendored LBM build (its main.h doesn't define REGION_LA_915) -
 * wisblockRUI3BandToRegion() returns false for it rather than silently picking something else,
 * and setRegion()/AT+BAND reject it the same way. EU433 (0) *is* supported - see
 * WISBLOCK_REGION_EU433's doc comment.
 */
enum WisBlockRUI3Band : uint8_t
{
	WISBLOCK_RUI3_BAND_EU433 = 0,
	WISBLOCK_RUI3_BAND_CN470 = 1,
	WISBLOCK_RUI3_BAND_RU864 = 2,
	WISBLOCK_RUI3_BAND_IN865 = 3,
	WISBLOCK_RUI3_BAND_EU868 = 4,
	WISBLOCK_RUI3_BAND_US915 = 5,
	WISBLOCK_RUI3_BAND_AU915 = 6,
	WISBLOCK_RUI3_BAND_KR920 = 7,
	WISBLOCK_RUI3_BAND_AS923_1 = 8,
	WISBLOCK_RUI3_BAND_AS923_2 = 9,
	WISBLOCK_RUI3_BAND_AS923_3 = 10,
	WISBLOCK_RUI3_BAND_AS923_4 = 11,
	WISBLOCK_RUI3_BAND_LA915 = 12,
	/** 2.4 GHz worldwide (WISBLOCK_REGION_WW2G4). Not a RUI3 band: RUI3 has no 2.4 GHz region, this
	 * library number is added after the last RUI3 one. Needs the LR2021 radio and the library built with
	 * WISBLOCK_ENABLE_LR2021, otherwise setRegion() returns false and AT+BAND=13 is refused. */
	WISBLOCK_RUI3_BAND_WW2G4 = 13,
	/** Returned by wisblockRegionToRUI3Band()/getRegion() when the current WisBlockRegion has no
	 * RUI3 band index at all (WISBLOCK_REGION_CN470_RP_1_0 - a library-specific region beyond
	 * RUI3's set). Never a valid value to pass to setRegion(). */
	WISBLOCK_RUI3_BAND_UNKNOWN = 0xFF,
};

/**
 * @brief Convert a RUI3 AT+BAND index to a library region
 *
 * Converts a RUI3 AT+BAND index (WisBlockRUI3Band, as used by WisBlockLoRaWAN::setRegion() and
 * AT+BAND) to this library's internal SWL2001/LBM-mirroring WisBlockRegion enum.
 * @param band Region in RUI3 numbering
 * @param outRegion Receives the region
 * @return false (outRegion left untouched) for WISBLOCK_RUI3_BAND_LA915,
 * WISBLOCK_RUI3_BAND_UNKNOWN, or any other value with no WisBlockRegion equivalent in this
 * vendored LBM build. WISBLOCK_RUI3_BAND_WW2G4 converts here whatever the radio is, whether the
 * region can be used is a separate question, see wisblockRegionUsable().
 */
bool wisblockRUI3BandToRegion(WisBlockRUI3Band band, WisBlockRegion &outRegion);

/**
 * @brief Convert a library region to a RUI3 AT+BAND index
 *
 * Inverse of wisblockRUI3BandToRegion() - converts this library's internal WisBlockRegion enum to
 * the RUI3 AT+BAND numbering (WisBlockRUI3Band) used by WisBlockLoRaWAN::getRegion() and AT+BAND.
 * @param region Region of the library
 * @return WISBLOCK_RUI3_BAND_UNKNOWN for WisBlockRegion values with no RUI3 band index
 * (WISBLOCK_REGION_CN470_RP_1_0). Use WisBlockLoRaWAN::setRegion(WisBlockRegion) to select that one.
 */
WisBlockRUI3Band wisblockRegionToRUI3Band(WisBlockRegion region);

/**
 * @brief Check if a region can be used with the radio of this board
 *
 * Every region is usable except WISBLOCK_REGION_WW2G4 (2.4 GHz), which needs the LR2021 radio:
 * the library has to be built with WISBLOCK_ENABLE_LR2021 (wb_lbm_config.h) and the board
 * configuration passed to begin() must have the LR2021 (wisblockLoRaHwConfigRAK3401_RAK13702()). An
 * SX1262 cannot transmit at 2.4 GHz. The radio is known after WisBlockLoRaWAN::begin(), call this after it.
 * @param region Region of the library
 * @return true if the region can be used
 */
bool wisblockRegionUsable(WisBlockRegion region);

enum WisBlockDeviceClass : uint8_t
{
	WISBLOCK_CLASS_A = 0,
	WISBLOCK_CLASS_B = 1,
	WISBLOCK_CLASS_C = 2,
};

/**
 * @brief State of the Class B set-up, the S0 ... S3 of RUI3's AT+CLASS=? (B,S0 ... B,S3)
 *
 * Only meaningful while Class B is the requested class. A normal start is S0, S1, S2.
 */
enum WisBlockClassBState : uint8_t
{
	/** Class B is requested, the device is not joined yet (or the join was lost), or it is joined but
	 * has not sent an uplink since the class was set. */
	WISBLOCK_CLASS_B_S0 = 0,
	/** Joined and the first uplink is sent (it carries the time and ping slot requests): the stack
	 * waits for a beacon from the gateway. */
	WISBLOCK_CLASS_B_S1 = 1,
	/** Beacon locked: the ping slots are open, the device receives Class B downlinks. */
	WISBLOCK_CLASS_B_S2 = 2,
	/** The beacon lock failed (the beacon was missed) or was lost again. The stack keeps searching, the
	 * state returns to S2 when a beacon is received. */
	WISBLOCK_CLASS_B_S3 = 3,
};

/**
 * @brief Compute the Class B state from what the stack reports (the rules behind WisBlockClassBState)
 *
 * Used by LoRaWANEngine, a separate function so the rules can be tested without a radio.
 * @param joined The device is joined
 * @param classApplied The class B request was accepted by the stack (needs a joined device)
 * @param uplinkSent An uplink was sent since the join / since Class B was requested
 * @param beaconLocked The stack reports Class B ready (beacon locked)
 * @param beaconLost A lock was lost, or beacons were missed while searching
 * @return The state S0 ... S3
 */
WisBlockClassBState wisblockComputeClassBState(bool joined, bool classApplied, bool uplinkSent, bool beaconLocked,
											   bool beaconLost);

enum WisBlockJoinMode : uint8_t
{
	WISBLOCK_JOIN_OTAA = 0,
	WISBLOCK_JOIN_ABP = 1,
};

enum WisBlockJoinState : uint8_t
{
	WISBLOCK_JOIN_IDLE = 0,
	WISBLOCK_JOIN_IN_PROGRESS = 1,
	WISBLOCK_JOIN_SUCCEEDED = 2,
	WISBLOCK_JOIN_FAILED = 3,
	/** maxJoinAttempts reached - retrying has stopped. Distinguishes "gave up" from
	 * WISBLOCK_JOIN_FAILED's "this one attempt failed, another is already scheduled" -
	 * see LoRaWANEngine::setMaxJoinAttempts()'s doc comment. */
	WISBLOCK_JOIN_GAVE_UP = 4,
};

/** LoRa P2P bandwidth options (matches SX126x LORA_BW_* indices). */
enum WisBlockP2PBandwidth : uint8_t
{
	WISBLOCK_BW_125 = 0,
	WISBLOCK_BW_250 = 1,
	WISBLOCK_BW_500 = 2,
	WISBLOCK_BW_062 = 3, // 62.5 kHz
	WISBLOCK_BW_041 = 4, // 41.67 kHz
	WISBLOCK_BW_031 = 5, // 31.25 kHz
	WISBLOCK_BW_020 = 6, // 20.83 kHz
	WISBLOCK_BW_015 = 7, // 15.63 kHz
	WISBLOCK_BW_010 = 8, // 10.42 kHz
	WISBLOCK_BW_007 = 9, // 7.81 kHz
};

enum WisBlockP2PCodingRate : uint8_t
{
	WISBLOCK_CR_4_5 = 1,
	WISBLOCK_CR_4_6 = 2,
	WISBLOCK_CR_4_7 = 3,
	WISBLOCK_CR_4_8 = 4,
};

/** Parameters for LoRa Basics Modem OTAA join. */
struct WisBlockOTAAKeys
{
	uint8_t devEui[8] = {0xac, 0x1f, 0x09, 0xff, 0xfe, 0x00, 0x00, 0x00};
	uint8_t joinEui[8] = {0x70, 0xb3, 0xd5, 0x7e, 0xd0, 0x02, 0x01, 0xe1};
	uint8_t appKey[16] = {0x2b, 0x84, 0xe0, 0xb0, 0x9b, 0x68, 0xe5, 0xcb, 0x42, 0x17, 0x6f, 0xe7, 0x53, 0xdc, 0xee, 0x79}; // Also used as NwkKey in LoRaWAN 1.1
};

/** Parameters for ABP activation. */
struct WisBlockABPKeys
{
	uint32_t devAddr = 0;
	uint8_t nwkSKey[16] = {0};
	uint8_t appSKey[16] = {0};
};

/** LoRaWAN-mode runtime/persisted settings. */
struct WisBlockLoRaWANSettings
{
	WisBlockRegion region = WISBLOCK_REGION_EU868;
	WisBlockDeviceClass deviceClass = WISBLOCK_CLASS_A;
	WisBlockJoinMode joinMode = WISBLOCK_JOIN_OTAA;
	uint8_t dataRate = 0;
	bool adrEnabled = true;
	uint8_t txPower = 0; // index, region-specific meaning
	bool confirmedUplinks = false;
	/** Sub-band pre-selection for US915/AU915/CN470/CN470_RP_1_0 (ignored elsewhere) - see
	 * LoRaWANEngine::setChannelMask() and WisBlockLoRaWAN::setChannelMask() for the encoding.
	 * 0 = no restriction (all channels enabled), matching AT+MASK's own ALL=0000 convention. */
	uint16_t channelMask = 0;
	/** RUI3-compatible AT+PGSLOT (Class B unicast ping slot periodicity, 0-7) - see
	 * LoRaWANEngine::setPingSlotPeriodicity()'s doc comment. Pushed to LBM every time settings
	 * are (re)applied, same as channelMask above - harmless for Class A/C, and lets it be
	 * pre-configured before ever switching to Class B. */
	uint8_t pingSlotPeriodicity = 0;
	/** FIX (Class A pending-downlink bug): when a downlink's FPending bit is set, the network
	 * has more downlinks queued, but a Class A device can only receive them in response to an
	 * uplink - per LoRaWAN 1.0.4 section 5.1 the device should send another uplink promptly to
	 * open another receive window, not wait for its next regular scheduled transmission. When
	 * true (default), this library does that automatically (an empty, unconfirmed uplink on the
	 * same FPort the pending-flagged downlink arrived on) - see LoRaWANEngine::handleEvents()'s
	 * SMTC_MODEM_EVENT_DOWNDATA case. Set false to handle it yourself instead (e.g. if duty-cycle
	 * budget is tight and an extra uplink per pending downlink isn't acceptable) - WisBlockRxResult::fpending
	 * still reports the bit either way. Only applies to Class A; Class B/C already have a standing
	 * receive window (ping slots / continuous RXC) so the network can just push the next downlink
	 * without this device needing to ask for it. */
	bool fetchPendingDownlinks = true;
	/** RUI3-compatible AT+JOIN / api.lorawan.join parameters - see LoRaWANEngine::setAutoJoin()/
	 * setJoinReattemptInterval()/setMaxJoinAttempts()'s doc comments for the full mechanism.
	 * autoJoin: join automatically once the LoRaWAN engine starts, instead of waiting for an
	 * explicit join()/AT+JOIN. Default false (matches this library's previous behavior).
	 * joinReattemptIntervalS: seconds between join attempts after a failure, 7-255, RUI3 default 8.
	 * maxJoinAttempts: give up after this many failed attempts, 0-255, 0 = retry forever (RUI3 default). */
	bool autoJoin = false;
	uint8_t joinReattemptIntervalS = 8;
	uint8_t maxJoinAttempts = 0;
	WisBlockOTAAKeys otaa;
	WisBlockABPKeys abp;
};

/** LoRa P2P mode runtime/persisted settings. */
struct WisBlockP2PSettings
{
	uint32_t frequencyHz = 916000000UL;
	uint8_t spreadingFactor = 7;  // SF7..SF12
	WisBlockP2PBandwidth bandwidth = WISBLOCK_BW_125;
	WisBlockP2PCodingRate codingRate = WISBLOCK_CR_4_5;
	uint16_t preambleLength = 8;
	int8_t txPowerDbm = 14;
	bool cadEnabled = false;
	uint16_t symbolTimeout = 0;
	// Default true to match this library's pre-existing behavior (the
	// SX1262 powers up with boosted gain already selected, and nothing
	// previously configured this register explicitly either way - see
	// LoRaP2PEngine::applyRadioParams()). Boosted trades roughly 4-5mA of
	// extra RX current for a few dB of sensitivity; set false via
	// setP2PRxBoostedGain() if your link budget doesn't need it and you'd
	// rather have the lower RX current.
	bool rxBoostedGainEnabled = true;
	/** Invert the IQ signals on TX and RX (RUI3 AT+IQINVER). Both ends of a link must match. */
	bool iqInversion = false;
	/**
	 * 16-bit LoRa sync word register value (RUI3 AT+SYNCWORD). 0x1424 = "private" (the SX126x
	 * power-on default, also what RUI3 P2P uses), 0x3444 = "public" (LoRaWAN). Both ends of a
	 * link must match.
	 */
	uint16_t syncWord = 0x1424;

	// ---- FSK (GFSK) P2P, used when modulation == WISBLOCK_P2P_MOD_FSK. Appended after the LoRa
	// fields (persisted config version 5). preambleLength, txPowerDbm, frequencyHz and
	// rxBoostedGainEnabled are shared with LoRa; preambleLength counts BYTES in FSK mode.
	// cadEnabled, spreadingFactor, bandwidth, codingRate, iqInversion and syncWord are LoRa only.
	WisBlockP2PModulation modulation = WISBLOCK_P2P_MOD_LORA;
	/** FSK bit rate in bit/s (RUI3 AT+PBR), 600..300000 */
	uint32_t fskBitrate = 50000;
	/** FSK frequency deviation in Hz (RUI3 AT+PFDEV), 600..200000 */
	uint32_t fskFreqDevHz = 25000;
	/** FSK RX bandwidth in Hz (RUI3 AT+PBW in FSK mode), 4800..467000. The next larger step of the
	 * radio is used. It should be at least bit rate + 2 * deviation. */
	uint32_t fskBandwidthHz = 117300;
	/** Number of valid bytes in fskSyncWord, 1..8 */
	uint8_t fskSyncWordLen = 3;
	/** FSK sync word, both ends must match. Default 0xC1 0x94 0xC1 (Semtech reference). */
	uint8_t fskSyncWord[WISBLOCK_FSK_SYNCWORD_MAX_LEN] = {0xC1, 0x94, 0xC1, 0, 0, 0, 0, 0};
};

/** Result of a CAD (Channel Activity Detection) operation. */
enum WisBlockCADResult : uint8_t
{
	WISBLOCK_CAD_CHANNEL_CLEAR = 0,
	WISBLOCK_CAD_CHANNEL_DETECTED = 1,
	WISBLOCK_CAD_ERROR = 2,
};

/** Generic RX result payload passed to callbacks (LoRaWAN + P2P share the shape). */
struct WisBlockRxResult
{
	uint8_t port = 0;	// LoRaWAN only; 0 for P2P
	uint8_t data[242] = {0};
	uint8_t length = 0;
	int16_t rssi = 0;
	int8_t snr = 0;
	/** LoRaWAN only (always false for P2P). Mirrors the downlink's FPending bit: the network
	 * has more downlinks queued for this device. See WisBlockLoRaWANSettings::fetchPendingDownlinks
	 * - by default this library automatically sends an empty uplink to drain them (Class A only;
	 * Class B/C already have a standing receive window and don't need one), so this field is
	 * mainly informational unless that's been disabled. */
	bool fpending = false;
	/** LoRaWAN only (always false for P2P). True if this downlink arrived on one of this
	 * device's multicast RX windows (Class B or C multicast group 0-3 - see
	 * WisBlockLoRaWAN::setMulticastGroup()) rather than its own unicast RX1_W/RX2_W/RXC/RXB
	 * window. See multicastGroupId below for which group. */
	bool isMulticast = false;
	/** Which multicast group (0-3, as used by setMulticastGroup()/AT+ADDMULC) this downlink
	 * arrived on - only meaningful when isMulticast is true; 0 otherwise (group 0 is also a
	 * valid group ID, so check isMulticast first, don't infer it from this being non-zero). */
	uint8_t multicastGroupId = 0;
};

struct WisBlockTxResult
{
	bool success = false;
	uint32_t airtimeMs = 0;
};

/** Number of LoRaWAN multicast groups that can be configured at once - a hard limit of the
 * underlying LoRa Basics Modem stack (multicast group IDs 0-3), not a choice made by this
 * library. See WisBlockMulticastGroup below. */
static const uint8_t WISBLOCK_MULTICAST_GROUP_COUNT = 4;

/**
 * One LoRaWAN multicast group's configuration, as set by
 * WisBlockLoRaWAN::setMulticastGroup() / AT+ADDMULC. Up to
 * WISBLOCK_MULTICAST_GROUP_COUNT (4) of these can be configured at once - a
 * hard limit of the underlying LoRa Basics Modem stack (multicast group IDs
 * 0-3), not a choice made by this library.
 *
 * Unlike WisBlockOTAAKeys::appKey and WisBlockABPKeys::nwkSKey/appSKey
 * (deliberately not readable back in plaintext - see their own AT+APPKEY=?/
 * AT+NWKSKEY=?/AT+APPSKEY=? handlers' doc comments), these multicast
 * session keys ARE readable back in plaintext via getMulticastGroup()/
 * AT+LSTMULC=?, matching RUI3's own AT+LSTMULC behavior (its documented
 * example output echoes both keys directly) - a deliberate choice to match
 * RUI3, not an oversight; see Creation-Log-From-Claude-AI.md for the
 * background on why plaintext readback is the wanted behavior here.
 *
 * Not persisted across reboots (no AT+SAVE/AT+RESTORE involvement) -
 * multicast group provisioning is normally redone by the network operator
 * each session anyway (whether manually via AT+ADDMULC or, once supported,
 * automatically via the LoRaWAN Remote Multicast Setup package - see
 * extra_script.py's build-flags documentation for that package's current
 * status in this library).
 */
struct WisBlockMulticastGroup
{
	bool configured = false;
	/** Only WISBLOCK_CLASS_B or WISBLOCK_CLASS_C are valid here - a multicast group makes no
	 * sense for Class A, which has no standing RX window to receive it on. */
	WisBlockDeviceClass deviceClass = WISBLOCK_CLASS_C;
	uint32_t devAddr = 0;
	uint8_t nwkSKey[16] = {0};
	uint8_t appSKey[16] = {0};
	uint32_t frequencyHz = 0;
	uint8_t dataRate = 0;
	/** Class B ping slot periodicity (0-7, same meaning as WisBlockLoRaWANSettings::
	 * pingSlotPeriodicity, but per-group rather than device-wide) - ignored for Class C, but
	 * RUI3's own AT+ADDMULC still requires a value for it even then (matching that convention
	 * here rather than making the parameter conditionally optional). */
	uint8_t periodicity = 0;
};

struct WisBlockLinkCheckResult
{
	uint8_t demodMargin = 0;
	uint8_t gatewayCount = 0;
};

struct WisBlockTimeAnswer
{
	uint32_t gpsEpochSeconds = 0;
	uint32_t fractionalSeconds = 0;
};

#endif // WISBLOCK_LORAWAN_TYPES_H
