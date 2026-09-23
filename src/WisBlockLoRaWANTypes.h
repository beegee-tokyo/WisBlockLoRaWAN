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
};

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
};

enum WisBlockDeviceClass : uint8_t
{
	WISBLOCK_CLASS_A = 0,
	WISBLOCK_CLASS_B = 1,
	WISBLOCK_CLASS_C = 2,
};

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
	 * LoRaWANEngine::setChannelMask()/WisBlockLoRaWAN::setChannelMask() for the encoding.
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
};

struct WisBlockTxResult
{
	bool success = false;
	uint32_t airtimeMs = 0;
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
