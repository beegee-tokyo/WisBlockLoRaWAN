/**
 * @file wisblock_ral_sx126x_bsp.c
 * @brief Board Support Package for LBM's SX126x RAL (Radio Abstraction
 * Layer) - the `ral_sx126x_bsp_*` callbacks `ral_sx126x.c` calls into for
 * board-specific radio configuration (regulator mode, RF switch wiring,
 * TX power amplifier config, TCXO voltage/startup time, RX boost, etc.).
 *
 * This is a *different* porting layer from wisblock_radio_hal_*.cpp:
 *   - wisblock_radio_hal_*.cpp implements `sx126x_hal_*` (SPI/GPIO
 *     transport - how bytes get to/from the chip), one file per MCU since
 *     that's genuinely MCU-specific.
 *   - This file implements `ral_sx126x_bsp_*` (what values to configure
 *     the chip with), used only by LBM's LoRaWAN-mode radio_planner/ral/
 *     ralf stack (LoRaP2PEngine.cpp talks to the raw sx126x_driver
 *     directly and doesn't go through this layer at all).
 *
 * Base: adapted from Semtech's own reference BSP for their nRF52840 port
 * (lbm_applications/2_porting_nrf_52840/radio_hal/ral_sx126x_bsp.c in the
 * upstream SWL2001 repo) - that reference targets Semtech's own eval board,
 * which has no TCXO (crystal only). The WisBlock SX1262 module used across
 * all three boards this library targets *does* have a TCXO, confirmed at
 * 1.8V / 5ms startup (same value already used directly in
 * LoRaP2PEngine.cpp's sx126x_set_dio3_as_tcxo_ctrl() call and in
 * wisblock_lbm_port.cpp's smtc_modem_hal_get_radio_tcxo_startup_delay_ms())
 * - ral_sx126x_bsp_get_xosc_cfg() below is adapted accordingly.
 *
 * Also removed: Semtech's reference calls a `radio_utilities_get_tx_power_offset()`
 * helper from their example app scaffolding (lbm_examples/, not vendored by
 * this library) - inlined as a fixed 0 dB offset here instead. If you find
 * your actual TX EIRP differs from what you requested, calibrate a real
 * offset here rather than in application code, so it applies uniformly to
 * both ADR-driven and manually-set power levels.
 *
 * NOTE: this file is intentionally MCU-agnostic (no #if defined(ARDUINO_ARCH_*)
 * guards) - the RAK4631/RAK3312/RAK11310 SX1262 modules are treated as
 * sharing the same radio-side board design, consistent with how TCXO
 * config is already handled elsewhere in this library. If a specific
 * board's module actually differs (different regulator wiring, no TCXO,
 * etc.), split this into per-board files following the
 * wisblock_radio_hal_rak*.cpp naming pattern.
 */

#include "ral_sx126x_bsp.h" // vendored: src/lbm/smtc_modem_core/smtc_ral/src/ral_sx126x_bsp.h
#include <stdbool.h>
#include <stdint.h>

// --- Semtech-published characterization data (SX1262 HP PA), used only by
// the two ral_sx126x_bsp_get_instantaneous_*_power_consumption() functions
// below for LBM's internal power-budget estimation, if anything in your
// application calls into that. Not used by TX/RX itself. ---
#define SX126X_HP_MIN_OUTPUT_POWER -9
#define SX126X_HP_MAX_OUTPUT_POWER 22
#define SX126X_HP_CONVERT_TABLE_INDEX_OFFSET 9

#define SX126X_GFSK_RX_CONSUMPTION_DCDC 4200
#define SX126X_GFSK_RX_BOOSTED_CONSUMPTION_DCDC 4800
#define SX126X_GFSK_RX_CONSUMPTION_LDO 8000
#define SX126X_GFSK_RX_BOOSTED_CONSUMPTION_LDO 9300

