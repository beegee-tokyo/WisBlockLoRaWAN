/**
 * \file      region_eu_433_defs.h
 *
 * \brief     region_eu_433_defs  abstraction layer definition
 *
 * \details   EU433 was dropped from this vendored LoRa Basics Modem release (Semtech no longer
 * ships a region_eu_433.c/.h/_defs.h triplet here), but RAKwireless WisBlock/WisCore modules
 * still need to support it, so this file restores it. Parameters below are the RP002-1.0.4
 * "EU433" defaults: 433.05-434.79 MHz, 3 mandatory channels (433.175/433.375/433.575 MHz,
 * DR0-DR5), no dwell time, default Max EIRP +12.15 dBm (rounded to 12 dBm here, matching how
 * this file's TX_POWER_EIRP_xxx macros are defined in dBm integers elsewhere in this codebase),
 * 1% duty-cycle (single global band, no EU868-style sub-bands), RX2 on 434.665 MHz/DR0, and no
 * LR-FHSS (RP002-1.0.4 never added LR-FHSS optional data rates for EU433 the way it did for
 * EU868/US915/AU915). Cross-checked against Semtech's own (now region-disabled but still
 * present) RegionEU433.h in Lora-net/LoRaMac-node, and against this codebase's region_in_865,
 * whose DR0-7/M-table shape EU433 shares.
 *
 * The Clear BSD License
 * Copyright Semtech Corporation 2021. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted (subject to the limitations in the disclaimer
 * below) provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the Semtech corporation nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE GRANTED BY
 * THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
 * CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT
 * NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL SEMTECH CORPORATION BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef REGION_EU_433_DEFS_H
#define REGION_EU_433_DEFS_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * -----------------------------------------------------------------------------
 * --- DEPENDENCIES ------------------------------------------------------------
 */

#include <stdint.h>
#include <stdbool.h>

#include "lr1mac_defs.h"

/*
 * -----------------------------------------------------------------------------
 * --- PUBLIC MACROS -----------------------------------------------------------
 */

/* clang-format off */
#define NUMBER_OF_CHANNEL_EU_433            (16)
#define NUMBER_OF_BOOT_TX_CHANNEL_EU_433    (3)             // define the number of channel at boot
#define JOIN_ACCEPT_DELAY1_EU_433           (5)             // define in seconds
#define JOIN_ACCEPT_DELAY2_EU_433           (6)             // define in seconds
#define RECEIVE_DELAY1_EU_433               (1)             // define in seconds
#define TX_POWER_EIRP_EU_433                (12)            // define in dbm - RP002-1.0.4 default Max EIRP is +12.15dBm
#define MAX_TX_POWER_IDX_EU_433             (5)             // index ex LinkADRReq - 6 steps (0..5), 2dB/step
#define ADR_ACK_LIMIT_EU_433                (64)
#define ADR_ACK_DELAY_EU_433                (32)
#define ACK_TIMEOUT_EU_433                  (2)             // +/- 1 s (random delay between 1 and 3 seconds)
#define FREQMIN_EU_433                      (433050000)     // Hz
#define FREQMAX_EU_433                      (434790000)     // Hz
#define RX2_FREQ_EU_433                     (434665000)     // Hz
#define FREQUENCY_FACTOR_EU_433             (100)           // MHz/100 when coded over 24 bits
#define RX2DR_INIT_EU_433                   (0)
#define SYNC_WORD_PRIVATE_EU_433            (0x12)
#define SYNC_WORD_PUBLIC_EU_433             (0x34)
#define MIN_TX_DR_EU_433                    (0)
#define MAX_TX_DR_EU_433                    (7)
#define MIN_TX_DR_LIMIT_EU_433              (0)
#define NUMBER_OF_TX_DR_EU_433              (8)
#define MIN_RX_DR_EU_433                    (0)
#define MAX_RX_DR_EU_433                    (7)

#define DR_BITFIELD_SUPPORTED_EU_433        (uint16_t)( ( 1 << DR7 ) | ( 1 << DR6 ) | \
                                                        ( 1 << DR5 ) | ( 1 << DR4 ) | ( 1 << DR3 ) | ( 1 << DR2 ) | ( 1 << DR1 ) | ( 1 << DR0 ) )

