/**
 * RAK3401_RAK13300.ino
 * Minimal OTAA join + periodic uplink, Class A, on a RAK3400 WisDuo module
 * used as a WisBlock Core module with a RAK13300 (or RAK13302, same wiring)
 * SX1262 transceiver module - i.e. "RAK3401" in the sense this library uses
 * the name (see WisBlockLoRaHwConfig.h).
 *
 * This is the same OTAA/join/uplink flow as BasicLoRaWAN.ino, the only
 * difference is this single line in setup():
 *
 *     lora.begin(wisblockLoRaHwConfigRAK3401());   // instead of plain lora.begin()
 *
 * Everything else - every other API call, the AT command layer if you add
 * it, the examples under examples/ - works exactly the same regardless of
 * which begin() you call, since it's only the radio wiring that differs.
 *
 * Only builds for nRF52840 targets (RAK3400 is an nRF52840 module) - see
 * WisBlockLoRaHwConfig.h and wisblock_radio_hal.h's "Board flexibility" doc
 * comment for why this flexible begin() overload doesn't exist for RP2040.
 */
#define RAK3401
#include <WisBlockLoRaWAN.h>

WisBlockLoRaWAN lora;

// Replace with your device's real OTAA credentials.
uint8_t devEui[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
uint8_t joinEui[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
uint8_t appKey[16] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
					  0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};

uint32_t lastUplinkMs = 0;
const uint32_t UPLINK_INTERVAL_MS = 60000;

void onJoined()
{
	Serial.println("[LoRaWAN] Join succeeded");
}

void onJoinFailed()
{
	// Don't call lora.join() here - a failed join is already retried
	// automatically (see setJoinReattemptInterval()/setMaxJoinAttempts() in
	// WisBlockLoRaWAN-API.md if you want different timing than the default).
	Serial.println("[LoRaWAN] Join attempt failed, retrying automatically...");
}

void onTxDone(const WisBlockTxResult &result)
{
	Serial.printf("[LoRaWAN] TX %s, airtime %lu ms\n", result.success ? "OK" : "FAILED", result.airtimeMs);
}

void onRxDone(const WisBlockRxResult &result)
{
	Serial.printf("[LoRaWAN] RX %u bytes on port %u, RSSI %d SNR %d\n",
				  result.length, result.port, result.rssi, result.snr);
}

void setup()
{
	Serial.begin(115200);
	delay(2000);

	// The only RAK3401/RAK13300-specific line in this sketch: configures
	// SPI + the radio GPIOs from the built-in RAK3401 preset (including its
	// second SPI peripheral, see WisBlockLoRaHwConfig.cpp) instead of the
	// compile-time RAK4631 default plain lora.begin() would otherwise pick
	// for an nRF52840 target.
	lora.begin(wisblockLoRaHwConfigRAK3401());

	lora.setWorkMode(WISBLOCK_MODE_LORAWAN);
	lora.setOTAAKeys(devEui, joinEui, appKey);
	lora.setRegion(WISBLOCK_RUI3_BAND_EU868); // change to match your region - see WisBlockRUI3Band
	lora.setDeviceClass(WISBLOCK_CLASS_A);
	lora.setADR(true);
	lora.setConfirmedUplinks(false);

	lora.onJoinSuccess(onJoined);
	lora.onJoinFailed(onJoinFailed);
	lora.onLoRaWANTxFinished(onTxDone);
	lora.onLoRaWANRxFinished(onRxDone);

	lora.saveConfig();
	lora.join();
}

void loop()
{
	lora.handleEvents();

	if (lora.isJoined() && millis() - lastUplinkMs > UPLINK_INTERVAL_MS)
	{
		lastUplinkMs = millis();
		uint8_t payload[4] = {0xDE, 0xAD, 0xBE, 0xEF};
		lora.sendLoRaWAN(1, payload, sizeof(payload));
	}
}