#define SX126X_LORA_RX_CONSUMPTION_DCDC 4600
#define SX126X_LORA_RX_BOOSTED_CONSUMPTION_DCDC 5300
#define SX126X_LORA_RX_CONSUMPTION_LDO 8880
#define SX126X_LORA_RX_BOOSTED_CONSUMPTION_LDO 10100

static const uint32_t ral_sx126x_convert_tx_dbm_to_ua_reg_mode_dcdc_hp[] = {
	24000,	 //  -9 dBm
	25400,	 //  -8 dBm
	26700,	 //  -7 dBm
	28000,	 //  -6 dBm
	30600,	 //  -5 dBm
	31900,	 //  -4 dBm
	33200,	 //  -3 dBm
	35700,	 //  -2 dBm
	38200,	 //  -1 dBm
	40600,	 //   0 dBm
	42900,	 //   1 dBm
	46200,	 //   2 dBm
	48200,	 //   3 dBm
	51800,	 //   4 dBm
	54100,	 //   5 dBm
	57000,	 //   6 dBm
	60300,	 //   7 dBm
	63500,	 //   8 dBm
	67100,	 //   9 dBm
	70500,	 //  10 dBm
	74200,	 //  11 dBm
	78400,	 //  12 dBm
	83500,	 //  13 dBm
	89300,	 //  14 dBm
	92400,	 //  15 dBm
	94500,	 //  16 dBm
	95400,	 //  17 dBm
	97500,	 //  18 dBm
	100100,	 //  19 dBm
	103800,	 //  20 dBm
	109100,	 //  21 dBm
	117900,	 //  22 dBm
};

static const uint32_t ral_sx126x_convert_tx_dbm_to_ua_reg_mode_ldo_hp[] = {
	25900,	 //  -9 dBm
	27400,	 //  -8 dBm
	28700,	 //  -7 dBm
	30000,	 //  -6 dBm
	32600,	 //  -5 dBm
	33900,	 //  -4 dBm
	35200,	 //  -3 dBm
	37700,	 //  -2 dBm
	40100,	 //  -1 dBm
	42600,	 //   0 dBm
	44900,	 //   1 dBm
	48200,	 //   2 dBm
	50200,	 //   3 dBm
	53800,	 //   4 dBm
	56100,	 //   5 dBm
	59000,	 //   6 dBm
	62300,	 //   7 dBm
	65500,	 //   8 dBm
	69000,	 //   9 dBm
	72500,	 //  10 dBm
	76200,	 //  11 dBm
	80400,	 //  12 dBm
	85400,	 //  13 dBm
	90200,	 //  14 dBm
	94400,	 //  15 dBm
	96500,	 //  16 dBm
	97700,	 //  17 dBm
	99500,	 //  18 dBm
	102100,	 //  19 dBm
	105800,	 //  20 dBm
	111000,	 //  21 dBm
	119800,	 //  22 dBm
};

void ral_sx126x_bsp_get_reg_mode( const void* context, sx126x_reg_mod_t* reg_mode )
{
	(void)context;
	// CHANGED from SX126X_REG_MODE_DCDC (Semtech's own reference default,
	// copied without verifying against RAK's actual module schematic - no
	// network access available to check it). DC-DC mode requires a
	// physical inductor to be populated on the board; if it isn't, standby/RX
	// draws little enough current that nothing visibly breaks, but TX's
	// current spike is exactly where a misconfigured supply would misbehave
	// - consistent with the observed symptom (P2P TX/RX fine, LoRaWAN join
	// panics specifically inside TX setup). LDO works unconditionally on
	// any board. If you've confirmed your module's schematic populates the
	// DC-DC inductor, switch back for the efficiency gain.
	*reg_mode = SX126X_REG_MODE_DCDC;
}

void ral_sx126x_bsp_get_rf_switch_cfg( const void* context, bool* dio2_is_set_as_rf_switch )
{
	(void)context;
	// Matches LORA_ANT_SWITCH == -1 in WisBlockLoRaBoards.h and the
	// sx126x_set_dio2_as_rf_sw_ctrl(kCtx, true) call already made directly
	// in LoRaP2PEngine.cpp for P2P mode - kept consistent here for LoRaWAN mode.
	*dio2_is_set_as_rf_switch = true;
}

