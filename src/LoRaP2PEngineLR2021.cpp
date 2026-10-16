/**
 * @file LoRaP2PEngineLR2021.cpp
 * @brief LoRa/FSK P2P engine for the Semtech LR2021 (RAK13702 on a RAK3401)
 *
 * LoRaP2PEngine.cpp talks to the SX126x driver directly. This file holds the LR2021 twin of every radio
 * method of that class (the lr*() members declared in LoRaP2PEngine.h). The SX126x methods only got a
 * short branch at the top that calls the twin when the board has an LR2021, see WISBLOCK_ENABLE_LR2021 in
 * wb_lbm_config.h; without that flag this file is empty and the SX126x code is exactly as before.
 *
 * The twins use LBM's RAL (the same radio API LoRaWAN mode uses) instead of the raw driver. That way the
 * RF switch, TCXO, PA and DIO set-up of wisblock_ral_lr20xx_bsp.c is used in both modes.
 *
 * Differences to the SX126x engine:
 *  - rxBoostedGainEnabled has no effect (the RAL has no RX boost function for the LR20xx)
 *  - the SX126x GFSK bit rate workarounds do not apply
 *  - the TX power is limited to the range of the PA that is used for the frequency, see the BSP
 *  - the LoRa sync word is the 8 bit form (0x12 / 0x34), derived from the 16 bit settings.syncWord
 *
 * NOT TESTED ON HARDWARE yet.
 */

/* WisBlockLoRaWAN: build flags for the Arduino IDE, see wb_lbm_config.h */
#include "wb_lbm_config.h"

#if defined(WISBLOCK_ENABLE_LR2021)

#include "LoRaP2PEngine.h"

#include "lbm/smtc_modem_core/radio_drivers/lr20xx_driver/src/lr20xx_radio_fsk.h" // vendored: src/lbm/smtc_modem_core/radio_drivers/lr20xx_driver/src/lr20xx_radio_fsk.h
#include "lbm/smtc_modem_core/radio_drivers/lr20xx_driver/src/lr20xx_system.h"	  // vendored: src/lbm/smtc_modem_core/radio_drivers/lr20xx_driver/src/lr20xx_system.h
#include "lbm/smtc_modem_core/smtc_ral/src/ral.h"								  // vendored: src/lbm/smtc_modem_core/smtc_ral/src/ral.h
#include "wisblock_radio_bsp_config.h"
#include "wisblock_radio_hal.h"

// RAL of the LR2021, defined in wisblock_ral_lr20xx_bsp.c
extern "C" const ral_t *wisblock_lr20xx_get_ral(void);

