/**
 * @file BasicLoRaP2P.ino
 * @brief Basic LoRa P2P example: CAD-gated transmission with a receive window
 *
 * @details Configures LoRa P2P mode (916 MHz, SF7, BW 125 kHz, CR 4/5, 14 dBm, CAD enabled) and
 * starts continuous reception. Every 10 seconds a channel activity detection (CAD) is started.
 * If the channel is clear, the packet "hello p2p" is sent. After each transmission the node
 * listens for 5 seconds. Received packets are printed with RSSI and SNR.
 *
 * The AT command interface runs on the USB serial port. It is polled from loop() with
 * at_serial.handleSerial(), the same way as lora.handleEvents().
 */
#include <WisBlockLoRaAT.h>
#include <WisBlockLoRaWAN.h>

WisBlockLoRaWAN lora;
WisBlockLoRaAT at_serial;
uint32_t lastActionMs = 0;
bool waitingForCad = false;

/**
 * @brief LoRa P2P TX finished callback
 *
 * Prints the result and opens a 5 second receive window.
 *
 * @param result TX result, result.success is true if the packet was sent
 */
void onTxDone(const WisBlockTxResult &result)
{
	Serial.printf("[P2P] TX %s\n", result.success ? "OK" : "FAILED");
	lora.startP2PReceive(5000); // listen for 5s after each TX
}

/**
 * @brief LoRa P2P packet received callback
 *
 * Prints length, RSSI and SNR of the received packet.
 *
 * @param result RX result with payload, length, RSSI and SNR
 */
void onRxDone(const WisBlockRxResult &result)
{
	Serial.printf("[P2P] RX %u bytes, RSSI %d SNR %d\n", result.length, result.rssi, result.snr);
}

/**
 * @brief LoRa P2P channel activity detection (CAD) result callback
 *
 * Sends "hello p2p" if the channel is clear, otherwise the transmission of this cycle is skipped.
 *
 * @param result CAD result, WISBLOCK_CAD_CHANNEL_CLEAR or WISBLOCK_CAD_CHANNEL_DETECTED
 */
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

/**
 * @brief Arduino setup function
 *
 * Configures the LoRa P2P radio parameters, registers the callbacks, saves the configuration,
 * starts the AT command interface and starts continuous reception.
 */
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

	// AT commands over USB serial, processed in loop()
	at_serial.begin(lora, Serial);

	lora.startP2PReceive(0);
}

/**
 * @brief Arduino loop function
 *
 * Handles AT commands and library events, and starts a CAD every 10 seconds.
 */
void loop()
{
	at_serial.handleSerial(); // reads and executes pending AT commands
	lora.handleEvents();

	if (!waitingForCad && millis() - lastActionMs > 10000)
	{
		lastActionMs = millis();
		waitingForCad = true;
		lora.startP2PCad();
	}
}
