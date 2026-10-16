/**
 * @file wisblock_ral_lr20xx_bsp.c
 * @brief Board Support Package for LBM's LR20xx RAL (Radio Abstraction Layer): the `ral_lr20xx_bsp_*`
 * callbacks `ral_lr20xx.c` calls for board specific radio configuration (regulator mode, DIO functions,
 * RF switch, TX power amplifier, TCXO, 32 kHz clock, RX path).
 *
 * This is the LR2021 counterpart of wisblock_ral_sx126x_bsp.c, used for the RAK13702 (Semtech LR2021)
 * module on a RAK3401 core. It is only compiled when WISBLOCK_ENABLE_LR2021 is defined, see
 * wb_lbm_config.h. The SPI/GPIO transport (lr20xx_hal_*) lives in wisblock_radio_hal.cpp.
 *
 * Board data used here (RAK13702 on RAK3401), taken from the request that added this file:
 *  - IRQ line: DIO8 of the LR2021, wired to the MCU pin that is DIO1 on the RAK13300 (WB_IO6)
 *  - 2.4 GHz antenna switch (RFI_HF): controlled by DIO9
 *  - sub-GHz antenna switch (RF_LF1 and RF_LF2, tied together): controlled by DIO7
 *  - both antenna switches are powered from WB_IO3 (see useRxenAntPwr in WisBlockLoRaHwConfig.h)
 *  - TCXO 3.3 V / 50 ms, supplied by the VTCXO pin of the LR2021
 *  - 32.768 kHz crystal between DIO10 and DIO11
 *  - DC-DC regulator
 * The DIO numbers, regulator and TCXO values are read from wisblock_radio_hal_get_bsp_config(), they
 * come from the WisBlockLoRaHwConfig of the board, see wisblockLoRaHwConfigRAK3401_RAK13702().
 *
 * NOT VERIFIED ON HARDWARE, please check with the RAK13702:
 *  1. RF switch truth table. Assumed to behave like DIO2 of an SX1262: the DIO of a switch is high
 *     while transmitting in its band and low in standby and receive.
 *  2. TX power amplifier settings, see ral_lr20xx_bsp_get_tx_cfg() below. The LR2021 datasheet says
 *     the PA parameters depend on the matching network of the board. The table below is a placeholder.
 */

/* WisBlockLoRaWAN: build flags for the Arduino IDE, see wb_lbm_config.h */
#include "wb_lbm_config.h"

#if defined(WISBLOCK_ENABLE_LR2021)

#include "lbm/smtc_modem_core/smtc_ral/src/ral_lr20xx_bsp.h" // vendored: src/lbm/smtc_modem_core/smtc_ral/src/ral_lr20xx_bsp.h
#include "lbm/smtc_modem_core/smtc_ralf/src/ralf_lr20xx.h" // vendored: src/lbm/smtc_modem_core/smtc_ralf/src/ralf_lr20xx.h
#include "wisblock_radio_bsp_config.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// --- Frequency limit between the two RF paths ------------------------------------------------------
// The low frequency path (LF PA / LF RX) covers 150-960 MHz, the high frequency path 1500-2500 MHz
#define LR2021_HF_PATH_MIN_FREQ_HZ 1500000000UL

// --- TX power ranges of the two power amplifiers (lr20xx_radio_common_set_tx_params() doc) --------------
#define LR2021_LF_MIN_PWR_DBM (-9)
#define LR2021_LF_MAX_PWR_DBM 22
#define LR2021_HF_MIN_PWR_DBM (-19)
#define LR2021_HF_MAX_PWR_DBM 12

