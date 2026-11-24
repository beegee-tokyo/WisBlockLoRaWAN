/**
 * \file      region_la_915_defs.h
 *
 * \brief     region_la_915_defs abstraction layer definition
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

/*
 * WisBlockLoRaWAN: LA915 (Latin America 915 MHz) region, library addition.
 *
 * LA915 (a.k.a. LA915A) is an UNOFFICIAL channel plan, used by network operators in Latin America
 * (Everynet and others). It is derived from the official LoRa Alliance AU915-928 plan, so this file set
 * (region_la_915.c/.h/_defs.h) is a copy of region_au_915 with these differences:
 *
 *  - own region id (SMTC_REAL_REGION_LA_915 = 15, SMTC_MODEM_REGION_LA_915 = 15),
 *  - only the first 8 uplink channels (CH0-CH7, 915.2 - 916.6 MHz, 125 kHz, DR0-DR5) are enabled by
 *    default (ChannelsDefaultMask[0] = 0x00FF). The 500 kHz channels and the 125 kHz channels 8-63
 *    are known to the stack but stay disabled until the application (AT+MASK / setChannelMask()) or
 *    the network (LinkADRReq / ChMask) enables them. See DEFAULT_CH_MASK_*_LA_915 below,
 *  - the channel / data rate / RX1 / RX2 definitions are the AU915 ones (RX1 downlink
 *    923.3 MHz + (uplink channel % 8) * 0.6 MHz, RX2 923.3 MHz DR8 (SF12/500 kHz)),
 *  - uplink dwell time is NOT limited (UPLINK_DWELL_TIME_LA_915 = false), so DR0 and DR1 are usable
 *    (AU915 has the dwell time limit on by default, which excludes DR0 and DR1),
 *  - there is no staged "first 8 channels, then the rest" opening of the channel mask after the join:
 *    the default mask above stays active until a network ChMask changes it.
 *
 * RX1 delay is the LoRaWAN default 1 s (RECEIVE_DELAY1), the join accept delays are 5 s / 6 s
 * (JOIN_ACCEPT_DELAY1/2) as in AU915. The attached "LA915-Regional-Parameters.txt" lists 5 s / 6 s as
 * "RX1/RX2 receive delay", those values are the JOIN accept delays; for normal uplinks the network
 * server sets the RX1 delay through RXTimingSetupReq (default 1 s).
 */

#ifndef REGION_LA915_DEFS_H
#define REGION_LA915_DEFS_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * -----------------------------------------------------------------------------
 * --- DEPENDENCIES ------------------------------------------------------------
 */

#include <stdint.h>
#include <stdbool.h>

#include "../../lr1mac_defs.h"

/*
 * -----------------------------------------------------------------------------
 * --- PUBLIC MACROS -----------------------------------------------------------
 */

/* clang-format off */
#define NUMBER_OF_TX_CHANNEL_LA_915         (72)            // TX 64 125KHz + 8 500KHz channels
#define NUMBER_OF_RX_CHANNEL_LA_915         (8)             // RX 8 500KHz channels
#define JOIN_ACCEPT_DELAY1_LA_915           (5)             // define in seconds
#define JOIN_ACCEPT_DELAY2_LA_915           (6)             // define in seconds
#define RECEIVE_DELAY1_LA_915               (1)             // define in seconds
#if defined( LR11XX ) || defined( LR20XX ) || defined( SX1262 ) || defined( SX1268 )
// This value must be the MIN of MAX supported by the region and the radio, region is 30dBm but radio is 22dBm ERP (+2 to EIRP) 
#define TX_POWER_EIRP_LA_915                (24)            // define in dbm
#else
// This value must be the MIN of MAX supported by the region and the radio, region is 30dBm but radio is 14dBm ERP (+2 to EIRP)
#define TX_POWER_EIRP_LA_915                (16)            // define in dbm regional parameters 1.0.2  // TODO must be checked, SX126x dependent for the max power
#endif
#define MAX_TX_POWER_IDX_LA_915             (14)            // index ex LinkADRReq
#define ADR_ACK_LIMIT_LA_915                (64)
#define ADR_ACK_DELAY_LA_915                (32)
#define ACK_TIMEOUT_LA_915                  (2)             // +/- 1 s (random delay between 1 and 3 seconds)
#define FREQMIN_LA_915                      (915000000)     // Hz
#define FREQMAX_LA_915                      (928000000)     // Hz
#define RX2_FREQ_LA_915                     (923300000)     // Hz
#define FREQUENCY_FACTOR_LA_915             (100)           // MHz/100 when coded over 24 bits
#define RX2DR_INIT_LA_915                   (8)
#define SYNC_WORD_PRIVATE_LA_915            (0x12)
#define SYNC_WORD_PUBLIC_LA_915             (0x34)
#define MIN_TX_DR_LA_915                    (0)
#define MAX_TX_DR_LORA_LA_915               (6)
#define MAX_TX_DR_LA_915                    (7)
#define MIN_TX_DR_LIMIT_LA_915              (2)
#define NUMBER_OF_TX_DR_LA_915              (8)
#define MIN_RX_DR_LA_915                    (8)
#define MAX_RX_DR_LA_915                    (13)

