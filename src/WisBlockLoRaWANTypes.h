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

/** Relay function, introduced in LoRaWAN 1.0.4 / RP002-1.0.4. */
enum WisBlockRelayMode : uint8_t
{
	WISBLOCK_RELAY_OFF = 0,
	WISBLOCK_RELAY_ED = 1,	  /**< End-device relies on a relay to forward its uplinks (WOR). */
	WISBLOCK_RELAY_SERVING = 2, /**< This device acts as the serving relay for nearby end-devices. */
};

enum WisBlockJoinState : uint8_t
{
	WISBLOCK_JOIN_IDLE = 0,
	WISBLOCK_JOIN_IN_PROGRESS = 1,
	WISBLOCK_JOIN_SUCCEEDED = 2,
	WISBLOCK_JOIN_FAILED = 3,
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
	uint8_t devEui[8] = {0};
	uint8_t joinEui[8] = {0};
	uint8_t appKey[16] = {0}; // Also used as NwkKey in LoRaWAN 1.1
};

/** Parameters for ABP activation. */
struct WisBlockABPKeys
{
	uint32_t devAddr = 0;
	uint8_t nwkSKey[16] = {0};
	uint8_t appSKey[16] = {0};
};

/** Mirrors smtc_modem_relay_tx_config_t (smtc_modem_relay_api.h) - see LoRaWANRelay.h for details. */
struct WisBlockRelayEDConfig
{
	uint8_t activationMode = 0; // smtc_modem_relay_tx_activation_mode_t: 0=disabled,1=enable,2=dynamic,3=ED_controlled
	uint8_t smartLevel = 0;
	uint8_t backoff = 0;
	uint8_t missedWorAckToNoSync = 8; // number_of_miss_wor_ack_to_switch_in_nosync_mode
	bool secondChannelEnable = false;
	uint32_t secondChannelFreqHz = 0;
	uint32_t secondChannelAckFreqHz = 0;
	uint8_t secondChannelDr = 0;
};

/** Mirrors relay_config_t (relay_rx_api.h) + relay_init()'s own parameters - see LoRaWANRelay.h for details. */
struct WisBlockRelayServingConfig
{
	uint8_t cadPeriod = 0;	 // wor_cad_periodicity_t: 0=1s,1=500ms,2=250ms,3=100ms,4=50ms,5=20ms
	uint32_t channelFreqHz = 0;
	uint32_t channelAckFreqHz = 0;
	uint8_t channelDr = 0;
	uint8_t errorPpm = 1;	 // wor_ack_ppm_error_t: 0=10ppm,1=20ppm,2=30ppm,3=40ppm
	uint8_t cadToRxSymb = 0; // wor_ack_cad_to_rx_t: 0=2symb,1=4symb,2=6symb,3=8symb
};

/**
 * One entry in the serving relay's trusted end-device list. Not persisted
 * in WisBlockLoRaWANSettings/flash (unlike the two configs above) - see the
 * IMPORTANT note in LoRaWANRelay.h; without at least one of these
 * registered, a serving relay forwards nothing.
 */
struct WisBlockRelayTrustedDevice
{
	uint8_t index = 0; // 0-15, matches relay_fwd_uplink_add_device()'s slot count
	uint32_t devAddr = 0;
	uint8_t rootWorSKey[16] = {0}; // TS011 WOR root session key - derived out-of-band, not by this library
	bool unlimitedForward = true;
	uint8_t bucketFactor = 0;
	uint8_t reloadRate = 0;
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
	WisBlockRelayMode relayMode = WISBLOCK_RELAY_OFF;
	WisBlockRelayEDConfig relayEDConfig;
	WisBlockRelayServingConfig relayServingConfig;
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