// --- TX power amplifier settings (PLACEHOLDER) ---------------------------------------------------------
// The LR2021 datasheet (section "Low Frequency Power Amplifier") says the output power is controlled by
// the PA conduction angle (pa_lf_duty_cycle), the PA cell size (pa_lf_slices) and the TX power value, and
// that a set of optimized values "can be defined to reach the targeted Tx power for each application
// PCB". Datasheet tables 7-16 / 7-17 list the values for the 490 MHz and 915 MHz reference designs.
// Those depend on the matching network of the board, so the values of another board must not be copied
// without measuring.
//
// TODO (before relying on the output power): fill kLfPa[] with the values for the RAK13702 and check the
// result with a power meter. The row for X dBm is kLfPa[X - LR2021_LF_MIN_PWR_DBM]. Until then every row
// has the defaults the driver documents for these fields (duty cycle 6, slices 7) and the TX power
// value is passed to the chip unchanged, so the real output power can differ from the requested one.
//
// The LF PA mode is also an assumption: RF_LF1 and RF_LF2 of the RAK13702 are connected to each other,
// which is taken as full single-ended mode. The other modes are LR20XX_RADIO_COMMON_PA_LF_MODE_FDM
// (differential) and ..._HSM_RFO_LF1 / ..._HSM_RFO_LF2 (half PA on one output).
#define LR2021_LF_PA_MODE LR20XX_RADIO_COMMON_PA_LF_MODE_FSM

typedef struct
{
	uint8_t duty_cycle;
	uint8_t slices;
} lr2021_lf_pa_row_t;

static const lr2021_lf_pa_row_t kLfPa[LR2021_LF_MAX_PWR_DBM - LR2021_LF_MIN_PWR_DBM + 1] = {
	{6, 7}, {6, 7}, {6, 7}, {6, 7}, {6, 7}, {6, 7}, {6, 7}, {6, 7}, // -9 ... -2 dBm
	{6, 7}, {6, 7}, {6, 7}, {6, 7}, {6, 7}, {6, 7}, {6, 7}, {6, 7}, // -1 ...  6 dBm
	{6, 7}, {6, 7}, {6, 7}, {6, 7}, {6, 7}, {6, 7}, {6, 7}, {6, 7}, //  7 ... 14 dBm
	{6, 7}, {6, 7}, {6, 7}, {6, 7}, {6, 7}, {6, 7}, {6, 7},		   // 15 ... 21 dBm
	{6, 7},															   // 22 dBm
};

// High frequency PA duty cycle, the default the driver documents for this field
#define LR2021_HF_PA_DUTY_CYCLE 16

// RAL of the LR2021 for LoRaP2PEngineLR2021.cpp (a C++ file cannot use the designated initializers of the
// RALF_LR20XX_INSTANTIATE macro). Same as the one LBM instantiates in smtc_modem.c, the context is unused.
static const ralf_t kLr2021Ralf = RALF_LR20XX_INSTANTIATE(NULL);

const ral_t *wisblock_lr20xx_get_ral(void)
{
	return &kLr2021Ralf.ral;
}

/** @brief Board configuration, same instance as used by the SX126x BSP */
static const struct WisBlockLoRaRadioBspConfig *cfg(void)
{
	return wisblock_radio_hal_get_bsp_config();
}

/** @brief Check if a DIO of the chip is the one configured as DIO number n in the board config */
static bool is_dio(lr20xx_system_dio_t dio, int8_t n)
{
	return n >= 5 && (int)dio == (int)n;
}

void ral_lr20xx_bsp_get_reg_mode(const void *context, lr20xx_system_reg_mode_t *reg_mode)
{
	(void)context;
	*reg_mode = cfg()->useLdo ? LR20XX_SYSTEM_REG_MODE_LDO : LR20XX_SYSTEM_REG_MODE_DCDC;
}

void ral_bsp_lr20xx_get_lfclk_cfg(const void *context, lr20xx_system_lfclk_cfg_t *lfclk_cfg)
{
	(void)context;
	// 32.768 kHz crystal between DIO10 and DIO11. DIO10/DIO11 must have no other function, see
	// ral_lr20xx_bsp_get_dio_function()
	*lfclk_cfg = LR20XX_SYSTEM_LFCLK_XTAL;
}