#if defined( RP2_101 )
#define DR_BITFIELD_SUPPORTED_LA_915        (uint16_t)( ( 1 << DR13 ) | ( 1 << DR12 ) | \
                                                        ( 1 << DR11 ) | ( 1 << DR10 ) | ( 1 << DR9 ) | ( 1 << DR8 ) | ( 1 << DR6 ) | \
                                                        ( 1 << DR5 ) | ( 1 << DR4 ) | ( 1 << DR3 ) | ( 1 << DR2 ) | ( 1 << DR1 ) | ( 1 << DR0 ) )
#elif defined( RP2_103 )
#define DR_BITFIELD_SUPPORTED_LA_915        (uint16_t)( ( 1 << DR13 ) | ( 1 << DR12 ) | \
                                                        ( 1 << DR11 ) | ( 1 << DR10 ) | ( 1 << DR9 ) | ( 1 << DR8 ) | ( 1 << DR7 ) | ( 1 << DR6 ) | \
                                                        ( 1 << DR5 ) | ( 1 << DR4 ) | ( 1 << DR3 ) | ( 1 << DR2 ) | ( 1 << DR1 ) | ( 1 << DR0 ) )
#endif

#define DEFAULT_TX_DR_125_BIT_FIELD_LA_915  (uint16_t)( ( 1 << DR5 ) | ( 1 << DR4 ) | ( 1 << DR3 ) | ( 1 << DR2 ) | ( 1 << DR1 ) | ( 1 << DR0 ) )
#if defined( RP2_101 )
#define DEFAULT_TX_DR_500_BIT_FIELD_LA_915  (uint16_t)( ( 1 << DR6 ) )
#elif defined( RP2_103 )
#define DEFAULT_TX_DR_500_BIT_FIELD_LA_915  (uint16_t)( ( 1 << DR7 ) | ( 1 << DR6 ) )
#endif

#define TX_PARAM_SETUP_REQ_SUPPORTED_LA_915 (true)
#define NEW_CHANNEL_REQ_SUPPORTED_LA_915    (false)         // This mac command is NOT required for LA915
#define DTC_SUPPORTED_LA_915                (false)
#define LBT_SUPPORTED_LA_915                (false)

#define UPLINK_DWELL_TIME_LA_915            (false)         // LA915: no dwell time limit (AU915: true)

#define CF_LIST_SUPPORTED_LA_915            (CF_LIST_CH_MASK)

// Default (boot / after reset) enabled channels. One byte per bank of 8 channels, bit N = channel 8 * bank + N
// 125 kHz bank 0 = CH0 - CH7 (915.2 - 916.6 MHz) = ChannelsDefaultMask[0] 0x00FF. Banks 1-7 (CH8 - CH63, 125 kHz)
// and bank 8 (CH64 - CH71, 500 kHz) are disabled. Change them here if a network needs another default.
#define DEFAULT_CH_MASK_125_BANK0_LA_915    (0xFF)
#define DEFAULT_CH_MASK_125_OTHER_LA_915    (0x00)
#define DEFAULT_CH_MASK_500_LA_915          (0x00)

#define DEFAULT_TX_FREQ_125_START_LA_915    (915200000) // Hz
#define DEFAULT_TX_STEP_125_LA_915          (200000)    // Hz
#define DEFAULT_TX_FREQ_500_START_LA_915    (915900000) // Hz
#define DEFAULT_TX_STEP_500_LA_915          (1600000)   // Hz
#define DEFAULT_RX_FREQ_500_START_LA_915    (923300000) // Hz
#define DEFAULT_RX_STEP_500_LA_915          (600000)    // Hz

