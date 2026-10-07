/**
 * @file BasicLoRaWAN.ino
 * @brief Basic LoRaWAN example: OTAA join, Class A, periodic uplink
 *
 * @details Joins a LoRaWAN network with OTAA (EU868, Class A, ADR on, unconfirmed uplinks) and sends
 * a 4 byte uplink on port 1 every 60 seconds. All LoRaWAN callbacks (join, TX, RX, network time,
 * link check) are wired and print their results. Works unmodified on RAK4631, RAK3312 and RAK11310
 * once WisBlockLoRaBoards.h has the right pins for your revision. Replace the OTAA credentials and
 * the region with your own.
 *
 * The AT command interface runs on the USB serial port. It is polled from loop() with
 * at_serial.handleSerial(), the same way as lora.handleEvents().
 */
#include <WisBlockLoRaAT.h>
#include <WisBlockLoRaWAN.h>

WisBlockLoRaWAN lora;
WisBlockLoRaAT at_serial;

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
 * Prints a message and starts a new join.
 */
void onJoinFailed()
{
	Serial.println("[LoRaWAN] Join failed, retrying...");
	lora.join();
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
 * @brief Network time answer callback
 *
 * Prints the network time (GPS epoch seconds) if the request succeeded.
 *
 * @param success true if the network answered the time request
 * @param t Time answer, t.gpsEpochSeconds is the network time in GPS epoch seconds
 */
void onTimeAnswer(bool success, const WisBlockTimeAnswer &t)
{
	if (success)
	{
		Serial.printf("[LoRaWAN] Network time: %lu s (GPS epoch)\n", t.gpsEpochSeconds);
	}
}

/**
 * @brief Link check answer callback
 *
 * Prints the demodulation margin and the number of gateways if the request succeeded.
 *
 * @param success true if the network answered the link check request
 * @param r Link check result with demodulation margin and gateway count
 */
void onLinkCheck(bool success, const WisBlockLinkCheckResult &r)
{
	if (success)
	{
		Serial.printf("[LoRaWAN] Link check: margin %u dB, %u gateways\n", r.demodMargin, r.gatewayCount);
	}
}

/**
 * @brief Arduino setup function
 *
 * Sets the OTAA keys, region, class and ADR, registers all LoRaWAN callbacks, saves the
 * configuration, starts the AT command interface and starts the join.
 */
void setup()
{
	Serial.begin(115200);
	delay(2000);

	lora.begin();
	lora.setWorkMode(WISBLOCK_MODE_LORAWAN);
	lora.setOTAAKeys(devEui, joinEui, appKey);
	lora.setRegion(WISBLOCK_RUI3_BAND_EU868); // RUI3 AT+BAND numbering - see WisBlockRUI3Band
	lora.setDeviceClass(WISBLOCK_CLASS_A);
	lora.setADR(true);
	lora.setConfirmedUplinks(false);

	lora.onJoinSuccess(onJoined);
	lora.onJoinFailed(onJoinFailed);
	lora.onLoRaWANTxFinished(onTxDone);
	lora.onLoRaWANRxFinished(onRxDone);
	lora.onTimeRequestAnswer(onTimeAnswer);
	lora.onLinkCheckAnswer(onLinkCheck);

	lora.saveConfig();

	// AT commands over USB serial, processed in loop()
	at_serial.begin(lora, Serial);

	lora.join();
}

/**
 * @brief Arduino loop function
 *
 * Handles AT commands and library events. Sends a 4 byte uplink on port 1 every 60 seconds once
 * joined, and lets the MCU sleep for up to 1 second if low power mode is enabled.
 */
void loop()
{
	at_serial.handleSerial(); // reads and executes pending AT commands
	lora.handleEvents();

	if (lora.isJoined() && millis() - lastUplinkMs > UPLINK_INTERVAL_MS)
	{
		lastUplinkMs = millis();
		uint8_t payload[4] = {0xDE, 0xAD, 0xBE, 0xEF};
		lora.sendLoRaWAN(1, payload, sizeof(payload));
	}

	if (lora.isLowPowerEnabled())
	{
		lora.sleep(1000); // wake on DIO1 IRQ or after 1s, whichever first
	}
}
