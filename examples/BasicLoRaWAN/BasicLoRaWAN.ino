/**
 * BasicLoRaWAN.ino
 * OTAA join, Class A, periodic uplink on port 1, all LoRaWAN callbacks wired.
 * Works unmodified on RAK4631 / RAK3312 / RAK11310 once WisBlockLoRaBoards.h
 * has the right pins for your revision and LBM is vendored in (see README).
 */
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
	Serial.println("[LoRaWAN] Join failed, retrying...");
	lora.join();
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

void onTimeAnswer(bool success, const WisBlockTimeAnswer &t)
{
	if (success)
	{
		Serial.printf("[LoRaWAN] Network time: %lu s (GPS epoch)\n", t.gpsEpochSeconds);
	}
}

void onLinkCheck(bool success, const WisBlockLinkCheckResult &r)
{
	if (success)
	{
		Serial.printf("[LoRaWAN] Link check: margin %u dB, %u gateways\n", r.demodMargin, r.gatewayCount);
	}
}

void setup()
{
	Serial.begin(115200);
	delay(2000);

	lora.begin();
	lora.setWorkMode(WISBLOCK_MODE_LORAWAN);
	lora.setOTAAKeys(devEui, joinEui, appKey);
	lora.setRegion(WISBLOCK_REGION_EU868);
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

	if (lora.isLowPowerEnabled())
	{
		lora.sleep(1000); // wake on DIO1 IRQ or after 1s, whichever first
	}
}