// Class B
#define BEACON_DR_LA_915                    (8)
#define BEACON_FREQ_START_LA_915            (923300000) // Hz
#define BEACON_STEP_LA_915                  (600000)    // Hz
#define PING_SLOT_FREQ_START_LA_915         (923300000) // Hz
#define PING_SLOT_STEP_LA_915               (600000)    // Hz

/* clang-format on */

/*
 * -----------------------------------------------------------------------------
 * --- PUBLIC TYPES ------------------------------------------------------------
 */

/**
 * Bank contains 8 channels
 */
typedef enum la_915_channels_bank_e
{
    BANK_0_125_LA915 = 0,  // 125KHZ  0 -  7
    BANK_1_125_LA915 = 1,  // 125KHZ  8 - 15
    BANK_2_125_LA915 = 2,  // 125KHZ 16 - 23
    BANK_3_125_LA915 = 3,  // 125KHZ 24 - 31
    BANK_4_125_LA915 = 4,  // 125KHZ 32 - 39
    BANK_5_125_LA915 = 5,  // 125KHZ 40 - 47
    BANK_6_125_LA915 = 6,  // 125KHZ 48 - 55
    BANK_7_125_LA915 = 7,  // 125KHZ 56 - 63
    BANK_8_500_LA915 = 8,  // 500KHz 64 - 72
    BANK_MAX_LA915
} la_915_channels_bank_t;

typedef struct region_la915_context_s
{
    uint16_t dr_bitfield_tx_channel[NUMBER_OF_TX_CHANNEL_LA_915];
    uint8_t  channel_index_enabled[BANK_MAX_LA915];     // 8ch-125KHz + 1ch-500KHZ // Enable by Network
    uint8_t  unwrapped_channel_mask[BANK_MAX_LA915];    // 8ch-125KHz + 1ch-500KHZ // Temp conf send by Network
    uint8_t  snapshot_channel_tx_mask[BANK_MAX_LA915];  // 8ch-125KHz + 1ch-500KHZ // snapshot of used channels
    uint8_t  dr_distribution_init[NUMBER_OF_TX_DR_LA_915];
    uint8_t  dr_distribution[NUMBER_OF_TX_DR_LA_915];
    uint8_t  join_dr_distribution[NUMBER_OF_TX_DR_LA_915];
    uint8_t  custom_dr_distribution_init[NUMBER_OF_TX_DR_LA_915];
    uint8_t  first_ch_mask_received;
    uint8_t  tx_channel_idx;

    la_915_channels_bank_t snapshot_bank_tx_mask;

} region_la915_context_t;

/*
 * -----------------------------------------------------------------------------
 * --- PUBLIC CONSTANTS --------------------------------------------------------
 */

/**
 * Default enabled channels, one byte per bank (see DEFAULT_CH_MASK_*_LA_915)
 */
static const uint8_t default_channel_mask_la_915[BANK_MAX_LA915] = {
    DEFAULT_CH_MASK_125_BANK0_LA_915, DEFAULT_CH_MASK_125_OTHER_LA_915, DEFAULT_CH_MASK_125_OTHER_LA_915,
    DEFAULT_CH_MASK_125_OTHER_LA_915, DEFAULT_CH_MASK_125_OTHER_LA_915, DEFAULT_CH_MASK_125_OTHER_LA_915,
    DEFAULT_CH_MASK_125_OTHER_LA_915, DEFAULT_CH_MASK_125_OTHER_LA_915, DEFAULT_CH_MASK_500_LA_915 };

static const uint8_t SYNC_WORD_LR_FHSS_LA_915[] = { 0x2C, 0x0F, 0x79, 0x95 };

/**
 * Up/Down link data rates offset definition
 */
static const uint8_t datarate_offsets_la_915[8][6] = {
    { 8, 8, 8, 8, 8, 8 },       // DR 0
    { 9, 8, 8, 8, 8, 8 },       // DR 1
    { 10, 9, 8, 8, 8, 8 },      // DR 2
    { 11, 10, 9, 8, 8, 8 },     // DR 3
    { 12, 11, 10, 9, 8, 8 },    // DR 4
    { 13, 12, 11, 10, 9, 8 },   // DR 5
    { 13, 13, 12, 11, 10, 9 },  // DR 6
    { 9, 8, 8, 8, 8, 8 },       // DR 7
};

