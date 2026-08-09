/**
 * BasicLoRaP2P.ino
 * Alternates between CAD-gated TX and RX every few seconds.
 */
#include <WisBlockLoRaWAN.h>

WisBlockLoRaWAN lora;
uint32_t lastActionMs = 0;
bool waitingForCad = false;

void onTxDone(const WisBlockTxResult &result)
{
	Serial.printf("[P2P] TX %s\n", result.success ? "OK" : "FAILED");
	lora.startP2PReceive(5000); // listen for 5s after each TX
}

void onRxDone(const WisBlockRxResult &result)
{
	Serial.printf("[P2P] RX %u bytes, RSSI %d SNR %d\n", result.length, result.rssi, result.snr);
}

void onCad(WisBlockCADResult result)
{
	waitingForCad = false;
	if (result == WISBLOCK_CAD_CHANNEL_CLEAR)
	{
		uint8_t payload[] = "hello p2p";
		lora.sendP2P(payload, sizeof(payload) - 1);
	}
	else
	{
		Serial.println("[P2P] Channel busy, skipping TX this cycle");
	}
}

void setup()
{
	Serial.begin(115200);
	delay(2000);

	lora.begin();
	lora.setWorkMode(WISBLOCK_MODE_LORA_P2P);
	lora.setP2PFrequency(916000000UL);
	lora.setP2PSpreadingFactor(7);
	lora.setP2PBandwidth(WISBLOCK_BW_125);
	lora.setP2PCodingRate(WISBLOCK_CR_4_5);
	lora.setP2PPreambleLength(8);
	lora.setP2PTxPower(14);
	lora.setP2PCad(true);

	lora.onP2PTxFinished(onTxDone);
	lora.onP2PRxFinished(onRxDone);
	lora.onP2PCadResult(onCad);

	lora.saveConfig();
	lora.startP2PReceive(0);
}

void loop()
{
	lora.handleEvents();

	if (!waitingForCad && millis() - lastActionMs > 10000)
	{
		lastActionMs = millis();
		waitingForCad = true;
		lora.startP2PCad();
	}
}
