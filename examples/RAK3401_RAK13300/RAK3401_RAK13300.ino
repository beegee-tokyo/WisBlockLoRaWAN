/**
 * @file RAK3401_RAK13300.ino
 * @brief OTAA join and periodic uplink on a RAK3401 (RAK3400 with RAK13300 / RAK13302 SX1262 module)
 *
 * @details Minimal OTAA join and periodic uplink (Class A, EU868) on a RAK3400 WisDuo module used as
 * WisBlock Core module together with a RAK13300 (or RAK13302, same wiring) SX1262 transceiver
 * module. "RAK3401" is the name this library uses for this combination (see WisBlockLoRaHwConfig.h).
 *
 * The flow is the same as in BasicLoRaWAN.ino, without the AT command interface and without the
 * network time and link check callbacks. The only hardware specific line is in setup():
 *
 *     lora.begin(wisblockLoRaHwConfigRAK3401());   // instead of plain lora.begin()
 *
 * It selects the radio wiring. Every other API call, the AT command layer if you add it and the
 * other examples work the same regardless of which begin() you call.
 *
 * Only builds for nRF52840 targets (the RAK3400 is an nRF52840 module). See WisBlockLoRaHwConfig.h
 * and the "Board flexibility" comment in wisblock_radio_hal.h for why this begin() overload does
 * not exist for the RP2040.
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

/**
 * @brief LoRaWAN join success callback
 *
 * Prints a message. Uplinks start after this in loop().
 */
void onJoined()
{
	Serial.println("[LoRaWAN] Join succeeded");
}

/**
 * @brief LoRaWAN join failed callback
 *
 * Prints a message. A failed join is retried by the library, so no new join is started here.
 */
void onJoinFailed()
{
	// Don't call lora.join() here - a failed join is already retried
	// automatically (see setJoinReattemptInterval()/setMaxJoinAttempts() in
	// WisBlockLoRaWAN-API.md if you want different timing than the default).
	Serial.println("[LoRaWAN] Join attempt failed, retrying automatically...");
}

/**
 * @brief LoRaWAN TX finished callback
 *
 * Prints the result and the airtime of the uplink.
 *
 * @param result TX result with success flag and airtime in ms
 */
void onTxDone(const WisBlockTxResult &result)
{
	Serial.printf("[LoRaWAN] TX %s, airtime %lu ms\n", result.success ? "OK" : "FAILED", result.airtimeMs);
}

/**
 * @brief LoRaWAN downlink received callback
 *
 * Prints length, port, RSSI and SNR of the downlink.
 *
 * @param result RX result with payload, length, port, RSSI and SNR
 */
void onRxDone(const WisBlockRxResult &result)
{
	Serial.printf("[LoRaWAN] RX %u bytes on port %u, RSSI %d SNR %d\n",
				  result.length, result.port, result.rssi, result.snr);
}

/**
 * @brief Arduino setup function
 *
 * Starts the library with the RAK3401 hardware configuration, sets the OTAA keys, region, class
 * and ADR, registers the callbacks, saves the configuration and starts the join.
 */
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

/**
 * @brief Arduino loop function
 *
 * Handles library events and sends a 4 byte uplink on port 1 every 60 seconds once joined.
 */
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
