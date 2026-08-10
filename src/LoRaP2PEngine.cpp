#include "LoRaP2PEngine.h"

#include <math.h>

#include "sx126x.h" // vendored: src/lbm/smtc_modem_core/radio_drivers/sx126x_driver/src/sx126x.h
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

// Semtech AN1200.13: enable Low Data Rate Optimization whenever the symbol
// period exceeds 16.38ms - matters most at SF11/SF12 on narrow bandwidths.
uint8_t computeLdro(uint8_t sf, WisBlockP2PBandwidth bw)
{
	double symbolPeriodMs = (1u << sf) * 1000.0 / (double)bandwidthToHz(bw);
	return symbolPeriodMs > 16.38 ? 1 : 0;
}

constexpr uint16_t kAllIrqsForTx = SX126X_IRQ_TX_DONE | SX126X_IRQ_TIMEOUT;
constexpr uint16_t kAllIrqsForRx = SX126X_IRQ_RX_DONE | SX126X_IRQ_TIMEOUT | SX126X_IRQ_CRC_ERROR;
constexpr uint16_t kAllIrqsForCad = SX126X_IRQ_CAD_DONE | SX126X_IRQ_CAD_DETECTED;
// Semtech AN1200.13 "LoRa Modem Designer's Guide", ยง4 Time on air:
//
//   Tsym = 2^SF / BW                                                  (seconds)
//   Tpreamble = (preambleLen + 4.25) * Tsym
//   payloadSymbNb = 8 + max(ceil((8*PL - 4*SF + 28 + 16*CRC - 20*H)
//                                  / (4*(SF - 2*DE))) * (CR + 4), 0)
//   Tpayload = payloadSymbNb * Tsym
//   Ttotal = Tpreamble + Tpayload
//
// where PL = payload length in bytes, CRC = 1 if CRC enabled, H = 0 for
// explicit header (1 for implicit), DE = 1 if LDRO is on, CR = coding rate
// numerator offset (1..4, matching WisBlockP2PCodingRate / sx126x_lora_cr_t).
// This library always uses explicit header + CRC on (see applyRadioParams()
// and send()), so H=0 and CRC=1 are fixed below rather than threaded through
// as parameters.
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

bool LoRaP2PEngine::computeRxDutyCycleTiming(uint32_t &rxTimeMs, uint32_t &sleepTimeMs, uint8_t marginSymbols) const
{
	return computeRxDutyCycleTiming(settings.preambleLength, rxTimeMs, sleepTimeMs, marginSymbols);
}

bool LoRaP2PEngine::computeRxDutyCycleTiming(uint16_t txPreambleLengthSymbols, uint32_t &rxTimeMs,
											  uint32_t &sleepTimeMs, uint8_t marginSymbols) const
{
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

	sx126x_reset(kCtx);
	sx126x_set_standby(kCtx, SX126X_STANDBY_CFG_RC);
	sx126x_set_pkt_type(kCtx, SX126X_PKT_TYPE_LORA);

	// All three boards (RAK4631/RAK3312/RAK11310) use the SX1262's built-in
	// RF switch control via DIO2, not an MCU GPIO (see LORA_ANT_SWITCH ==
	// -1 in WisBlockLoRaBoards.h).
	sx126x_set_dio2_as_rf_sw_ctrl(kCtx, true);

	// TCXO: 3.3V / 5ms startup delay, confirmed for this board's SX1262 module.
	sx126x_set_dio3_as_tcxo_ctrl(kCtx, SX126X_TCXO_CTRL_3_3V, 50 << 6 /* 50ms, in 15.625us steps: 50000/15.625 = 3200 = 50<<6 */);

	applyRadioParams();
}

void LoRaP2PEngine::applySettings(const WisBlockP2PSettings &newSettings)
{
	settings = newSettings;
	applyRadioParams();
}

