/**
 * @file wisblock_radio_bsp_config.h
 * @brief Plain-C view of the active board's radio wiring, shared between
 * wisblock_radio_hal.cpp (C++, owns the actual WisBlockLoRaHwConfig - see
 * that struct's doc comment) and wisblock_ral_sx126x_bsp.c (a C file, part
 * of LoRa Basics Modem's porting layer, which cannot include a C++ header
 * with a SPIClass* member).
 *
 * wisblock_radio_hal.cpp fills this in once, from the WisBlockLoRaHwConfig
 * passed to WisBlockRadioHal::init(), and exposes it through
 * wisblock_radio_hal_get_bsp_config(). wisblock_ral_sx126x_bsp.c reads it on
 * every ral_sx126x_bsp_*() call instead of the hardcoded regulator/RF-switch/
 * TCXO constants it used before this - see the Creation Log entry "Flexible
 * hw_config-based radio init (RAK3401 / non-WisBlock boards)".
 */
#ifndef WISBLOCK_RADIO_BSP_CONFIG_H
#define WISBLOCK_RADIO_BSP_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#include "sx126x.h" // vendored: src/lbm/smtc_modem_core/radio_drivers/sx126x_driver/src/sx126x.h

#ifdef __cplusplus
extern "C"
{
#endif

	struct WisBlockLoRaRadioBspConfig
	{
		bool useLdo;			 ///< false (default) = DC-DC regulator, true = LDO
		bool dio2AntSwitch;		 ///< SX1262 DIO2 controls the antenna/RF switch
		bool dio3Tcxo;			 ///< SX1262 DIO3 controls the TCXO supply voltage
		sx126x_tcxo_ctrl_voltages_t tcxoVoltage;
		uint32_t tcxoStartupTimeInTick; ///< in the driver's native 15.625us steps, see ral_sx126x_bsp_get_xosc_cfg()
	};

	/**
	 * Returns the active board's radio BSP config, as last set by
	 * WisBlockRadioHal::init(). Only valid after that has run once (which
	 * WisBlockLoRaWAN::begin() always does before anything else touches the
	 * radio) - wisblock_radio_hal.cpp seeds it with the RAK4631 preset before
	 * the first init() so this is never read uninitialized either way.
	 */
	const struct WisBlockLoRaRadioBspConfig *wisblock_radio_hal_get_bsp_config( void );

#ifdef __cplusplus
}
#endif

#endif // WISBLOCK_RADIO_BSP_CONFIG_H