#define DEFAULT_TX_DR_BIT_FIELD_EU_433      (uint16_t)( ( 1 << DR5 ) | ( 1 << DR4 ) | ( 1 << DR3 ) | ( 1 << DR2 ) | ( 1 << DR1 ) | ( 1 << DR0 ) )
#define TX_PARAM_SETUP_REQ_SUPPORTED_EU_433 (false)         // This mac command is NOT required for EU433
#define NEW_CHANNEL_REQ_SUPPORTED_EU_433    (true)
#define DTC_SUPPORTED_EU_433                (true)
#define LBT_SUPPORTED_EU_433                (false)
#define LBT_SNIFF_DURATION_MS_EU_433        (5)
#define LBT_THRESHOLD_DBM_EU_433            (int16_t)(-80)
#define LBT_BW_HZ_EU_433                    (200000)
#define CF_LIST_SUPPORTED_EU_433            (CF_LIST_FREQ)

// Class B
#define BEACON_DR_EU_433                    (3)
#define BEACON_FREQ_EU_433                  (434665000)     // Hz
#define PING_SLOT_FREQ_EU_433               (434665000)     // Hz
/* clang-format on */

/*
 * -----------------------------------------------------------------------------
 * --- PUBLIC TYPES ------------------------------------------------------------
 */

/**
 * Bank contains 8 channels
 */
typedef enum eu_433_channels_bank_e
{
    BANK_0_EU433 = 0,  // 0 to 7 channels
    BANK_1_EU433 = 1,  // 8 to 15 channels
    BANK_MAX_EU433
} eu_433_channels_bank_t;

/**
 * Bands enumeration - EU433 is a single global band (no EU868-style sub-bands): the whole
 * 433.05-434.79 MHz range shares one 1% duty-cycle budget (RP002-1.0.4, EN300220's 10% legal
 * limit derated to LoRaWAN's own 1% to avoid network congestion).
 */
typedef enum region_eu_433_band_e
{
    BAND_EU433_GLOBAL = 0,
    BAND_EU433_MAX
} region_eu_433_band_t;

typedef struct region_eu433_context_s
{
    uint32_t tx_frequency_channel[NUMBER_OF_CHANNEL_EU_433];
    uint32_t rx1_frequency_channel[NUMBER_OF_CHANNEL_EU_433];
    uint16_t dr_bitfield_tx_channel[NUMBER_OF_CHANNEL_EU_433];
    uint8_t  dr_distribution_init[NUMBER_OF_TX_DR_EU_433];
    uint8_t  dr_distribution[NUMBER_OF_TX_DR_EU_433];
    uint8_t  join_dr_distribution[NUMBER_OF_TX_DR_EU_433];
    uint8_t  custom_dr_distribution_init[NUMBER_OF_TX_DR_EU_433];
    uint8_t  channel_index_enabled[BANK_MAX_EU433];   // Enable by Network
    uint8_t  unwrapped_channel_mask[BANK_MAX_EU433];  // Temp conf send by Network
} region_eu433_context_t;

/*
 * -----------------------------------------------------------------------------
 * --- PUBLIC CONSTANTS --------------------------------------------------------
 */

static const uint8_t SYNC_WORD_GFSK_EU_433[] = { 0xC1, 0x94, 0xC1 };

/**
 * Default frequencies at boot - the 3 mandatory join channels (RP002-1.0.4 EU433, cannot be
 * remapped by NewChannelReq).
 */
static const uint32_t default_freq_eu_433[] = { 433175000, 433375000, 433575000 };

/**
 * Up/Down link data rates offset definition - identical shape to EU868's DR0-7 (same SF/BW/FSK
 * datarates and the same MinRX1DROffset..MaxRX1DROffset = 0..5 range), EU433 simply has no
 * DR8-11 LR-FHSS rows.
 */
static const uint8_t datarate_offsets_eu_433[8][6] = {
    { 0, 0, 0, 0, 0, 0 },  // DR 0
    { 1, 0, 0, 0, 0, 0 },  // DR 1
    { 2, 1, 0, 0, 0, 0 },  // DR 2
    { 3, 2, 1, 0, 0, 0 },  // DR 3
    { 4, 3, 2, 1, 0, 0 },  // DR 4
    { 5, 4, 3, 2, 1, 0 },  // DR 5
    { 6, 5, 4, 3, 2, 1 },  // DR 6
    { 7, 6, 5, 4, 3, 2 },  // DR 7
};

