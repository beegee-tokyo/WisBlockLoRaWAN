/* WisBlockLoRaWAN: build flags for the Arduino IDE, see wb_lbm_config.h */
#include "wb_lbm_config.h"
#include "LoRaP2PEngine.h"

#include <math.h>

#include "lbm/smtc_modem_core/radio_drivers/sx126x_driver/src/sx126x.h" // vendored: src/lbm/smtc_modem_core/radio_drivers/sx126x_driver/src/sx126x.h
#include "wisblock_radio_bsp_config.h"
#include "wisblock_radio_hal.h"

/*
 * NOTE on `context`: every sx126x_* call below passes nullptr for context.
 * This matches wisblock_radio_hal_*.cpp, which ignores the context pointer
 * entirely and operates on a single static board context instead (see the
 * NOTE in those files) - so nullptr here is intentional and correct, not a
 * shortcut.
 */
namespace
{
constexpr void *kCtx = nullptr;

/**
 * @brief Convert a library bandwidth to the SX126x driver value
 *
 * @param bw Library bandwidth
 * @return SX126x driver bandwidth
 */
sx126x_lora_bw_t toSx126xBandwidth(WisBlockP2PBandwidth bw)
{
	switch (bw)
	{
	case WISBLOCK_BW_125:
		return SX126X_LORA_BW_125;
	case WISBLOCK_BW_250:
		return SX126X_LORA_BW_250;
	case WISBLOCK_BW_500:
		return SX126X_LORA_BW_500;
	case WISBLOCK_BW_062:
		return SX126X_LORA_BW_062;
	case WISBLOCK_BW_041:
		return SX126X_LORA_BW_041;
	case WISBLOCK_BW_031:
		return SX126X_LORA_BW_031;
	case WISBLOCK_BW_020:
		return SX126X_LORA_BW_020;
	case WISBLOCK_BW_015:
		return SX126X_LORA_BW_015;
	case WISBLOCK_BW_010:
		return SX126X_LORA_BW_010;
	case WISBLOCK_BW_007:
	default:
		return SX126X_LORA_BW_007;
	}
}

/**
 * @brief Convert a library bandwidth to Hz
 *
 * @param bw Library bandwidth
 * @return Bandwidth in Hz
 */
uint32_t bandwidthToHz(WisBlockP2PBandwidth bw)
{
	switch (bw)
	{
	case WISBLOCK_BW_125:
		return 125000;
	case WISBLOCK_BW_250:
		return 250000;
	case WISBLOCK_BW_500:
		return 500000;
	case WISBLOCK_BW_062:
		return 62500;
	case WISBLOCK_BW_041:
		return 41670;
	case WISBLOCK_BW_031:
		return 31250;
	case WISBLOCK_BW_020:
		return 20830;
	case WISBLOCK_BW_015:
		return 15630;
	case WISBLOCK_BW_010:
		return 10420;
	case WISBLOCK_BW_007:
	default:
		return 7810;
	}
}

/**
 * @brief Decide if the low data rate optimization is needed (symbol time above 16.38 ms)
 *
 * Semtech AN1200.13: enable Low Data Rate Optimization whenever the symbol
 * period exceeds 16.38ms - matters most at SF11/SF12 on narrow bandwidths.
 *
 * @param sf Spreading factor
 * @param bw Library bandwidth
 * @return 1 if the optimization must be on, otherwise 0
 */
uint8_t computeLdro(uint8_t sf, WisBlockP2PBandwidth bw)
{
	double symbolPeriodMs = (1u << sf) * 1000.0 / (double)bandwidthToHz(bw);
	return symbolPeriodMs > 16.38 ? 1 : 0;
}

// ---- GFSK helpers -------------------------------------------------------------------------

struct GfskBwEntry
{
	uint32_t hz;
	sx126x_gfsk_bw_t value;
};
// RX bandwidth steps of the SX126x (double side band), ascending
const GfskBwEntry kGfskBw[] = {
	{4800, SX126X_GFSK_BW_4800},	 {5800, SX126X_GFSK_BW_5800},	  {7300, SX126X_GFSK_BW_7300},
	{9700, SX126X_GFSK_BW_9700},	 {11700, SX126X_GFSK_BW_11700},	  {14600, SX126X_GFSK_BW_14600},
	{19500, SX126X_GFSK_BW_19500},	 {23400, SX126X_GFSK_BW_23400},	  {29300, SX126X_GFSK_BW_29300},
	{39000, SX126X_GFSK_BW_39000},	 {46900, SX126X_GFSK_BW_46900},	  {58600, SX126X_GFSK_BW_58600},
	{78200, SX126X_GFSK_BW_78200},	 {93800, SX126X_GFSK_BW_93800},	  {117300, SX126X_GFSK_BW_117300},
	{156200, SX126X_GFSK_BW_156200}, {187200, SX126X_GFSK_BW_187200}, {234300, SX126X_GFSK_BW_234300},
	{312000, SX126X_GFSK_BW_312000}, {373600, SX126X_GFSK_BW_373600}, {467000, SX126X_GFSK_BW_467000},
};

/**
 * @brief Smallest GFSK RX bandwidth step that is at least the requested bandwidth
 *
 * @param hz Requested bandwidth in Hz
 * @return Driver value, the widest step if hz is above all of them
 */
sx126x_gfsk_bw_t toSx126xGfskBandwidth(uint32_t hz)
{
	for (const GfskBwEntry &e : kGfskBw)
	{
		if (e.hz >= hz)
		{
			return e.value;
		}
	}
	return SX126X_GFSK_BW_467000;
}

uint32_t clampU32(uint32_t v, uint32_t lo, uint32_t hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

/**
 * @brief GFSK modulation parameters from the P2P settings
 *
 * @param s P2P settings
 * @return Driver parameters (bit rate and deviation limited to what the modem supports)
 */
sx126x_mod_params_gfsk_t buildGfskModParams(const WisBlockP2PSettings &s)
{
	sx126x_mod_params_gfsk_t p;
	p.br_in_bps = clampU32(s.fskBitrate, WISBLOCK_FSK_BITRATE_MIN, WISBLOCK_FSK_BITRATE_MAX);
	p.fdev_in_hz = clampU32(s.fskFreqDevHz, WISBLOCK_FSK_FDEV_MIN, WISBLOCK_FSK_FDEV_MAX);
	p.pulse_shape = SX126X_GFSK_PULSE_SHAPE_BT_1;
	p.bw_dsb_param = toSx126xGfskBandwidth(s.fskBandwidthHz);
	return p;
}

/**
 * @brief GFSK packet parameters from the P2P settings
 *
 * Variable length packets (the length is sent in the packet), 2 byte inverted CCITT CRC, whitening,
 * no address filtering. preambleLength is in bytes.
 *
 * @param s P2P settings
 * @param payloadLen Payload length for TX, 255 (maximum) for RX
 * @return Driver parameters
 */
sx126x_pkt_params_gfsk_t buildGfskPktParams(const WisBlockP2PSettings &s, uint8_t payloadLen)
{
	sx126x_pkt_params_gfsk_t p;
	uint32_t bits = clampU32(s.preambleLength, 1, 8191) * 8;
	p.preamble_len_in_bits = (uint16_t)bits;
	p.preamble_detector = bits >= 16 ? SX126X_GFSK_PREAMBLE_DETECTOR_MIN_16BITS : SX126X_GFSK_PREAMBLE_DETECTOR_MIN_8BITS;
	uint8_t swLen = s.fskSyncWordLen;
	if (swLen < 1 || swLen > WISBLOCK_FSK_SYNCWORD_MAX_LEN)
	{
		swLen = 3;
	}
	p.sync_word_len_in_bits = swLen * 8;
	p.address_filtering = SX126X_GFSK_ADDRESS_FILTERING_DISABLE;
	p.header_type = SX126X_GFSK_PKT_VAR_LEN;
	p.pld_len_in_bytes = payloadLen;
	p.crc_type = SX126X_GFSK_CRC_2_BYTES_INV;
	p.dc_free = SX126X_GFSK_DC_FREE_WHITENING;
	return p;
}

constexpr uint16_t kAllIrqsForTx = SX126X_IRQ_TX_DONE | SX126X_IRQ_TIMEOUT;
constexpr uint16_t kAllIrqsForRx = SX126X_IRQ_RX_DONE | SX126X_IRQ_TIMEOUT | SX126X_IRQ_CRC_ERROR;
constexpr uint16_t kAllIrqsForCad = SX126X_IRQ_CAD_DONE | SX126X_IRQ_CAD_DETECTED;
/**
 * @brief Calculate the time on air of a packet (Semtech AN1200.13), explicit header and CRC on
 *
 * Semtech AN1200.13 "LoRa Modem Designer's Guide", ยง4 Time on air:
 *
 *   Tsym = 2^SF / BW                                                  (seconds)
 *   Tpreamble = (preambleLen + 4.25) * Tsym
 *   payloadSymbNb = 8 + max(ceil((8*PL - 4*SF + 28 + 16*CRC - 20*H)
 *                                  / (4*(SF - 2*DE))) * (CR + 4), 0)
 *   Tpayload = payloadSymbNb * Tsym
 *   Ttotal = Tpreamble + Tpayload
 *
 * where PL = payload length in bytes, CRC = 1 if CRC enabled, H = 0 for
 * explicit header (1 for implicit), DE = 1 if LDRO is on, CR = coding rate
 * numerator offset (1..4, matching WisBlockP2PCodingRate / sx126x_lora_cr_t).
 * This library always uses explicit header + CRC on (see applyRadioParams()
 * and send()), so H=0 and CRC=1 are fixed below rather than threaded through
 * as parameters.
 *
 * @param sf Spreading factor
 * @param bw Library bandwidth
 * @param codingRate SX126x driver coding rate
 * @param preambleLen Preamble length in symbols
 * @param payloadLen Payload length in bytes
 * @param ldro Low data rate optimization, 1 = on
 * @return Airtime in ms
 */
uint32_t computeAirtimeMs(uint8_t sf, WisBlockP2PBandwidth bw, uint8_t codingRate, uint16_t preambleLen,
						   uint8_t payloadLen, uint8_t ldro)
{
	double bwHz = (double)bandwidthToHz(bw);
	double tSymMs = (double)(1u << sf) / bwHz * 1000.0;

	double tPreambleMs = (preambleLen + 4.25) * tSymMs;

	double numerator = 8.0 * payloadLen - 4.0 * sf + 28.0 + 16.0 /* CRC=1 */ - 20.0 * 0.0 /* H=0, explicit header */;
	double denominator = 4.0 * (sf - 2.0 * ldro);
	double payloadSymbNb = 8.0;
	if (numerator > 0.0)
	{
		payloadSymbNb += ceil(numerator / denominator) * (codingRate + 4);
	}

	double tPayloadMs = payloadSymbNb * tSymMs;
	return (uint32_t)(tPreambleMs + tPayloadMs + 0.5); // +0.5 for round-to-nearest on the final cast
}

} // namespace

bool LoRaP2PEngine::isLr2021() const
{
#if defined(WISBLOCK_ENABLE_LR2021)
	return wisblock_radio_hal_is_lr2021();
#else
	return false;
#endif
}

bool LoRaP2PEngine::computeRxDutyCycleTiming(uint32_t &rxTimeMs, uint32_t &sleepTimeMs, uint8_t marginSymbols) const
{
	return computeRxDutyCycleTiming(settings.preambleLength, rxTimeMs, sleepTimeMs, marginSymbols);
}

bool LoRaP2PEngine::computeRxDutyCycleTiming(uint16_t txPreambleLengthSymbols, uint32_t &rxTimeMs,
											  uint32_t &sleepTimeMs, uint8_t marginSymbols) const
{
	if (settings.modulation == WISBLOCK_P2P_MOD_FSK)
	{
		return false; // the formula is LoRa specific, use startReceiveDutyCycle() with your own times
	}
	// Bandwidth and SF still come from this radio's own settings - those
	// two have to match the transmitter's for the link to work at all, so
	// unlike preamble length there's no meaningful "transmitter's value"
	// to pass separately here.
	double bwHz = (double)bandwidthToHz(settings.bandwidth);
	double tSymMs = (double)(1u << settings.spreadingFactor) / bwHz * 1000.0;

	// Same "+4.25" constant as computeAirtimeMs() above (Semtech AN1200.13).
	double preambleDurationMs = (txPreambleLengthSymbols + 4.25) * tSymMs;

	// FIX (confirmed by real hardware testing, not theoretical - see the
	// README's "RX duty-cycle timing needed more margin than the textbook
	// formula" note): the textbook "~2 symbols is enough to detect an
	// in-progress preamble" figure was not sufficient in practice on real
	// hardware. rxMs now scales with marginSymbols directly instead of
	// being a fixed 2-symbol floor - callers who need more reliable
	// detection increase marginSymbols and both the RX window and the
	// margin subtracted from the sleep budget grow together.
	double rxMs = tSymMs * (double)marginSymbols;
	double marginMs = (double)marginSymbols * tSymMs;
	double availableSleepMs = preambleDurationMs - rxMs - marginMs;

	if (availableSleepMs < 1.0)
	{
		// Preamble too short to fit any usable duty-cycle window at this
		// margin - increase the transmitter's preamble length first, or
		// use startReceive() instead of duty-cycling at this configuration.
		return false;
	}

	// FIX (also confirmed by real hardware testing): rounding rxMs to the
	// nearest ms was not enough margin to reliably catch every packet in
	// practice either - a 1.5x multiplier was needed on top. Like the
	// change above, this is an empirical correction, not something
	// derived from the datasheet - treat both as a starting point that
	// may need further tuning for your specific hardware/link conditions,
	// not a guaranteed-correct formula.
	rxTimeMs = (uint32_t)(rxMs * 1.5);
	if (rxTimeMs == 0)
	{
		rxTimeMs = 1; // SetRxDutyCycle needs a nonzero RX phase
	}
	sleepTimeMs = (uint32_t)(availableSleepMs + 0.5);
	return true;
}

void LoRaP2PEngine::begin(const WisBlockP2PSettings &initial)
{
	settings = initial;

#if defined(WISBLOCK_ENABLE_LR2021)
	if (isLr2021())
	{
		lrBegin();
		return;
	}
#endif

	sx126x_reset(kCtx);
	sx126x_set_standby(kCtx, SX126X_STANDBY_CFG_RC);
	sx126x_set_pkt_type(kCtx, isFsk() ? SX126X_PKT_TYPE_GFSK : SX126X_PKT_TYPE_LORA);

	// RF switch (DIO2) and TCXO config come from the active WisBlockLoRaHwConfig
	// (see wisblock_radio_hal_get_bsp_config() / WisBlockRadioHal::init()) -
	// not hardcoded to the three built-in WisBlock presets, since the
	// Creation Log entry "Flexible hw_config-based radio init (RAK3401 /
	// non-WisBlock boards)". LoRaWAN mode's ral_sx126x_bsp_get_rf_switch_cfg()/
	// _get_xosc_cfg() (wisblock_ral_sx126x_bsp.c) read the exact same config,
	// so the two modes always agree.
	{
		const WisBlockLoRaRadioBspConfig *bspCfg = wisblock_radio_hal_get_bsp_config();
		sx126x_set_dio2_as_rf_sw_ctrl(kCtx, bspCfg->dio2AntSwitch);
		if (bspCfg->dio3Tcxo)
		{
			sx126x_set_dio3_as_tcxo_ctrl(kCtx, bspCfg->tcxoVoltage, bspCfg->tcxoStartupTimeInTick);
		}
	}

	applyRadioParams();
}

void LoRaP2PEngine::applySettings(const WisBlockP2PSettings &newSettings)
{
	bool modulationChanged = settings.modulation != newSettings.modulation;
	settings = newSettings;
#if defined(WISBLOCK_ENABLE_LR2021)
	if (isLr2021())
	{
		lrApplySettings(modulationChanged);
		return;
	}
#endif
	if (modulationChanged)
	{
		// The packet type can only be changed in standby
		sx126x_set_standby(kCtx, SX126X_STANDBY_CFG_RC);
		sx126x_set_pkt_type(kCtx, isFsk() ? SX126X_PKT_TYPE_GFSK : SX126X_PKT_TYPE_LORA);
	}
	applyRadioParams();
}

void LoRaP2PEngine::applyRadioParams()
{
	sx126x_set_rf_freq(kCtx, settings.frequencyHz);

	if (isFsk())
	{
		applyFskParams();
	}
	else
	{
		sx126x_mod_params_lora_t modParams;
		modParams.sf = (sx126x_lora_sf_t)settings.spreadingFactor;
		modParams.bw = toSx126xBandwidth(settings.bandwidth);
		modParams.cr = (sx126x_lora_cr_t)settings.codingRate; // WisBlockP2PCodingRate values (1..4) match sx126x_lora_cr_t exactly
		modParams.ldro = computeLdro(settings.spreadingFactor, settings.bandwidth);
		sx126x_set_lora_mod_params(kCtx, &modParams);

		applyPktParams(255); // max; overridden per-TX in send(), irrelevant for RX (explicit header)

		// 16-bit LoRa sync word (SX126x register 0x0740/0x0741). The driver's own
		// sx126x_set_lora_sync_word() only takes the 8-bit short form (0x12/0x34 ->
		// 0x1424/0x3444), so write the full 16-bit value directly.
		{
			const uint16_t kRegLoraSyncWord = 0x0740;
			uint8_t sw[2] = {(uint8_t)(settings.syncWord >> 8), (uint8_t)(settings.syncWord & 0xFF)};
			sx126x_write_register(kCtx, kRegLoraSyncWord, sw, 2);
		}
	}

	// CRITICAL, previously missing: SetTxParams alone (below) only sets the
	// requested power level and ramp time - it does NOT select which PA
	// (LP or HP) is active. Without this call the chip runs on its
	// power-on-reset PA default (LP PA, not the SX1262's HP PA capable of
	// +22dBm), silently capping actual radiated power far below whatever
	// dBm value is requested - this was the real cause of P2P TX arriving
	// dramatically weaker than expected (measured ~50dB below a reference
	// implementation that does configure this). Same values already
	// verified correct for these boards in wisblock_ral_sx126x_bsp.c's
	// ral_sx126x_bsp_get_tx_cfg() (LoRaWAN mode's equivalent config path,
	// which P2P mode doesn't go through at all, hence this gap existing
	// only in P2P mode).
	sx126x_cfg_tx_clamp(kCtx); // SX1262 HP PA errata workaround, required alongside device_sel == 0x00
	sx126x_pa_cfg_params_t paCfg;
	paCfg.pa_duty_cycle = 0x04;
	paCfg.hp_max = 0x07; // to achieve 22dBm
	paCfg.device_sel = 0x00; // select SX1262/SX1268 device (HP PA)
	paCfg.pa_lut = 0x01;	  // reserved value, same for sx1261/sx1262/sx1268
	sx126x_set_pa_cfg(kCtx, &paCfg);

	// Clamp to the SX1262 HP PA's valid range, same bounds
	// wisblock_ral_sx126x_bsp.c applies for LoRaWAN mode.
	int16_t power = settings.txPowerDbm;
	if (power > 22)
	{
		power = 22;
	}
	if (power < -9)
	{
		power = -9;
	}
	sx126x_set_tx_params(kCtx, (int8_t)power, SX126X_RAMP_40_US);
	sx126x_set_buffer_base_address(kCtx, 0, 0);

	// Per the driver's own doc comment on sx126x_cfg_rx_boosted(): "not kept
	// in retention memory - shall be enabled each time the chip leaves
	// sleep mode." applyRadioParams() already runs on every path that
	// matters for that (begin(), applySettings(), and - critically -
	// reconfigureAfterColdSleep(), called before every CAD/RX/TX after this
	// engine's cold-start sleep wipes the chip's config), so a single call
	// here covers all of them without needing to duplicate it at each call
	// site.
	sx126x_cfg_rx_boosted(kCtx, settings.rxBoostedGainEnabled);
}

void LoRaP2PEngine::applyFskParams()
{
	// Undo a bit rate workaround of an earlier configuration before the modulation changes
	sx126x_workaround_gfsk_reset(kCtx);

	sx126x_mod_params_gfsk_t mod = buildGfskModParams(settings);
	sx126x_set_gfsk_mod_params(kCtx, &mod);
	applyPktParams(255);

	// Semtech workarounds for two bit rate/deviation/bandwidth combinations, see the driver's docs
	if (mod.br_in_bps == 1200 && mod.fdev_in_hz == 5000 && mod.bw_dsb_param == SX126X_GFSK_BW_19500)
	{
		sx126x_workaround_gfsk_1_2_kbps(kCtx);
	}
	else if (mod.br_in_bps == 600 && mod.fdev_in_hz == 800 && mod.bw_dsb_param == SX126X_GFSK_BW_4800)
	{
		sx126x_workaround_gfsk_0_6_kbps(kCtx);
	}

	uint8_t swLen = settings.fskSyncWordLen;
	if (swLen < 1 || swLen > WISBLOCK_FSK_SYNCWORD_MAX_LEN)
	{
		swLen = 3;
	}
	sx126x_set_gfsk_sync_word(kCtx, settings.fskSyncWord, swLen);
	sx126x_set_gfsk_crc_seed(kCtx, 0x1D0F); // CRC_2_BYTES_INV: inverted CCITT
	sx126x_set_gfsk_crc_polynomial(kCtx, 0x1021);
	sx126x_set_gfsk_whitening_seed(kCtx, 0x01FF);
}

void LoRaP2PEngine::applyPktParams(uint8_t payloadLen)
{
	if (isFsk())
	{
		sx126x_pkt_params_gfsk_t pktParams = buildGfskPktParams(settings, payloadLen);
		sx126x_set_gfsk_pkt_params(kCtx, &pktParams);
		return;
	}
	sx126x_pkt_params_lora_t pktParams;
	pktParams.preamble_len_in_symb = settings.preambleLength;
	pktParams.header_type = SX126X_LORA_PKT_EXPLICIT;
	pktParams.pld_len_in_bytes = payloadLen;
	pktParams.crc_is_on = true;
	pktParams.invert_iq_is_on = settings.iqInversion;
	sx126x_set_lora_pkt_params(kCtx, &pktParams);
}

bool LoRaP2PEngine::send(const uint8_t *data, uint8_t length)
{
	if (data == nullptr || length == 0)
	{
		return false;
	}
	if (rxMode == WISBLOCK_P2P_RX_CONTINUOUS)
	{
		return false; // like RUI3 (AT_BUSY_ERROR): stop the continuous receive first with startReceive(0)
	}
#if defined(WISBLOCK_ENABLE_LR2021)
	if (isLr2021())
	{
		return lrSend(data, length);
	}
#endif
	reconfigureAfterColdSleep();

	// settings.cadEnabled gates whether the *caller* should run CAD first
	// (via startCad(), then call send() from the onCadResult callback once
	// WISBLOCK_CAD_CHANNEL_CLEAR comes back) - this method always sends
	// immediately, since blocking on CAD here would stall loop().
	sx126x_set_standby(kCtx, SX126X_STANDBY_CFG_RC);
	if (rxMode != WISBLOCK_P2P_RX_CONTINUOUS_TX)
	{
		rxMode = 0; // the send ends a running receive window, only mode 65533 continues after the TX
	}

	applyPktParams(length);

	sx126x_write_buffer(kCtx, 0, data, length);
	lastTxPayloadLength = length;

	sx126x_set_dio_irq_params(kCtx, kAllIrqsForTx, kAllIrqsForTx, 0, 0); // route to DIO1 only
	sx126x_clear_irq_status(kCtx, SX126X_IRQ_ALL);

	// 0 = SX126X_RX_SINGLE_MODE-equivalent "no timeout" for TX: chip stays
	// in TX until the packet is fully sent, then auto-returns to STDBY_RC.
	sx126x_set_tx(kCtx, 0);
	return true;
}

bool LoRaP2PEngine::startReceive(uint32_t mode)
{
	if (mode == WISBLOCK_P2P_RX_STOP)
	{
		stopReceive();
		return true;
	}
	if (rxMode == WISBLOCK_P2P_RX_CONTINUOUS)
	{
		return false; // like RUI3: in continuous receive only startReceive(0) is accepted
	}
	rxMode = mode;
	startRadioReceive(mode);
	return true;
}

void LoRaP2PEngine::startRadioReceive(uint32_t mode)
{
#if defined(WISBLOCK_ENABLE_LR2021)
	if (isLr2021())
	{
		lrStartRadioReceive(mode);
		return;
	}
#endif
	reconfigureAfterColdSleep();
	sx126x_set_standby(kCtx, SX126X_STANDBY_CFG_RC);

	applyPktParams(255);

	sx126x_set_dio_irq_params(kCtx, kAllIrqsForRx, kAllIrqsForRx, 0, 0);
	sx126x_clear_irq_status(kCtx, SX126X_IRQ_ALL);

	if (mode == WISBLOCK_P2P_RX_CONTINUOUS_TX || mode == WISBLOCK_P2P_RX_CONTINUOUS)
	{
		// Continuous reception. SX126X_RX_CONTINUOUS (0x00FFFFFF) is a value in RTC steps, so it must
		// go to the _with_timeout_in_rtc_step() variant. sx126x_set_rx() takes milliseconds and
		// rejects everything above SX126X_MAX_TIMEOUT_IN_MS without sending the command - the radio
		// then stays in standby and never receives.
		sx126x_set_rx_with_timeout_in_rtc_step(kCtx, SX126X_RX_CONTINUOUS);
	}
	else if (mode == WISBLOCK_P2P_RX_SINGLE)
	{
		// No timeout, the radio goes to standby after the first packet
		sx126x_set_rx_with_timeout_in_rtc_step(kCtx, SX126X_RX_SINGLE_MODE);
	}
	else
	{
		// A timeout above the maximum of the radio (about 262 s) is limited, not dropped
		sx126x_set_rx(kCtx, mode > SX126X_MAX_TIMEOUT_IN_MS ? SX126X_MAX_TIMEOUT_IN_MS : mode);
	}
}

void LoRaP2PEngine::startReceiveDutyCycle(uint32_t rxTimeMs, uint32_t sleepTimeMs)
{
	rxMode = 0; // not one of the startReceive() modes, send() and CAD interrupt it
#if defined(WISBLOCK_ENABLE_LR2021)
	if (isLr2021())
	{
		lrStartReceiveDutyCycle(rxTimeMs, sleepTimeMs);
		return;
	}
#endif
	reconfigureAfterColdSleep();
	sx126x_set_standby(kCtx, SX126X_STANDBY_CFG_RC);

	applyPktParams(255);

	// Same IRQ routing as startReceive() - a packet arriving during a duty
	// cycle's RX phase raises the exact same RX_DONE/CRC_ERROR/etc. flags
	// on DIO1 as continuous RX does; handleEvents() doesn't need to know
	// which mode got it there.
	sx126x_set_dio_irq_params(kCtx, kAllIrqsForRx, kAllIrqsForRx, 0, 0);
	sx126x_clear_irq_status(kCtx, SX126X_IRQ_ALL);

	// FIX/NOTE: NOT routed through checkDeviceReady()'s sleep-state
	// tracking (SetSleep opcode 0x84) - the chip's autonomous sleep phases
	// here are its own internal state, invisible to and untouched by our
	// radioMode/isAsleep() bookkeeping in wisblock_radio_hal_*.cpp. That's
	// intentional: LORA_ANT_PWR needs to stay powered for the whole duty
	// cycle sequence (the chip needs it during every RX phase, which our
	// software has no visibility into the timing of), so it correctly
	// stays on throughout rather than being cut on the chip's internal
	// sleep phases the way an explicit sleepRadio() call would.
	sx126x_set_rx_duty_cycle(kCtx, rxTimeMs, sleepTimeMs);
}

void LoRaP2PEngine::stopReceive()
{
#if defined(WISBLOCK_ENABLE_LR2021)
	if (isLr2021())
	{
		lrStopReceive();
		return;
	}
#endif
	rxMode = 0;
	cadResumeRx = false;
	sx126x_set_standby(kCtx, SX126X_STANDBY_CFG_RC);
}

void LoRaP2PEngine::sleep()
{
#if defined(WISBLOCK_ENABLE_LR2021)
	if (isLr2021())
	{
		lrSleep();
		return;
	}
#endif
	// Cold-start: see sleep()'s doc comment in LoRaP2PEngine.h for why this
	// changed from warm-start. RTC-wakeup bit intentionally left clear
	// either way - this library manages timing on the MCU side
	// (WisBlockLbmTask / the application's own scheduling), not via the
	// SX1262's internal wakeup timer.
	sx126x_set_sleep(kCtx, SX126X_SLEEP_CFG_COLD_START);
	needsReconfigureAfterSleep = true;
	rxMode = 0;
	cadResumeRx = false;
}

void LoRaP2PEngine::reconfigureAfterColdSleep()
{
	if (!needsReconfigureAfterSleep)
	{
		return;
	}
	needsReconfigureAfterSleep = false;

	// Everything begin() does except sx126x_reset() - checkDeviceReady()'s
	// wake sequence (wisblock_radio_hal_*.cpp) already brought BUSY/SPI
	// back up before this runs, a hardware reset pin toggle isn't needed
	// (and would itself impose real extra latency here, since a reset
	// requires waiting out the chip's boot time on top of everything
	// else).
	sx126x_set_standby(kCtx, SX126X_STANDBY_CFG_RC);
	sx126x_set_pkt_type(kCtx, isFsk() ? SX126X_PKT_TYPE_GFSK : SX126X_PKT_TYPE_LORA);
	{
		const WisBlockLoRaRadioBspConfig *bspCfg = wisblock_radio_hal_get_bsp_config();
		sx126x_set_dio2_as_rf_sw_ctrl(kCtx, bspCfg->dio2AntSwitch);
		if (bspCfg->dio3Tcxo)
		{
			sx126x_set_dio3_as_tcxo_ctrl(kCtx, bspCfg->tcxoVoltage, bspCfg->tcxoStartupTimeInTick);
		}
	}
	applyRadioParams();
}

bool LoRaP2PEngine::getInstantRssi(int16_t &rssiDbm)
{
#if defined(WISBLOCK_ENABLE_LR2021)
	if (isLr2021())
	{
		return lrGetInstantRssi(rssiDbm);
	}
#endif
	return sx126x_get_rssi_inst(kCtx, &rssiDbm) == SX126X_STATUS_OK;
}

bool LoRaP2PEngine::getRadioStatus(uint8_t &chipMode, uint8_t &cmdStatus, uint16_t &deviceErrors)
{
#if defined(WISBLOCK_ENABLE_LR2021)
	if (isLr2021())
	{
		return lrGetRadioStatus(chipMode, cmdStatus, deviceErrors);
	}
#endif
	sx126x_chip_status_t st;
	sx126x_errors_mask_t errs = 0;
	if (sx126x_get_status(kCtx, &st) != SX126X_STATUS_OK || sx126x_get_device_errors(kCtx, &errs) != SX126X_STATUS_OK)
	{
		return false;
	}
	chipMode = (uint8_t)st.chip_mode;
	cmdStatus = (uint8_t)st.cmd_status;
	deviceErrors = errs;
	return true;
}

bool LoRaP2PEngine::getFskStats(uint16_t &received, uint16_t &crcErrors, uint16_t &lengthErrors)
{
	if (!isFsk())
	{
		return false;
	}
#if defined(WISBLOCK_ENABLE_LR2021)
	if (isLr2021())
	{
		return lrGetFskStats(received, crcErrors, lengthErrors);
	}
#endif
	sx126x_stats_gfsk_t stats;
	if (sx126x_get_gfsk_stats(kCtx, &stats) != SX126X_STATUS_OK)
	{
		return false;
	}
	received = stats.nb_pkt_received;
	crcErrors = stats.nb_pkt_crc_error;
	lengthErrors = stats.nb_pkt_len_error;
	return true;
}

void LoRaP2PEngine::startCad()
{
	if (isFsk())
	{
		// Channel activity detection only exists for LoRa
		if (cadResultCb)
		{
			cadResultCb(WISBLOCK_CAD_ERROR);
		}
		return;
	}
#if defined(WISBLOCK_ENABLE_LR2021)
	if (isLr2021())
	{
		lrStartCad();
		return;
	}
#endif
	reconfigureAfterColdSleep();
	// SetCadParams and SetCad are only accepted from standby. A running receive (continuous or with a
	// timeout) is not left by itself, so without this the CAD never started while the radio was
	// listening and the result callback was never called.
	sx126x_set_standby(kCtx, SX126X_STANDBY_CFG_RC);
	// A continuous receive is started again when the CAD is done, any other receive ends here
	cadResumeRx = (rxMode == WISBLOCK_P2P_RX_CONTINUOUS_TX || rxMode == WISBLOCK_P2P_RX_CONTINUOUS);
	if (!cadResumeRx)
	{
		rxMode = 0;
	}
	sx126x_cad_params_t cadParams;
	cadParams.cad_symb_nb = SX126X_CAD_04_SYMB;
	cadParams.cad_detect_peak = 22; // Semtech reference apps' default for SF7-SF10; retune for your SF/BW if you see false positives/negatives
	cadParams.cad_detect_min = 10;
	cadParams.cad_exit_mode = SX126X_CAD_ONLY;
	cadParams.cad_timeout = 0;
	sx126x_set_cad_params(kCtx, &cadParams);

	sx126x_set_dio_irq_params(kCtx, kAllIrqsForCad, kAllIrqsForCad, 0, 0);
	sx126x_clear_irq_status(kCtx, SX126X_IRQ_ALL);

	sx126x_set_cad(kCtx);
}

uint32_t LoRaP2PEngine::handleEvents()
{
	// No LBM-style self-scheduling contract for P2P mode - purely IRQ-driven.
	// A large fixed value here just means "no specific deadline, rely on the
	// next DIO1 IRQ or an application-triggered call"; see
	// WisBlockLbmTask's task loop for how this gets clamped to a sane
	// background-task wait ceiling regardless.
	constexpr uint32_t kNoScheduledDeadlineMs = 60000;

#if defined(WISBLOCK_ENABLE_LR2021)
	if (isLr2021())
	{
		return lrHandleEvents();
	}
#endif

	// FIX (was the direct cause of "sleepRadio() called every cycle, idle
	// current still ~20x higher than expected"): sx126x_get_irq_status()
	// below is itself an SPI transaction, and checkDeviceReady()
	// (wisblock_radio_hal_*.cpp) treats *every* SPI transaction as a wake
	// request - antenna power back on, NSS wake sequence run - regardless
	// of why it was issued. This function used to call it unconditionally
	// on every single invocation, including the routine "anything
	// pending?" poll that runs whenever the background task wakes for any
	// reason. The moment anything gave that task's event semaphore during
	// an otherwise-idle stretch, this poll would silently wake a
	// deliberately-slept radio back into STANDBY as a side effect - and
	// nothing ever put it back to sleep afterward, since only an explicit
	// sleepRadio() call does that. A radio that's already asleep has
	// nothing new to report via IRQ status anyway (real events only ever
	// arrive on DIO1 while genuinely listening/transmitting/CAD'ing, none
	// of which happen while asleep), so the fix is simply to not ask.
	if (WisBlockRadioHal::isAsleep())
	{
		return kNoScheduledDeadlineMs;
	}

	sx126x_irq_mask_t irq = 0;
	if (sx126x_get_irq_status(kCtx, &irq) != SX126X_STATUS_OK || irq == 0)
	{
		return kNoScheduledDeadlineMs; // nothing pending - cheap no-op path for the common case
	}
	sx126x_clear_irq_status(kCtx, irq);

	if (irq & SX126X_IRQ_TX_DONE)
	{
		WisBlockTxResult r;
		r.success = true;
		if (isFsk())
		{
			sx126x_pkt_params_gfsk_t pkt = buildGfskPktParams(settings, lastTxPayloadLength);
			sx126x_mod_params_gfsk_t mod = buildGfskModParams(settings);
			r.airtimeMs = sx126x_get_gfsk_time_on_air_in_ms(&pkt, &mod);
		}
		else
		{
			r.airtimeMs = computeAirtimeMs(settings.spreadingFactor, settings.bandwidth, (uint8_t)settings.codingRate,
										   settings.preambleLength, lastTxPayloadLength,
										   computeLdro(settings.spreadingFactor, settings.bandwidth));
		}
		// Mode 65533: back to receive before the callback, so a send() in the callback works as well
		if (rxMode == WISBLOCK_P2P_RX_CONTINUOUS_TX)
		{
			startRadioReceive(rxMode);
		}
		if (txFinishedCb)
		{
			txFinishedCb(r);
		}
	}

	if (irq & SX126X_IRQ_RX_DONE)
	{
		WisBlockRxResult r;
		sx126x_rx_buffer_status_t bufStatus;
		sx126x_pkt_status_lora_t pktStatus;
		// FSK: the radio reports RX_DONE together with CRC_ERROR for a damaged packet. A damaged
		// packet is reported like an RX timeout (no data), so the application can start the RX again.
		bool fskCrcError = isFsk() && (irq & SX126X_IRQ_CRC_ERROR);

		if (!fskCrcError && sx126x_get_rx_buffer_status(kCtx, &bufStatus) == SX126X_STATUS_OK)
		{
			uint8_t len = bufStatus.pld_len_in_bytes > sizeof(r.data) ? sizeof(r.data) : bufStatus.pld_len_in_bytes;
			sx126x_read_buffer(kCtx, bufStatus.buffer_start_pointer, r.data, len);
			r.length = len;
		}
		if (fskCrcError)
		{
			// nothing to read
		}
		else if (isFsk())
		{
			sx126x_pkt_status_gfsk_t fskStatus;
			if (sx126x_get_gfsk_pkt_status(kCtx, &fskStatus) == SX126X_STATUS_OK)
			{
				r.rssi = fskStatus.rssi_avg; // dBm, there is no SNR in FSK
				r.snr = 0;
			}
		}
		else if (sx126x_get_lora_pkt_status(kCtx, &pktStatus) == SX126X_STATUS_OK)
		{
			r.rssi = pktStatus.rssi_pkt_in_dbm;
			r.snr = pktStatus.snr_pkt_in_db;
		}
		// A receive window or a single receive is finished, the radio is in standby
		if (rxMode != WISBLOCK_P2P_RX_CONTINUOUS_TX && rxMode != WISBLOCK_P2P_RX_CONTINUOUS)
		{
			rxMode = 0;
		}
		if (rxFinishedCb)
		{
			rxFinishedCb(r);
		}
	}

	if (irq & SX126X_IRQ_TIMEOUT)
	{
		if (rxMode != WISBLOCK_P2P_RX_CONTINUOUS_TX && rxMode != WISBLOCK_P2P_RX_CONTINUOUS)
		{
			rxMode = 0; // the receive window ended
		}
		// Surfaces as an empty RX result today; add a dedicated
		// onRxTimeout callback if your app needs to distinguish "nothing
		// received in time" from "received 0 bytes" (which can't actually
		// happen with CRC-checked explicit-header LoRa, but the two are
		// conflated here for now).
		if (rxFinishedCb)
		{
			WisBlockRxResult r;
			rxFinishedCb(r);
		}
	}

	if (irq & SX126X_IRQ_CAD_DONE)
	{
		bool detected = (irq & SX126X_IRQ_CAD_DETECTED) != 0;
		if (cadResumeRx)
		{
			// Back to the continuous receive before the callback, a send() in the callback works as well
			cadResumeRx = false;
			startRadioReceive(rxMode);
		}
		if (cadResultCb)
		{
			cadResultCb(detected ? WISBLOCK_CAD_CHANNEL_DETECTED : WISBLOCK_CAD_CHANNEL_CLEAR);
		}
	}

	return kNoScheduledDeadlineMs;
}
