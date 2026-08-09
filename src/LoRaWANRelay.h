/**
 * @file LoRaWANRelay.h
 * @brief Wraps the LoRaWAN Relay function (TS011 Relay Specification) as
 * implemented in LBM v4.9.0.
 *
 * VERIFIED against the real vendored source
 * (src/lbm/smtc_modem_core/lr1mac/src/relay/, src/lbm/smtc_modem_api/):
 *
 * - Relay TX (WISBLOCK_RELAY_ED - this device is an end-device relaying
 *   through a nearby relay) is a clean public API:
 *   `smtc_modem_relay_tx_enable/disable` + `smtc_modem_relay_tx_config_t`
 *   in `smtc_modem_relay_api.h`. Fully runtime-configurable.
 *
 * - Relay RX (WISBLOCK_RELAY_SERVING - this device *is* the serving relay)
 *   has no public smtc_modem_api entry point. Its implementation
 *   (`relay_rx_api.h`) is reached via `lorawan_api_stack_mac_get(stack_id)`
 *   (declared in `smtc_modem_core/lorawan_api/lorawan_api.h`) - see
 *   `relay_init()`. Compile-gated behind `ADD_RELAY_RX`.
 *
 * - IMPORTANT, easy to miss: starting the relay (`relay_start()`) is NOT
 *   sufficient for a serving relay to forward anything. Per TS011, the
 *   relay only forwards WOR traffic from end-devices it has been told to
 *   trust: each one's DevAddr + WOR root session key must be registered via
 *   `relay_fwd_uplink_add_device()` first (up to 16 devices, `idx` 0-15).
 *   There is no discovery/auto-trust mechanism - an un-registered
 *   end-device's WOR frames are silently ignored. See addTrustedDevice()
 *   below and the `AT+RELAYDEV` command in WisBlockLoRaAT.cpp.
 *
 * - `relay_init()`'s `error_ppm`/`cad_to_rx` and `relay_config_t`'s
 *   `cad_period` parameters are typed enums (`wor_ack_ppm_error_t`,
 *   `wor_ack_cad_to_rx_t`, `wor_cad_periodicity_t` in
 *   `lr1mac/src/relay/wake_on_radio.h`), not raw integers - passing raw
 *   numbers here was an earlier mistake, now fixed.
 */
#ifndef LORAWAN_RELAY_H
#define LORAWAN_RELAY_H

#include "WisBlockLoRaWANTypes.h" // WisBlockRelayMode, WisBlockRelayEDConfig, WisBlockRelayServingConfig, WisBlockRelayTrustedDevice
#include <stdint.h>

namespace LoRaWANRelay
{
/** Applies the given relay mode using the last config set via configureED()/configureServing(). */
void configure(WisBlockRelayMode mode);

void configureED(const WisBlockRelayEDConfig &cfg);
void configureServing(const WisBlockRelayServingConfig &cfg);

/** Registers (or overwrites) a trusted end-device slot. Only meaningful in WISBLOCK_RELAY_SERVING mode. */
bool addTrustedDevice(const WisBlockRelayTrustedDevice &device);
bool removeTrustedDevice(uint8_t index);

WisBlockRelayMode currentMode();
} // namespace LoRaWANRelay

#endif // LORAWAN_RELAY_H
