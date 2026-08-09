/**
 * @file wisblock_relay_rx_bridge.c
 * @brief Plain-C implementation of the relay RX bridge - see the header
 * comment in wisblock_relay_rx_bridge.h for why this needs to be a .c file,
 * not a .cpp file.
 */
#include "wisblock_relay_rx_bridge.h"

#if defined(ADD_RELAY_RX)

#include "lorawan_api.h"   // vendored: src/lbm/smtc_modem_core/lorawan_api/ - lorawan_api_stack_mac_get()
#include "relay_rx_api.h"  // vendored: src/lbm/smtc_modem_core/lr1mac/src/relay/relay_rx/
#include "wake_on_radio.h" // vendored: src/lbm/smtc_modem_core/lr1mac/src/relay/ - wor_ack_ppm_error_t, wor_ack_cad_to_rx_t, wor_cad_periodicity_t

bool wisblock_relay_rx_init(uint8_t stack_id, uint8_t error_ppm, uint8_t cad_to_rx)
{
	// UNVERIFIED AGAINST REAL HARDWARE - see LoRaWANRelay.h. relay_init()
	// needs the internal lr1_stack_mac_t* handle, reached via the
	// undocumented-for-this-purpose lorawan_api_stack_mac_get().
	lr1_stack_mac_t *mac = lorawan_api_stack_mac_get(stack_id);
	return relay_init(mac, (wor_ack_ppm_error_t)error_ppm, (wor_ack_cad_to_rx_t)cad_to_rx);
}

void wisblock_relay_rx_stop(bool stop_to_fwd)
{
	relay_stop(stop_to_fwd);
}

void wisblock_relay_rx_start(void)
{
	relay_start();
}

void wisblock_relay_rx_update_config(uint8_t cad_period, uint32_t channel_freq_hz, uint32_t channel_ack_freq_hz,
									  uint8_t channel_dr)
{
	relay_config_t config = {0};
	config.cad_period = (wor_cad_periodicity_t)cad_period;
	config.nb_wor_channel = 1; // this library only exposes one WOR channel via the API/AT layer; MAX_WOR_CH is 2
	config.channel_cfg[0].freq_hz = channel_freq_hz;
	config.channel_cfg[0].ack_freq_hz = channel_ack_freq_hz;
	config.channel_cfg[0].dr = channel_dr;
	relay_update_config(&config);
}

bool wisblock_relay_rx_add_device(uint8_t index, uint32_t dev_addr, const uint8_t root_wor_skey[16],
								   bool unlimited_forward, uint8_t bucket_factor, uint8_t reload_rate)
{
	if (index > 15)
	{
		return false;
	}
	return relay_fwd_uplink_add_device(index, dev_addr, root_wor_skey, unlimited_forward, bucket_factor, reload_rate,
										0 /* wfcnt32: start at 0 */);
}

bool wisblock_relay_rx_remove_device(uint8_t index)
{
	if (index > 15)
	{
		return false;
	}
	return relay_fwd_uplink_remove_device(index);
}

#else // !ADD_RELAY_RX - stub implementations so the bridge always links regardless of build config

bool wisblock_relay_rx_init(uint8_t stack_id, uint8_t error_ppm, uint8_t cad_to_rx)
{
	(void)stack_id;
	(void)error_ppm;
	(void)cad_to_rx;
	return false;
}

void wisblock_relay_rx_stop(bool stop_to_fwd)
{
	(void)stop_to_fwd;
}

void wisblock_relay_rx_start(void)
{
}

void wisblock_relay_rx_update_config(uint8_t cad_period, uint32_t channel_freq_hz, uint32_t channel_ack_freq_hz,
									  uint8_t channel_dr)
{
	(void)cad_period;
	(void)channel_freq_hz;
	(void)channel_ack_freq_hz;
	(void)channel_dr;
}

bool wisblock_relay_rx_add_device(uint8_t index, uint32_t dev_addr, const uint8_t root_wor_skey[16],
								   bool unlimited_forward, uint8_t bucket_factor, uint8_t reload_rate)
{
	(void)index;
	(void)dev_addr;
	(void)root_wor_skey;
	(void)unlimited_forward;
	(void)bucket_factor;
	(void)reload_rate;
	return false;
}

bool wisblock_relay_rx_remove_device(uint8_t index)
{
	(void)index;
	return false;
}

#endif // ADD_RELAY_RX