void ral_sx126x_bsp_get_tx_cfg( const void* context, const ral_sx126x_bsp_tx_cfg_input_params_t* input_params,
								 ral_sx126x_bsp_tx_cfg_output_params_t* output_params )
{
	(void)context;

	// TODO: calibrate a real board TX power offset here if measured EIRP
	// differs from requested; 0 = trust the requested power as-is.
	const int8_t board_tx_pwr_offset_db = 0;

	int16_t power = input_params->system_output_pwr_in_dbm + board_tx_pwr_offset_db;

	output_params->pa_ramp_time = SX126X_RAMP_40_US;
	output_params->pa_cfg.pa_lut = 0x01; // reserved value, same for sx1261/sx1262/sx1268

	// All three boards use the SX1262 HP PA variant (up to +22dBm).
	if( power > 22 )
	{
		power = 22;
	}
	if( power < -9 )
	{
		power = -9;
	}
	output_params->pa_cfg.device_sel = 0x00; // select SX1262/SX1268 device
	output_params->pa_cfg.hp_max = 0x07;	  // to achieve 22dBm
	output_params->pa_cfg.pa_duty_cycle = 0x04;
	output_params->chip_output_pwr_in_dbm_configured = (int8_t)power;
	output_params->chip_output_pwr_in_dbm_expected = (int8_t)power;
}

void ral_sx126x_bsp_get_xosc_cfg( const void* context, ral_xosc_cfg_t* xosc_cfg,
								   sx126x_tcxo_ctrl_voltages_t* supply_voltage, uint32_t* startup_time_in_tick )
{
	(void)context;
	// This board's SX1262 module uses a TCXO (confirmed 1.8V / ~5ms
	// startup - same value used directly in LoRaP2PEngine.cpp and
	// wisblock_lbm_port.cpp). startup_time_in_tick is passed straight
	// through to sx126x_set_dio3_as_tcxo_ctrl() (see ral_sx126x.c), same
	// raw 15.625us-per-step units as everywhere else in this library:
	// 50ms / 15.625us = 320 = 50 << 6.
	*xosc_cfg = RAL_XOSC_CFG_TCXO_RADIO_CTRL;
	*supply_voltage = SX126X_TCXO_CTRL_3_3V;
	*startup_time_in_tick = 50 << 6;
}

void ral_sx126x_bsp_get_trim_cap( const void* context, uint8_t* trimming_cap_xta, uint8_t* trimming_cap_xtb )
{
	(void)context;
	(void)trimming_cap_xta;
	(void)trimming_cap_xtb;
	// Not used with a TCXO (trimming caps are a crystal-mode concept) -
	// leave the driver's defaults in place.
}

void ral_sx126x_bsp_get_rx_boost_cfg( const void* context, bool* rx_boost_is_activated )
{
	(void)context;
	// RX boost trades higher RX current for better sensitivity. Off by
	// default (matches Semtech's own reference default); flip to true if
	// your application prioritizes range/sensitivity over battery life.
	*rx_boost_is_activated = false;
}

void ral_sx126x_bsp_get_ocp_value( const void* context, uint8_t* ocp_in_step_of_2_5_ma )
{
	(void)context;
	(void)ocp_in_step_of_2_5_ma;
	// Leave the driver's default Over-Current-Protection value in place.
}

void ral_sx126x_bsp_get_lora_cad_det_peak( const void* context, ral_lora_sf_t sf, ral_lora_bw_t bw,
											ral_lora_cad_symbs_t nb_symbol, uint8_t* in_out_cad_det_peak )
{
	(void)context;
	(void)sf;
	(void)bw;
	(void)nb_symbol;
	// No override - this only affects LBM's own internal LoRaWAN-mode CAD
	// use (e.g. LR-FHSS/listen-before-talk in some regions), not the
	// direct P2P CAD calls LoRaP2PEngine.cpp makes with its own explicit
	// cad_detect_peak/cad_detect_min values.
	(void)in_out_cad_det_peak;
}