void ral_lr20xx_bsp_get_xosc_cfg(const void *context, ral_xosc_cfg_t *xosc_cfg,
								 lr20xx_system_tcxo_supply_voltage_t *tcxo_supply_voltage,
								 uint32_t *startup_time_in_tick)
{
	(void)context;
	// TCXO supplied by the VTCXO pin of the chip. The start-up time is in steps of 31.25 ns
	// (lr20xx_system_set_tcxo_mode()), 50 ms = 1 600 000 steps, computed in wisblock_radio_hal.cpp
	*xosc_cfg = RAL_XOSC_CFG_TCXO_RADIO_CTRL;
	switch (cfg()->tcxoVoltage)
	{
	case SX126X_TCXO_CTRL_1_6V:
		*tcxo_supply_voltage = LR20XX_SYSTEM_TCXO_CTRL_1_6V;
		break;
	case SX126X_TCXO_CTRL_1_7V:
		*tcxo_supply_voltage = LR20XX_SYSTEM_TCXO_CTRL_1_7V;
		break;
	case SX126X_TCXO_CTRL_1_8V:
		*tcxo_supply_voltage = LR20XX_SYSTEM_TCXO_CTRL_1_8V;
		break;
	case SX126X_TCXO_CTRL_2_2V:
		*tcxo_supply_voltage = LR20XX_SYSTEM_TCXO_CTRL_2_2V;
		break;
	case SX126X_TCXO_CTRL_2_4V:
		*tcxo_supply_voltage = LR20XX_SYSTEM_TCXO_CTRL_2_4V;
		break;
	case SX126X_TCXO_CTRL_2_7V:
		*tcxo_supply_voltage = LR20XX_SYSTEM_TCXO_CTRL_2_7V;
		break;
	case SX126X_TCXO_CTRL_3_0V:
		*tcxo_supply_voltage = LR20XX_SYSTEM_TCXO_CTRL_3_0V;
		break;
	case SX126X_TCXO_CTRL_3_3V:
	default:
		*tcxo_supply_voltage = LR20XX_SYSTEM_TCXO_CTRL_3_3V;
		break;
	}
	*startup_time_in_tick = cfg()->tcxoStartupTimeIn32MHzStep;
}

void ral_lr20xx_bsp_get_dio_function(const void *context, lr20xx_system_dio_t dio, lr20xx_system_dio_func_t *function)
{
	(void)context;
	if (is_dio(dio, cfg()->lrIrqDio))
	{
		*function = LR20XX_SYSTEM_DIO_FUNC_IRQ;
	}
	else if (is_dio(dio, cfg()->lrRfSwitchDioLf) || is_dio(dio, cfg()->lrRfSwitchDioHf))
	{
		*function = LR20XX_SYSTEM_DIO_FUNC_RF_SWITCH;
	}
	else
	{
		// Includes DIO10 and DIO11 (32 kHz crystal)
		*function = LR20XX_SYSTEM_DIO_FUNC_NONE;
	}
}

void ral_lr20xx_bsp_get_dio_sleep_drive(const void *context, lr20xx_system_dio_t dio, lr20xx_system_dio_drive_t *drive)
{
	(void)context;
	// The IRQ line is pulled down on the MCU side as well, the switch DIOs are low in sleep so the
	// antenna switches do not draw current. Everything else is left alone.
	if (is_dio(dio, cfg()->lrIrqDio) || is_dio(dio, cfg()->lrRfSwitchDioLf) || is_dio(dio, cfg()->lrRfSwitchDioHf))
	{
		*drive = LR20XX_SYSTEM_DIO_DRIVE_PULL_DOWN;
	}
	else
	{
		*drive = LR20XX_SYSTEM_DIO_DRIVE_NONE;
	}
}

void ral_lr20xx_bsp_get_dio_irq_mask(const void *context, lr20xx_system_dio_t dio, lr20xx_system_irq_mask_t *irq_mask)
{
	(void)context;
	// ral_lr20xx_set_dio_irq_params() fails if the masks of all IRQ DIOs together do not cover the
	// requested IRQs, so the single IRQ DIO takes all of them
	*irq_mask = is_dio(dio, cfg()->lrIrqDio) ? LR20XX_SYSTEM_IRQ_ALL_MASK : LR20XX_SYSTEM_IRQ_NONE;
}