void LoRaP2PEngine::applyRadioParams()
{
	sx126x_set_rf_freq(kCtx, settings.frequencyHz);

	sx126x_mod_params_lora_t modParams;
	modParams.sf = (sx126x_lora_sf_t)settings.spreadingFactor;
	modParams.bw = toSx126xBandwidth(settings.bandwidth);
	modParams.cr = (sx126x_lora_cr_t)settings.codingRate; // WisBlockP2PCodingRate values (1..4) match sx126x_lora_cr_t exactly
	modParams.ldro = computeLdro(settings.spreadingFactor, settings.bandwidth);
	sx126x_set_lora_mod_params(kCtx, &modParams);

	sx126x_pkt_params_lora_t pktParams;
	pktParams.preamble_len_in_symb = settings.preambleLength;
	pktParams.header_type = SX126X_LORA_PKT_EXPLICIT;
	pktParams.pld_len_in_bytes = 255; // max; overridden per-TX in send(), irrelevant for RX (explicit header)
	pktParams.crc_is_on = true;
	pktParams.invert_iq_is_on = false;
	sx126x_set_lora_pkt_params(kCtx, &pktParams);

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

bool LoRaP2PEngine::send(const uint8_t *data, uint8_t length)
{
	if (data == nullptr || length == 0)
	{
		return false;
	}
	reconfigureAfterColdSleep();

	// settings.cadEnabled gates whether the *caller* should run CAD first
	// (via startCad(), then call send() from the onCadResult callback once
	// WISBLOCK_CAD_CHANNEL_CLEAR comes back) - this method always sends
	// immediately, since blocking on CAD here would stall loop().
	sx126x_set_standby(kCtx, SX126X_STANDBY_CFG_RC);

	sx126x_pkt_params_lora_t pktParams;
	pktParams.preamble_len_in_symb = settings.preambleLength;
	pktParams.header_type = SX126X_LORA_PKT_EXPLICIT;
	pktParams.pld_len_in_bytes = length;
	pktParams.crc_is_on = true;
	pktParams.invert_iq_is_on = false;
	sx126x_set_lora_pkt_params(kCtx, &pktParams);

	sx126x_write_buffer(kCtx, 0, data, length);
	lastTxPayloadLength = length;

	sx126x_set_dio_irq_params(kCtx, kAllIrqsForTx, kAllIrqsForTx, 0, 0); // route to DIO1 only
	sx126x_clear_irq_status(kCtx, SX126X_IRQ_ALL);

	// 0 = SX126X_RX_SINGLE_MODE-equivalent "no timeout" for TX: chip stays
	// in TX until the packet is fully sent, then auto-returns to STDBY_RC.
	sx126x_set_tx(kCtx, 0);
	return true;
}

void LoRaP2PEngine::startReceive(uint32_t timeoutMs)
{
	reconfigureAfterColdSleep();
	sx126x_set_standby(kCtx, SX126X_STANDBY_CFG_RC);

	sx126x_pkt_params_lora_t pktParams;
	pktParams.preamble_len_in_symb = settings.preambleLength;
	pktParams.header_type = SX126X_LORA_PKT_EXPLICIT;
	pktParams.pld_len_in_bytes = 255; // ignored on RX with explicit header; actual length comes from sx126x_get_rx_buffer_status
	pktParams.crc_is_on = true;
	pktParams.invert_iq_is_on = false;
	sx126x_set_lora_pkt_params(kCtx, &pktParams);

	sx126x_set_dio_irq_params(kCtx, kAllIrqsForRx, kAllIrqsForRx, 0, 0);
	sx126x_clear_irq_status(kCtx, SX126X_IRQ_ALL);

	uint32_t timeout = (timeoutMs == 0) ? SX126X_RX_CONTINUOUS : timeoutMs;
	sx126x_set_rx(kCtx, timeout);
}

void LoRaP2PEngine::startReceiveDutyCycle(uint32_t rxTimeMs, uint32_t sleepTimeMs)
{
	reconfigureAfterColdSleep();
	sx126x_set_standby(kCtx, SX126X_STANDBY_CFG_RC);

	sx126x_pkt_params_lora_t pktParams;
	pktParams.preamble_len_in_symb = settings.preambleLength;
	pktParams.header_type = SX126X_LORA_PKT_EXPLICIT;
	pktParams.pld_len_in_bytes = 255; // ignored on RX with explicit header; actual length comes from sx126x_get_rx_buffer_status
	pktParams.crc_is_on = true;
	pktParams.invert_iq_is_on = false;
	sx126x_set_lora_pkt_params(kCtx, &pktParams);

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
	sx126x_set_standby(kCtx, SX126X_STANDBY_CFG_RC);
}

void LoRaP2PEngine::sleep()
{
	// Cold-start: see sleep()'s doc comment in LoRaP2PEngine.h for why this
	// changed from warm-start. RTC-wakeup bit intentionally left clear
	// either way - this library manages timing on the MCU side
	// (WisBlockLbmTask / the application's own scheduling), not via the
	// SX1262's internal wakeup timer.
	sx126x_set_sleep(kCtx, SX126X_SLEEP_CFG_COLD_START);
	needsReconfigureAfterSleep = true;
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
	sx126x_set_pkt_type(kCtx, SX126X_PKT_TYPE_LORA);
	sx126x_set_dio2_as_rf_sw_ctrl(kCtx, true);
	sx126x_set_dio3_as_tcxo_ctrl(kCtx, SX126X_TCXO_CTRL_3_3V, 50 << 6);
	applyRadioParams();
}

void LoRaP2PEngine::startCad()
{
	reconfigureAfterColdSleep();
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
		r.airtimeMs = computeAirtimeMs(settings.spreadingFactor, settings.bandwidth, (uint8_t)settings.codingRate,
										settings.preambleLength, lastTxPayloadLength,
										computeLdro(settings.spreadingFactor, settings.bandwidth));
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

		if (sx126x_get_rx_buffer_status(kCtx, &bufStatus) == SX126X_STATUS_OK)
		{
			uint8_t len = bufStatus.pld_len_in_bytes > sizeof(r.data) ? sizeof(r.data) : bufStatus.pld_len_in_bytes;
			sx126x_read_buffer(kCtx, bufStatus.buffer_start_pointer, r.data, len);
			r.length = len;
		}
		if (sx126x_get_lora_pkt_status(kCtx, &pktStatus) == SX126X_STATUS_OK)
		{
			r.rssi = pktStatus.rssi_pkt_in_dbm;
			r.snr = pktStatus.snr_pkt_in_db;
		}
		if (rxFinishedCb)
		{
			rxFinishedCb(r);
		}
	}

	if (irq & SX126X_IRQ_TIMEOUT)
	{
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
		if (cadResultCb)
		{
			cadResultCb(detected ? WISBLOCK_CAD_CHANNEL_DETECTED : WISBLOCK_CAD_CHANNEL_CLEAR);
		}
	}

	return kNoScheduledDeadlineMs;
}