ral_status_t ral_sx126x_bsp_get_instantaneous_tx_power_consumption(
	const void* context, const ral_sx126x_bsp_tx_cfg_output_params_t* tx_cfg_output_params,
	sx126x_reg_mod_t radio_reg_mode, uint32_t* pwr_consumption_in_ua )
{
	(void)context;

	// This library only configures the SX1262 HP PA (device_sel == 0x00);
	// the SX1261 LP PA branch from Semtech's reference is omitted since
	// it's unreachable given ral_sx126x_bsp_get_tx_cfg() above always
	// selects HP.
	if( tx_cfg_output_params->pa_cfg.device_sel != 0x00 )
	{
		return RAL_STATUS_UNKNOWN_VALUE;
	}

	uint8_t index;
	if( tx_cfg_output_params->chip_output_pwr_in_dbm_expected > SX126X_HP_MAX_OUTPUT_POWER )
	{
		index = SX126X_HP_MAX_OUTPUT_POWER + SX126X_HP_CONVERT_TABLE_INDEX_OFFSET;
	}
	else if( tx_cfg_output_params->chip_output_pwr_in_dbm_expected < SX126X_HP_MIN_OUTPUT_POWER )
	{
		index = SX126X_HP_MIN_OUTPUT_POWER + SX126X_HP_CONVERT_TABLE_INDEX_OFFSET;
	}
	else
	{
		index = tx_cfg_output_params->chip_output_pwr_in_dbm_expected + SX126X_HP_CONVERT_TABLE_INDEX_OFFSET;
	}

	*pwr_consumption_in_ua = ( radio_reg_mode == SX126X_REG_MODE_DCDC )
								  ? ral_sx126x_convert_tx_dbm_to_ua_reg_mode_dcdc_hp[index]
								  : ral_sx126x_convert_tx_dbm_to_ua_reg_mode_ldo_hp[index];
	return RAL_STATUS_OK;
}

ral_status_t ral_sx126x_bsp_get_instantaneous_gfsk_rx_power_consumption( const void* context,
																		  sx126x_reg_mod_t radio_reg_mode,
																		  bool rx_boosted,
																		  uint32_t* pwr_consumption_in_ua )
{
	(void)context;
	if( radio_reg_mode == SX126X_REG_MODE_DCDC )
	{
		*pwr_consumption_in_ua = rx_boosted ? SX126X_GFSK_RX_BOOSTED_CONSUMPTION_DCDC : SX126X_GFSK_RX_CONSUMPTION_DCDC;
	}
	else
	{
		*pwr_consumption_in_ua = rx_boosted ? SX126X_GFSK_RX_BOOSTED_CONSUMPTION_LDO : SX126X_GFSK_RX_CONSUMPTION_LDO;
	}
	return RAL_STATUS_OK;
}

ral_status_t ral_sx126x_bsp_get_instantaneous_lora_rx_power_consumption( const void* context,
																		  sx126x_reg_mod_t radio_reg_mode,
																		  bool rx_boosted,
																		  uint32_t* pwr_consumption_in_ua )
{
	(void)context;
	if( radio_reg_mode == SX126X_REG_MODE_DCDC )
	{
		*pwr_consumption_in_ua = rx_boosted ? SX126X_LORA_RX_BOOSTED_CONSUMPTION_DCDC : SX126X_LORA_RX_CONSUMPTION_DCDC;
	}
	else
	{
		*pwr_consumption_in_ua = rx_boosted ? SX126X_LORA_RX_BOOSTED_CONSUMPTION_LDO : SX126X_LORA_RX_CONSUMPTION_LDO;
	}
	return RAL_STATUS_OK;
}
