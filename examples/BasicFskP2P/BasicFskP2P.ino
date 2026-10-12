/**
 * @file BasicFskP2P.ino
 * @brief Basic FSK P2P example: GFSK transmission every 10 seconds with continuous reception
 *
 * @details Configures the FSK P2P mode (916 MHz, 50 kbit/s, 25 kHz deviation, 117.3 kHz RX
 * bandwidth, 14 dBm) and starts continuous reception with WISBLOCK_P2P_RX_CONTINUOUS_TX (65533, the
 * same as RUI3 AT+PRECV=65533): the library starts the receive again after every transmission.
 * Every 10 seconds the packet "hello fsk" is sent. Received packets are printed with their RSSI
 * (there is no SNR in FSK).
 *
 * Both ends of a link must use the same frequency, bit rate, deviation, bandwidth, sync word and
 * preamble length. The FSK preamble length is counted in bytes (LoRa: symbols). The default sync
 * word is C1 94 C1, change it with lora.setP2PFskSyncWord(). Channel activity detection (CAD) only
 * exists for LoRa, there is no listen-before-talk in FSK mode.
 *
 * The same settings can be made with the AT commands: AT+NWM=2, AT+PFREQ, AT+PBR, AT+PFDEV,
 * AT+PBW (in Hz), AT+PPL (bytes), AT+PTP. The AT command interface runs on the USB serial port and
 * is polled from loop() with at_serial.handleSerial().
 *
 * Not tested against other FSK devices (for example RUI3): the packet format is variable length
 * with a 2 byte CRC and whitening, Gaussian filter BT 1.0 - check this if you need to talk to
 * other firmware.
 */
#include <WisBlockLoRaAT.h>
#include <WisBlockLoRaWAN.h>

WisBlockLoRaWAN lora;
WisBlockLoRaAT at_serial;
uint32_t lastActionMs = 0;

/**
 * @brief FSK P2P TX finished callback
 *
 * Prints the result and the airtime. The receive continues by itself (WISBLOCK_P2P_RX_CONTINUOUS_TX).
 *
 * @param result TX result, result.success is true if the packet was sent
 */
void onTxDone(const WisBlockTxResult &result)
{
	Serial.printf("[FSK] TX %s, airtime %lu ms\n", result.success ? "OK" : "FAILED", (unsigned long)result.airtimeMs);
}

/**
 * @brief FSK P2P packet received callback
 *
 * A length of 0 means that a packet with a wrong CRC was received. The continuous receive goes on.
 *
 * @param result RX result with payload, length and RSSI
 */
void onRxDone(const WisBlockRxResult &result)
{
	if (result.length == 0)
	{
		Serial.println("[FSK] Packet with a wrong CRC");
		return;
	}
	Serial.printf("[FSK] RX %u bytes, RSSI %d dBm: %.*s\n", result.length, result.rssi, result.length, (const char *)result.data);
}

/**
 * @brief Arduino setup function
 *
 * Switches to the FSK P2P mode, sets the FSK parameters, registers the callbacks, saves the
 * configuration, starts the AT command interface and starts continuous reception.
 */
void setup()
{
	Serial.begin(115200);
	delay(2000);

	lora.begin();
	lora.setWorkMode(WISBLOCK_MODE_FSK_P2P);
	lora.setP2PFrequency(916000000UL);
	lora.setP2PFskBitrate(50000);			// bit/s
	lora.setP2PFskFrequencyDeviation(25000); // Hz
	lora.setP2PFskBandwidth(117300);		// Hz, at least bit rate + 2 * deviation
	lora.setP2PPreambleLength(8);			// bytes in FSK mode
	lora.setP2PTxPower(14);

	lora.onP2PTxFinished(onTxDone);
	lora.onP2PRxFinished(onRxDone);

	lora.saveConfig();

	// AT commands over USB serial, processed in loop()
	at_serial.begin(lora, Serial);

	lora.startP2PReceive(WISBLOCK_P2P_RX_CONTINUOUS_TX);
}

/**
 * @brief Arduino loop function
 *
 * Handles AT commands and library events, and sends a packet every 10 seconds.
 */
void loop()
{
	at_serial.handleSerial(); // reads and executes pending AT commands
	lora.handleEvents();

	if (millis() - lastActionMs > 10000)
	{
		lastActionMs = millis();
		uint8_t payload[] = "hello fsk";
		lora.sendP2P(payload, sizeof(payload) - 1);
	}
}
