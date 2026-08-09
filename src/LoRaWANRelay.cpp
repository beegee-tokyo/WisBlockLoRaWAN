#include "LoRaWANRelay.h"

#include "smtc_modem_relay_api.h" // vendored: src/lbm/smtc_modem_api/smtc_modem_relay_api.h (public relay TX API)

// Internal relay RX path - see LoRaWANRelay.h header comment for why this
// isn't a public smtc_modem_api call, and wisblock_relay_rx_bridge.h for
// why it's reached through a C bridge rather than included directly here
// (LBM's internal headers use GNU C syntax G++ can't parse). Only
// functional if LBM was built with ADD_RELAY_RX defined (matches the guard
// smtc_modem.c itself uses at src/lbm/smtc_modem_core/smtc_modem.c ~line
// 109) - the bridge falls back to harmless no-op stubs otherwise.
#include "wisblock_relay_rx_bridge.h"

namespace
{
constexpr uint8_t kStackId = 0;

WisBlockRelayMode activeMode = WISBLOCK_RELAY_OFF;
WisBlockRelayEDConfig edConfig;
WisBlockRelayServingConfig servingConfig;
bool relayRxInitialized = false;
} // namespace

namespace LoRaWANRelay
{
void configure(WisBlockRelayMode mode)
{
	activeMode = mode;
	switch (mode)
	{
	case WISBLOCK_RELAY_OFF:
		smtc_modem_relay_tx_disable(kStackId);
		if (relayRxInitialized)
		{
			// false = permanent stop, as opposed to the `true` (temporary,
			// "pausing to forward an uplink") case relay_stop() also
			// supports internally - we want the former when the app
			// explicitly switches relay mode off.
			wisblock_relay_rx_stop(false);
		}
		break;

	case WISBLOCK_RELAY_ED:
	{
		smtc_modem_relay_tx_config_t config = {};
		config.activation = (smtc_modem_relay_tx_activation_mode_t)edConfig.activationMode;
		config.smart_level = edConfig.smartLevel;
		config.backoff = edConfig.backoff;
		config.number_of_miss_wor_ack_to_switch_in_nosync_mode = edConfig.missedWorAckToNoSync;
		config.second_ch_enable = edConfig.secondChannelEnable;
		config.second_ch.freq_hz = edConfig.secondChannelFreqHz;
		config.second_ch.ack_freq_hz = edConfig.secondChannelAckFreqHz;
		config.second_ch.dr = edConfig.secondChannelDr;
		smtc_modem_relay_tx_enable(kStackId, &config);
		break;
	}

	case WISBLOCK_RELAY_SERVING:
	{
		if (!relayRxInitialized)
		{
			// UNVERIFIED AGAINST REAL HARDWARE - see LoRaWANRelay.h header
			// comment. Only call wisblock_relay_rx_init() once (per
			// relay_init()'s own doc comment). Returns false as a harmless
			// no-op if LBM wasn't built with ADD_RELAY_RX defined.
			relayRxInitialized =
				wisblock_relay_rx_init(kStackId, servingConfig.errorPpm, servingConfig.cadToRxSymb);
		}

		wisblock_relay_rx_update_config(servingConfig.cadPeriod, servingConfig.channelFreqHz,
										 servingConfig.channelAckFreqHz, servingConfig.channelDr);
		wisblock_relay_rx_start();

		// Reminder: starting the relay alone forwards nothing until at
		// least one trusted device is registered - see addTrustedDevice() /
		// AT+RELAYDEV.
		break;
	}
	}
}

void configureED(const WisBlockRelayEDConfig &cfg)
{
	edConfig = cfg;
	if (activeMode == WISBLOCK_RELAY_ED)
	{
		configure(activeMode); // re-apply
	}
}

void configureServing(const WisBlockRelayServingConfig &cfg)
{
	servingConfig = cfg;
	if (activeMode == WISBLOCK_RELAY_SERVING)
	{
		configure(activeMode);
	}
}

bool addTrustedDevice(const WisBlockRelayTrustedDevice &device)
{
	if (!relayRxInitialized)
	{
		return false;
	}
	return wisblock_relay_rx_add_device(device.index, device.devAddr, device.rootWorSKey, device.unlimitedForward,
										 device.bucketFactor, device.reloadRate);
}

bool removeTrustedDevice(uint8_t index)
{
	if (!relayRxInitialized)
	{
		return false;
	}
	return wisblock_relay_rx_remove_device(index);
}

WisBlockRelayMode currentMode()
{
	return activeMode;
}
} // namespace LoRaWANRelay