/**
 * @brief uplink datarate backoff
 *
 */
static const uint8_t datarate_backoff_eu_433[] = {
    0,  // DR0 -> DR0
    0,  // DR1 -> DR0
    1,  // DR2 -> DR1
    2,  // DR3 -> DR2
    3,  // DR4 -> DR3
    4,  // DR5 -> DR4
    5,  // DR6 -> DR5
    6,  // DR7 -> DR6
};

static const uint8_t NUMBER_RX1_DR_OFFSET_EU_433 =
    sizeof( datarate_offsets_eu_433[0] ) / sizeof( datarate_offsets_eu_433[0][0] );

/**
 * Data rates table definition
 */
static const uint8_t datarates_to_sf_eu_433[] = { 12, 11, 10, 9, 8, 7, 7 };

/**
 * Bandwidths table definition in KHz
 */
static const uint32_t datarates_to_bandwidths_eu_433[] = { BW125, BW125, BW125, BW125, BW125, BW125, BW250 };

/**
 * Payload max size table definition in bytes with FHDROFFSET - identical to this codebase's
 * M_eu_868/M_in_865 DR0-7 entries: max payload size only depends on SF/BW/coding, not on which
 * sub-GHz band is used, and EU433's DR0-7 use exactly the same modulation parameters as EU868's.
 */
static const uint8_t M_eu_433[8] = { 59, 59, 59, 123, 250, 250, 250, 250 };

/**
 * Mobile long range datarate distribution
 * DR0: 20%,
 * DR1: 20%,
 * DR2: 30%,
 * DR3: 30%,
 * DR4:  0%,
 * DR5:  0%,
 * DR6:  0%,
 * DR7:  0%
 */
static const uint8_t MOBILE_LONGRANGE_DR_DISTRIBUTION_EU_433[] = { 2, 2, 3, 3, 0, 0, 0, 0 };

/**
 * Mobile low power datarate distribution
 * DR0:  0%,
 * DR1:  0%,
 * DR2: 10%,
 * DR3: 30%,
 * DR4: 30%,
 * DR5: 30%,
 * DR6:  0%,
 * DR7:  0%
 */
static const uint8_t MOBILE_LOWPER_DR_DISTRIBUTION_EU_433[] = { 0, 0, 1, 3, 3, 3, 0, 0 };

/**
 * Join datarate distribution
 * DR0:  5%,
 * DR1: 10%,
 * DR2: 15%,
 * DR3: 20%,
 * DR4: 20%,
 * DR5: 30%,
 * DR6:  0%,
 * DR7:  0%
 */
static const uint8_t JOIN_DR_DISTRIBUTION_EU_433[] = { 1, 2, 3, 4, 4, 6, 0, 0 };

/**
 * Default datarate distribution
 * DR0: 100%,
 * DR1:   0%,
 * DR2:   0%,
 * DR3:   0%,
 * DR4:   0%,
 * DR5:   0%,
 * DR6:   0%,
 * DR7:   0%
 */
static const uint8_t DEFAULT_DR_DISTRIBUTION_EU_433[] = { 1, 0, 0, 0, 0, 0, 0, 0 };

/**
 * Duty Cycle table definition by bands - single global band at 1% (RP002-1.0.4 EU433), unlike
 * EU868's several sub-bands.
 */
static const uint16_t duty_cycle_by_band_eu_433[BAND_EU433_MAX] = {
    [BAND_EU433_GLOBAL] = 100,  // 1 %
};

static const uint32_t frequency_range_by_band_eu_433[BAND_EU433_MAX][2] = {
    [BAND_EU433_GLOBAL] = { 433050000, 434790001 },
};

/*
 * -----------------------------------------------------------------------------
 * --- PUBLIC FUNCTIONS PROTOTYPES ---------------------------------------------
 */

#ifdef __cplusplus
}
#endif

#endif  // REGION_EU_433_DEFS_H

/* --- EOF ------------------------------------------------------------------ */
