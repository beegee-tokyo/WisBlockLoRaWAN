/**
 * @file wisblock_relay_rx_bridge.h
 * @brief C-linkage bridge to LBM's internal relay RX (serving relay) API.
 *
 * Why this file exists: `lorawan_api.h`, `relay_rx_api.h`, and the headers
 * they pull in (e.g. `lr1mac_defs.h`) are LBM-internal headers never
 * designed for direct inclusion from C++ - `lr1mac_defs.h` specifically
 * uses GNU C's array designated-initializer syntax
 * (`[INDEX] = value` inside an array literal), which GCC's C front-end
 * supports as a long-standing extension but G++'s C++ front-end does not
 * support at all ("sorry, unimplemented: non-trivial designated
 * initializers not supported"). Semtech's *public* API
 * (`smtc_modem_api.h`, `smtc_modem_relay_api.h`) is properly wrapped for
 * C++ consumption; these internal ones are not.
 *
 * wisblock_relay_rx_bridge.c includes the problematic internal headers and
 * is compiled as plain C (where that syntax is completely normal), and
 * exposes only this small, C++-safe surface for LoRaWANRelay.cpp to call.
 * No LBM-internal types (lr1_stack_mac_t, relay_config_t, wor_ack_ppm_error_t,
 * ...) leak across this boundary - only plain integers/bools/pointers.
 */
#ifndef WISBLOCK_RELAY_RX_BRIDGE_H
#define WISBLOCK_RELAY_RX_BRIDGE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

	/**
	 * Calls lorawan_api_stack_mac_get() + relay_init(). Only call once (per
	 * relay_init()'s own doc comment). Returns false if ADD_RELAY_RX wasn't
	 * defined at build time (stub implementation) or if LBM's relay_init()
	 * itself failed.
	 */
	bool wisblock_relay_rx_init(uint8_t stack_id, uint8_t error_ppm, uint8_t cad_to_rx);

	/** false = permanent stop, true = temporary "pause to forward an uplink" (see relay_stop()'s own doc comment). */
	void wisblock_relay_rx_stop(bool stop_to_fwd);

	void wisblock_relay_rx_start(void);

	/** Single WOR channel only - this library's API/AT layer doesn't expose LBM's second optional channel. */
	void wisblock_relay_rx_update_config(uint8_t cad_period, uint32_t channel_freq_hz, uint32_t channel_ack_freq_hz,
										  uint8_t channel_dr);

	bool wisblock_relay_rx_add_device(uint8_t index, uint32_t dev_addr, const uint8_t root_wor_skey[16],
									   bool unlimited_forward, uint8_t bucket_factor, uint8_t reload_rate);

	bool wisblock_relay_rx_remove_device(uint8_t index);

#ifdef __cplusplus
}
#endif

#endif // WISBLOCK_RELAY_RX_BRIDGE_H