namespace
{
// The LR20xx HAL (wisblock_radio_hal.cpp) ignores the context, see the note there
constexpr void *kCtx = nullptr;

// Longest RX timeout the RAL accepts, ral_lr20xx_set_rx()
constexpr uint32_t kMaxRxTimeoutMs = 511999;

const ral_t *radio()
{
	return wisblock_lr20xx_get_ral();
}

uint32_t clampValue(uint32_t v, uint32_t lo, uint32_t hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

ral_lora_bw_t toRalBandwidth(WisBlockP2PBandwidth bw)
{
	switch (bw)
	{
	case WISBLOCK_BW_125:
		return RAL_LORA_BW_125_KHZ;
	case WISBLOCK_BW_250:
		return RAL_LORA_BW_250_KHZ;
	case WISBLOCK_BW_500:
		return RAL_LORA_BW_500_KHZ;
	case WISBLOCK_BW_062:
		return RAL_LORA_BW_062_KHZ;
	case WISBLOCK_BW_041:
		return RAL_LORA_BW_041_KHZ;
	case WISBLOCK_BW_031:
		return RAL_LORA_BW_031_KHZ;
	case WISBLOCK_BW_020:
		return RAL_LORA_BW_020_KHZ;
	case WISBLOCK_BW_015:
		return RAL_LORA_BW_015_KHZ;
	case WISBLOCK_BW_010:
		return RAL_LORA_BW_010_KHZ;
	case WISBLOCK_BW_007:
	default:
		return RAL_LORA_BW_007_KHZ;
	}
}

/**
 * @brief 8 bit LoRa sync word (0x12 private, 0x34 public) from the 16 bit register form
 *
 * 0x1424 -> 0x12, 0x3444 -> 0x34: the high nibbles of both bytes. Other values lose their low nibbles.
 */
uint8_t toShortSyncWord(uint16_t syncWord)
{
	return (uint8_t)(((syncWord >> 8) & 0xF0) | ((syncWord >> 4) & 0x0F));
}

ral_lora_mod_params_t buildLoraModParams(const WisBlockP2PSettings &s)
{
	ral_lora_mod_params_t p;
	p.sf = (ral_lora_sf_t)s.spreadingFactor; // RAL_LORA_SF5..12 are the numbers 5..12
	p.bw = toRalBandwidth(s.bandwidth);
	p.cr = (ral_lora_cr_t)s.codingRate; // WisBlockP2PCodingRate values 1..4 are RAL_LORA_CR_4_5..4_8
	p.ldro = ral_compute_lora_ldro(p.sf, p.bw);
	return p;
}

ral_lora_pkt_params_t buildLoraPktParams(const WisBlockP2PSettings &s, uint8_t payloadLen)
{
	ral_lora_pkt_params_t p;
	p.preamble_len_in_symb = s.preambleLength;
	p.header_type = RAL_LORA_PKT_EXPLICIT;
	p.pld_len_in_bytes = payloadLen;
	p.crc_is_on = true;
	p.invert_iq_is_on = s.iqInversion;
	return p;
}

ral_gfsk_mod_params_t buildGfskModParams(const WisBlockP2PSettings &s)
{
	ral_gfsk_mod_params_t p;
	p.br_in_bps = clampValue(s.fskBitrate, WISBLOCK_FSK_BITRATE_MIN, WISBLOCK_FSK_BITRATE_MAX);
	p.fdev_in_hz = clampValue(s.fskFreqDevHz, WISBLOCK_FSK_FDEV_MIN, WISBLOCK_FSK_FDEV_MAX);
	p.bw_dsb_in_hz = s.fskBandwidthHz;
	p.pulse_shape = RAL_GFSK_PULSE_SHAPE_BT_1;
	return p;
}

// Variable length packets, 2 byte inverted CCITT CRC, whitening, no address filtering. preambleLength is in bytes.
ral_gfsk_pkt_params_t buildGfskPktParams(const WisBlockP2PSettings &s, uint8_t payloadLen)
{
	ral_gfsk_pkt_params_t p;
	uint32_t bits = clampValue(s.preambleLength, 1, 8191) * 8;
	p.preamble_len_in_bits = (uint16_t)bits;
	p.preamble_detector = bits >= 16 ? RAL_GFSK_PREAMBLE_DETECTOR_MIN_16BITS : RAL_GFSK_PREAMBLE_DETECTOR_MIN_8BITS;
	uint8_t swLen = s.fskSyncWordLen;
	if (swLen < 1 || swLen > WISBLOCK_FSK_SYNCWORD_MAX_LEN)
	{
		swLen = 3;
	}
	p.sync_word_len_in_bits = swLen * 8;
	p.address_filtering = RAL_GFSK_ADDRESS_FILTERING_DISABLE;
	p.header_type = RAL_GFSK_PKT_VAR_LEN;
	p.pld_len_in_bytes = payloadLen;
	p.crc_type = RAL_GFSK_CRC_2_BYTES_INV;
	p.dc_free = RAL_GFSK_DC_FREE_WHITENING;
	return p;
}

constexpr ral_irq_t kIrqsForTx = (ral_irq_t)(RAL_IRQ_TX_DONE | RAL_IRQ_RX_TIMEOUT);
constexpr ral_irq_t kIrqsForRx = (ral_irq_t)(RAL_IRQ_RX_DONE | RAL_IRQ_RX_TIMEOUT | RAL_IRQ_RX_CRC_ERROR);
constexpr ral_irq_t kIrqsForCad = (ral_irq_t)(RAL_IRQ_CAD_DONE | RAL_IRQ_CAD_OK);
} // namespace

void LoRaP2PEngine::lrBegin()
{
	const ral_t *r = radio();
	ral_reset(r);
	ral_init(r); // regulator, TCXO, 32 kHz clock, DIO functions, RF switch and front end calibration, see the BSP
	ral_set_standby(r, RAL_STANDBY_CFG_RC);
	ral_set_pkt_type(r, isFsk() ? RAL_PKT_TYPE_GFSK : RAL_PKT_TYPE_LORA);
	lrApplyRadioParams();
}

void LoRaP2PEngine::lrApplySettings(bool modulationChanged)
{
	const ral_t *r = radio();
	if (modulationChanged)
	{
		// The packet type can only be changed in standby
		ral_set_standby(r, RAL_STANDBY_CFG_RC);
		ral_set_pkt_type(r, isFsk() ? RAL_PKT_TYPE_GFSK : RAL_PKT_TYPE_LORA);
	}
	lrApplyRadioParams();
}

void LoRaP2PEngine::lrApplyRadioParams()
{
	const ral_t *r = radio();
	ral_set_rf_freq(r, settings.frequencyHz);

	if (isFsk())
	{
		lrApplyFskParams();
	}
	else
	{
		ral_lora_mod_params_t mod = buildLoraModParams(settings);
		ral_set_lora_mod_params(r, &mod);
		lrApplyPktParams(255); // max; overridden per TX in lrSend(), irrelevant for RX (explicit header)
		ral_set_lora_sync_word(r, toShortSyncWord(settings.syncWord));
	}

	// PA selection, power and ramp time. The BSP picks the LF or HF PA from the frequency and limits the
	// power to its range. Has to come after the frequency is set.
	ral_set_tx_cfg(r, settings.txPowerDbm, settings.frequencyHz);
}

void LoRaP2PEngine::lrApplyFskParams()
{
	const ral_t *r = radio();
	ral_gfsk_mod_params_t mod = buildGfskModParams(settings);
	ral_set_gfsk_mod_params(r, &mod);
	lrApplyPktParams(255);

	uint8_t swLen = settings.fskSyncWordLen;
	if (swLen < 1 || swLen > WISBLOCK_FSK_SYNCWORD_MAX_LEN)
	{
		swLen = 3;
	}
	ral_set_gfsk_sync_word(r, settings.fskSyncWord, swLen);
	ral_set_gfsk_crc_params(r, 0x1D0F, 0x1021); // CRC_2_BYTES_INV: inverted CCITT
	ral_set_gfsk_whitening_seed(r, 0x01FF);
}

void LoRaP2PEngine::lrApplyPktParams(uint8_t payloadLen)
{
	const ral_t *r = radio();
	if (isFsk())
	{
		ral_gfsk_pkt_params_t pkt = buildGfskPktParams(settings, payloadLen);
		ral_set_gfsk_pkt_params(r, &pkt);
		return;
	}
	ral_lora_pkt_params_t pkt = buildLoraPktParams(settings, payloadLen);
	ral_set_lora_pkt_params(r, &pkt);
}

bool LoRaP2PEngine::lrSend(const uint8_t *data, uint8_t length)
{
	const ral_t *r = radio();
	lrReconfigureAfterColdSleep();

	// See send(): this always sends immediately, a CAD first is up to the caller (startCad())
	ral_set_standby(r, RAL_STANDBY_CFG_RC);
	if (rxMode != WISBLOCK_P2P_RX_CONTINUOUS_TX)
	{
		rxMode = 0; // the send ends a running receive window, only mode 65533 continues after the TX
	}

	lrApplyPktParams(length);

	ral_set_pkt_payload(r, data, length);
	lastTxPayloadLength = length;

	ral_set_dio_irq_params(r, kIrqsForTx);
	ral_clear_irq_status(r, RAL_IRQ_ALL);

	// No timeout: the chip stays in TX until the packet is sent, then returns to standby
	ral_set_tx(r);
	return true;
}

void LoRaP2PEngine::lrStartRadioReceive(uint32_t mode)
{
	const ral_t *r = radio();
	lrReconfigureAfterColdSleep();
	ral_set_standby(r, RAL_STANDBY_CFG_RC);

	lrApplyPktParams(255);

	ral_set_dio_irq_params(r, kIrqsForRx);
	ral_clear_irq_status(r, RAL_IRQ_ALL);

	if (mode == WISBLOCK_P2P_RX_CONTINUOUS_TX || mode == WISBLOCK_P2P_RX_CONTINUOUS)
	{
		ral_set_rx(r, RAL_RX_TIMEOUT_CONTINUOUS_MODE);
	}
	else if (mode == WISBLOCK_P2P_RX_SINGLE)
	{
		// No timeout, the radio goes to standby after the first packet (timeout 0 in the RAL)
		ral_set_rx(r, 0);
	}
	else
	{
		ral_set_rx(r, mode > kMaxRxTimeoutMs ? kMaxRxTimeoutMs : mode);
	}
}

void LoRaP2PEngine::lrStartReceiveDutyCycle(uint32_t rxTimeMs, uint32_t sleepTimeMs)
{
	const ral_t *r = radio();
	rxMode = 0; // not one of the startReceive() modes, send() and CAD interrupt it
	lrReconfigureAfterColdSleep();
	ral_set_standby(r, RAL_STANDBY_CFG_RC);

	lrApplyPktParams(255);

	// Same IRQ routing as a normal receive. The sleep phases of the duty cycle are the chip's own, the HAL
	// does not see them (same as for the SX126x), so the antenna switch power stays on.
	ral_set_dio_irq_params(r, kIrqsForRx);
	ral_clear_irq_status(r, RAL_IRQ_ALL);
	ral_set_rx_duty_cycle(r, rxTimeMs, sleepTimeMs);
}

void LoRaP2PEngine::lrStopReceive()
{
	rxMode = 0;
	cadResumeRx = false;
	ral_set_standby(radio(), RAL_STANDBY_CFG_RC);
}

void LoRaP2PEngine::lrSleep()
{
	// Cold start: the configuration is lost and applied again by lrReconfigureAfterColdSleep()
	ral_set_sleep(radio(), false);
	needsReconfigureAfterSleep = true;
	rxMode = 0;
	cadResumeRx = false;
}

void LoRaP2PEngine::lrReconfigureAfterColdSleep()
{
	if (!needsReconfigureAfterSleep)
	{
		return;
	}
	needsReconfigureAfterSleep = false;

	// Everything lrBegin() does except the reset: the wake-up sequence of the HAL has already brought
	// the chip back. ral_init() sets regulator, TCXO, clock, DIOs and calibration again.
	const ral_t *r = radio();
	ral_init(r);
	ral_set_standby(r, RAL_STANDBY_CFG_RC);
	ral_set_pkt_type(r, isFsk() ? RAL_PKT_TYPE_GFSK : RAL_PKT_TYPE_LORA);
	lrApplyRadioParams();
}

bool LoRaP2PEngine::lrGetInstantRssi(int16_t &rssiDbm)
{
	return ral_get_rssi_inst(radio(), &rssiDbm) == RAL_STATUS_OK;
}

bool LoRaP2PEngine::lrGetRadioStatus(uint8_t &chipMode, uint8_t &cmdStatus, uint16_t &deviceErrors)
{
	lr20xx_system_stat1_t stat1;
	lr20xx_system_stat2_t stat2;
	lr20xx_system_irq_mask_t irq = 0;
	lr20xx_system_errors_t errors = 0;
	if (lr20xx_system_get_status(kCtx, &stat1, &stat2, &irq) != LR20XX_STATUS_OK ||
		lr20xx_system_get_errors(kCtx, &errors) != LR20XX_STATUS_OK)
	{
		return false;
	}
	chipMode = (uint8_t)stat2.chip_mode;
	cmdStatus = (uint8_t)stat1.command_status;
	deviceErrors = errors;
	return true;
}

bool LoRaP2PEngine::lrGetFskStats(uint16_t &received, uint16_t &crcErrors, uint16_t &lengthErrors)
{
	lr20xx_radio_fsk_rx_statistics_t stats;
	if (lr20xx_radio_fsk_get_rx_statistics(kCtx, &stats) != LR20XX_STATUS_OK)
	{
		return false;
	}
	received = stats.n_received_packets;
	crcErrors = stats.n_crc_errors;
	lengthErrors = stats.n_length_errors;
	return true;
}

void LoRaP2PEngine::lrStartCad()
{
	const ral_t *r = radio();
	lrReconfigureAfterColdSleep();
	// CAD is only accepted from standby, a running receive does not end by itself
	ral_set_standby(r, RAL_STANDBY_CFG_RC);
	// A continuous receive is started again when the CAD is done, any other receive ends here
	cadResumeRx = (rxMode == WISBLOCK_P2P_RX_CONTINUOUS_TX || rxMode == WISBLOCK_P2P_RX_CONTINUOUS);
	if (!cadResumeRx)
	{
		rxMode = 0;
	}

	ral_lora_cad_params_t cad;
	const ral_lora_mod_params_t mod = buildLoraModParams(settings);
	cad.cad_symb_nb = RAL_LORA_CAD_04_SYMB;
	// Detection peak for the spreading factor from the RAL (and the BSP), 22 is the SX126x default for SF7-SF10
	uint8_t peak = 22;
	if (ral_get_lora_cad_det_peak(r, mod.sf, mod.bw, cad.cad_symb_nb, &peak) != RAL_STATUS_OK)
	{
		peak = 22;
	}
	cad.cad_det_peak_in_symb = peak;
	cad.cad_det_min_in_symb = 10; // not used by the LR20xx
	cad.cad_exit_mode = RAL_LORA_CAD_ONLY;
	cad.cad_timeout_in_ms = 0;
	ral_set_lora_cad_params(r, &cad);

	ral_set_dio_irq_params(r, kIrqsForCad);
	ral_clear_irq_status(r, RAL_IRQ_ALL);

	ral_set_lora_cad(r);
}

uint32_t LoRaP2PEngine::lrHandleEvents()
{
	// See handleEvents(): purely IRQ driven, no deadline of its own
	constexpr uint32_t kNoScheduledDeadlineMs = 60000;

	// A sleeping radio has nothing to report, and reading the IRQ status would wake it up (see the long
	// comment in handleEvents())
	if (WisBlockRadioHal::isAsleep())
	{
		return kNoScheduledDeadlineMs;
	}

	const ral_t *r = radio();
	ral_irq_t irq = RAL_IRQ_NONE;
	if (ral_get_and_clear_irq_status(r, &irq) != RAL_STATUS_OK || irq == RAL_IRQ_NONE)
	{
		return kNoScheduledDeadlineMs; // nothing pending, the common case
	}

	if (irq & RAL_IRQ_TX_DONE)
	{
		WisBlockTxResult res;
		res.success = true;
		if (isFsk())
		{
			ral_gfsk_pkt_params_t pkt = buildGfskPktParams(settings, lastTxPayloadLength);
			ral_gfsk_mod_params_t mod = buildGfskModParams(settings);
			res.airtimeMs = ral_get_gfsk_time_on_air_in_ms(r, &pkt, &mod);
		}
		else
		{
			ral_lora_pkt_params_t pkt = buildLoraPktParams(settings, lastTxPayloadLength);
			ral_lora_mod_params_t mod = buildLoraModParams(settings);
			res.airtimeMs = ral_get_lora_time_on_air_in_ms(r, &pkt, &mod);
		}
		// Mode 65533: back to receive before the callback, so a send() in the callback works as well
		if (rxMode == WISBLOCK_P2P_RX_CONTINUOUS_TX)
		{
			lrStartRadioReceive(rxMode);
		}
		if (txFinishedCb)
		{
			txFinishedCb(res);
		}
	}

	if (irq & RAL_IRQ_RX_DONE)
	{
		WisBlockRxResult res;
		// FSK: the radio reports RX_DONE together with CRC_ERROR for a damaged packet. It is reported like
		// an RX timeout (no data), so the application can start the RX again.
		bool fskCrcError = isFsk() && (irq & RAL_IRQ_RX_CRC_ERROR);

		if (!fskCrcError)
		{
			uint16_t len = 0;
			if (ral_get_pkt_payload(r, sizeof(res.data), res.data, &len) == RAL_STATUS_OK)
			{
				res.length = (uint8_t)(len > sizeof(res.data) ? sizeof(res.data) : len);
			}
			if (isFsk())
			{
				ral_gfsk_rx_pkt_status_t fskStatus;
				if (ral_get_gfsk_rx_pkt_status(r, &fskStatus) == RAL_STATUS_OK)
				{
					res.rssi = fskStatus.rssi_avg_in_dbm; // dBm, there is no SNR in FSK
					res.snr = 0;
				}
			}
			else
			{
				ral_lora_rx_pkt_status_t loraStatus;
				if (ral_get_lora_rx_pkt_status(r, &loraStatus) == RAL_STATUS_OK)
				{
					res.rssi = loraStatus.rssi_pkt_in_dbm;
					res.snr = (int8_t)loraStatus.snr_pkt_in_db;
				}
			}
		}
		// A receive window or a single receive is finished, the radio is in standby
		if (rxMode != WISBLOCK_P2P_RX_CONTINUOUS_TX && rxMode != WISBLOCK_P2P_RX_CONTINUOUS)
		{
			rxMode = 0;
		}
		if (rxFinishedCb)
		{
			rxFinishedCb(res);
		}
	}

	if (irq & RAL_IRQ_RX_TIMEOUT)
	{
		if (rxMode != WISBLOCK_P2P_RX_CONTINUOUS_TX && rxMode != WISBLOCK_P2P_RX_CONTINUOUS)
		{
			rxMode = 0; // the receive window ended
		}
		// Reported as an empty RX result, same as the SX126x engine
		if (rxFinishedCb)
		{
			WisBlockRxResult res;
			rxFinishedCb(res);
		}
	}

	if (irq & RAL_IRQ_CAD_DONE)
	{
		bool detected = (irq & RAL_IRQ_CAD_OK) != 0;
		if (cadResumeRx)
		{
			// Back to the continuous receive before the callback, a send() in the callback works as well
			cadResumeRx = false;
			lrStartRadioReceive(rxMode);
		}
		if (cadResultCb)
		{
			cadResultCb(detected ? WISBLOCK_CAD_CHANNEL_DETECTED : WISBLOCK_CAD_CHANNEL_CLEAR);
		}
	}

	return kNoScheduledDeadlineMs;
}

#endif // WISBLOCK_ENABLE_LR2021