void ral_lr20xx_bsp_get_dio_rf_switch_cfg(const void *context, lr20xx_system_dio_t dio,
										  lr20xx_system_dio_rf_switch_cfg_t *rf_switch_cfg)
{
	(void)context;
	// ASSUMPTION, see the top of this file: like DIO2 of an SX1262, a switch DIO is high while
	// transmitting in its band and low in standby and receive.
	if (is_dio(dio, cfg()->lrRfSwitchDioLf))
	{
		*rf_switch_cfg = LR20XX_SYSTEM_DIO_RF_SWITCH_WHEN_TX_LF;
	}
	else if (is_dio(dio, cfg()->lrRfSwitchDioHf))
	{
		*rf_switch_cfg = LR20XX_SYSTEM_DIO_RF_SWITCH_WHEN_TX_HF;
	}
	else
	{
		*rf_switch_cfg = 0;
	}
}

void ral_lr20xx_bsp_get_dio_hf_clk_scaling_cfg(const void *context, lr20xx_system_hf_clk_scaling_t *hf_clk_scaling)
{
	(void)context;
	// No DIO of this board outputs the HF clock, the value is not used
	*hf_clk_scaling = LR20XX_SYSTEM_HF_CLK_SCALING_32_MHZ;
}

void ral_lr20xx_bsp_get_rx_cfg(const void *context, const uint32_t freq_in_hz, lr20xx_radio_common_rx_path_t *rx_path,
							   lr20xx_radio_common_rx_path_boost_mode_t *boost_mode)
{
	(void)context;
	*rx_path = (freq_in_hz >= LR2021_HF_PATH_MIN_FREQ_HZ) ? LR20XX_RADIO_COMMON_RX_PATH_HF : LR20XX_RADIO_COMMON_RX_PATH_LF;
	// RX boost is not enabled, the RAL has no function to select it for the receiver
	*boost_mode = LR20XX_RADIO_COMMON_RX_PATH_BOOST_MODE_NONE;
}

void ral_lr20xx_bsp_get_front_end_calibration_cfg(const void *context,
												  lr20xx_radio_common_front_end_calibration_value_t front_end_calibration_structures[3])
{
	(void)context;
	// Calibrated once when the radio is initialized: the two sub-GHz bands the LoRaWAN regions of this
	// library use most (EU868, US915/AU915) and the middle of the 2.4 GHz band
	front_end_calibration_structures[0].rx_path = LR20XX_RADIO_COMMON_RX_PATH_LF;
	front_end_calibration_structures[0].frequency_in_hertz = 868000000UL;
	front_end_calibration_structures[1].rx_path = LR20XX_RADIO_COMMON_RX_PATH_LF;
	front_end_calibration_structures[1].frequency_in_hertz = 915000000UL;
	front_end_calibration_structures[2].rx_path = LR20XX_RADIO_COMMON_RX_PATH_HF;
	front_end_calibration_structures[2].frequency_in_hertz = 2440000000UL;
}

void ral_lr20xx_bsp_get_tx_cfg(const void *context, const ral_lr20xx_bsp_tx_cfg_input_params_t *input_params,
							   ral_lr20xx_bsp_tx_cfg_output_params_t *output_params)
{
	(void)context;
	int8_t power = input_params->system_output_pwr_in_dbm; // no board loss offset

	// Defaults of the fields of the PA that is not used, see lr20xx_radio_common_pa_cfg_t
	output_params->pa_cfg.pa_lf_mode = LR20XX_RADIO_COMMON_PA_LF_MODE_FSM;
	output_params->pa_cfg.pa_lf_duty_cycle = 6;
	output_params->pa_cfg.pa_lf_slices = 7;
	output_params->pa_cfg.pa_hf_duty_cycle = LR2021_HF_PA_DUTY_CYCLE;

	if (input_params->freq_in_hz >= LR2021_HF_PATH_MIN_FREQ_HZ)
	{
		if (power > LR2021_HF_MAX_PWR_DBM)
		{
			power = LR2021_HF_MAX_PWR_DBM;
		}
		if (power < LR2021_HF_MIN_PWR_DBM)
		{
			power = LR2021_HF_MIN_PWR_DBM;
		}
		output_params->pa_cfg.pa_sel = LR20XX_RADIO_COMMON_PA_SEL_HF;
	}
	else
	{
		if (power > LR2021_LF_MAX_PWR_DBM)
		{
			power = LR2021_LF_MAX_PWR_DBM;
		}
		if (power < LR2021_LF_MIN_PWR_DBM)
		{
			power = LR2021_LF_MIN_PWR_DBM;
		}
		output_params->pa_cfg.pa_sel = LR20XX_RADIO_COMMON_PA_SEL_LF;
		output_params->pa_cfg.pa_lf_mode = LR2021_LF_PA_MODE;
		output_params->pa_cfg.pa_lf_duty_cycle = kLfPa[power - LR2021_LF_MIN_PWR_DBM].duty_cycle;
		output_params->pa_cfg.pa_lf_slices = kLfPa[power - LR2021_LF_MIN_PWR_DBM].slices;
	}

	output_params->pa_ramp_time = LR20XX_RADIO_COMMON_RAMP_48_US;
	output_params->chip_output_half_pwr_in_dbm_configured = (int8_t)(power * 2); // 0.5 dB steps
	output_params->chip_output_pwr_in_dbm_expected = power;
}