/**
 * @brief uplink datarate backoff
 *
 */
static const uint8_t datarate_backoff_la_915[2][8] = { {
                                                           // [0][dr_backoff] dwell time Off
                                                           0,  // DR0 -> DR0
                                                           0,  // DR1 -> DR0
                                                           1,  // DR2 -> DR1
                                                           2,  // DR3 -> DR2
                                                           3,  // DR4 -> DR3
                                                           4,  // DR5 -> DR4
                                                           5,  // DR6 -> DR5
                                                           0   // DR7 -> DR0
                                                       },
                                                       {
                                                           // [1][dr_backoff] dwell time On
                                                           2,  // NA
                                                           2,  // NA
                                                           2,  // DR2 -> DR2
                                                           2,  // DR3 -> DR2
                                                           3,  // DR4 -> DR3
                                                           4,  // DR5 -> DR4
                                                           5,  // DR6 -> DR5
                                                           2   // DR7 -> DR2
                                                       } };

static const uint8_t NUMBER_RX1_DR_OFFSET_LA_915 =
    sizeof( datarate_offsets_la_915[0] ) / sizeof( datarate_offsets_la_915[0][0] );

/**
 * Data rates table definition
 */
static const uint8_t datarates_to_sf_la_915[] = { 12, 11, 10, 9, 8, 7, 8, 0, 12, 11, 10, 9, 8, 7, 0, 0 };

/**
 * Bandwidths table definition in KHz
 */
static const uint32_t datarates_to_bandwidths_la_915[] = { BW125, BW125, BW125, BW125, BW125, BW125, BW500,  BW_RFU,
                                                           BW500, BW500, BW500, BW500, BW500, BW500, BW_RFU, BW_RFU };

/**
 * Payload max size table definition in bytes
 */
static const uint8_t M_la_915[2][16] = {
    { 59, 59, 59, 123, 250, 250, 250, 58, 61, 137, 250, 250, 250, 250, 0, 0 },  // [0][dr] dwell time Off
    { 0, 0, 19, 61, 133, 250, 250, 58, 61, 137, 250, 250, 250, 250, 0, 0 }      // [1][dr] dwell time On
};

/**
 * Mobile long range datarate distribution
 * DR0: 20%,
 * DR1: 20%,
 * DR2: 30%,
 * DR3: 30%,
 * DR4:  0%,
 * DR5:  0%,
 * DR6:  0%
 */
static const uint8_t MOBILE_LONGRANGE_DR_DISTRIBUTION_LA_915[] = { 2, 2, 3, 3, 0, 0, 0, 0 };

/**
 * Mobile low power datarate distribution
 * DR0:  0%,
 * DR1:  0%,
 * DR2: 10%,
 * DR3: 30%,
 * DR4: 30%,
 * DR5: 30%,
 * DR6:  0%
 */
static const uint8_t MOBILE_LOWPER_DR_DISTRIBUTION_LA_915[] = { 0, 0, 1, 3, 3, 3, 0, 0 };

/**
 * !! NOT USED IN LA915 !!
 *
 * Join datarate distribution
 * DR0:  0%,
 * DR1:  0%,
 * DR2: 50%,
 * DR3:  0%,
 * DR4:  0%,
 * DR5:  0%,
 * DR6: 50%
 */
static const uint8_t JOIN_DR_DISTRIBUTION_LA_915[] = { 0, 0, 5, 0, 0, 0, 5, 0 };

/**
 * Default datarate distribution
 * DR0:   0%,
 * DR1:   0%,
 * DR2: 100%,
 * DR3:   0%,
 * DR4:   0%,
 * DR5:   0%,
 * DR6:   0%
 */
static const uint8_t DEFAULT_DR_DISTRIBUTION_LA_915[] = { 0, 0, 1, 0, 0, 0, 0, 0 };

/*
 * -----------------------------------------------------------------------------
 * --- PUBLIC FUNCTIONS PROTOTYPES ---------------------------------------------
 */

#ifdef __cplusplus
}
#endif

#endif  // REGION_LA915_DEFS_H

/* --- EOF ------------------------------------------------------------------ */