void ral_lr20xx_bsp_get_lora_cad_det_peak(const void *context, ral_lora_sf_t sf, ral_lora_cad_symbs_t nb_symbol,
										  uint8_t *in_out_cad_det_peak)
{
	// Keep the value the RAL proposes
	(void)context;
	(void)sf;
	(void)nb_symbol;
	(void)in_out_cad_det_peak;
}

// --- Power consumption estimates -------------------------------------------------------------------------
// Only used by LBM for its charge statistics (radio planner), not for the radio itself. Rough values
// from the typical figures of the LR2021 datasheet (sub-GHz, DC-DC): about 13 mA at +10 dBm, 27 mA at
// +14 dBm, 105 mA at +22 dBm, 5.7 mA receiving; 2.4 GHz: about 27.6 mA at +12 dBm.

ral_status_t ral_lr20xx_bsp_get_instantaneous_tx_power_consumption(const void *context,
																   const ral_lr20xx_bsp_tx_cfg_output_params_t *tx_cfg,
																   lr20xx_system_reg_mode_t radio_reg_mode,
																   uint32_t *pwr_consumption_in_ua)
{
	(void)context;
	(void)radio_reg_mode;
	const int8_t dbm = tx_cfg->chip_output_pwr_in_dbm_expected;
	if (tx_cfg->pa_cfg.pa_sel == LR20XX_RADIO_COMMON_PA_SEL_HF)
	{
		*pwr_consumption_in_ua = 27600;
	}
	else if (dbm <= 10)
	{
		*pwr_consumption_in_ua = 13000;
	}
	else if (dbm <= 14)
	{
		*pwr_consumption_in_ua = 13000 + (uint32_t)(dbm - 10) * 3500; // 10 dBm -> 13 mA, 14 dBm -> 27 mA
	}
	else
	{
		*pwr_consumption_in_ua = 27000 + (uint32_t)(dbm - 14) * 9750; // 14 dBm -> 27 mA, 22 dBm -> 105 mA
	}
	return RAL_STATUS_OK;
}

ral_status_t ral_lr20xx_bsp_get_instantaneous_gfsk_rx_power_consumption(const void *context,
																		lr20xx_system_reg_mode_t radio_reg_mode,
																		bool rx_boosted, uint32_t *pwr_consumption_in_ua)
{
	(void)context;
	(void)radio_reg_mode;
	(void)rx_boosted;
	*pwr_consumption_in_ua = 5700;
	return RAL_STATUS_OK;
}

ral_status_t ral_lr20xx_bsp_get_instantaneous_lora_rx_power_consumption(const void *context,
																		const lr20xx_system_reg_mode_t radio_reg_mode,
																		const ral_lora_bw_t bw, bool rx_boosted,
																		uint32_t *pwr_consumption_in_ua)
{
	(void)context;
	(void)radio_reg_mode;
	(void)bw;
	(void)rx_boosted;
	*pwr_consumption_in_ua = 5700;
	return RAL_STATUS_OK;
}

#endif /* WISBLOCK_ENABLE_LR2021 */
