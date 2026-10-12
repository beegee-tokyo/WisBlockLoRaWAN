/**
 * @file FskP2PTest.ino
 * @brief Minimal FSK P2P test between two devices, API calls only
 *
 * @details Flash this sketch on two devices. On the first one keep `ROLE_SENDER` at 1, on the other
 * one set it to 0 (receiver). The sender transmits "FSK test <n>" every 2 seconds, the receiver
 * prints every packet with its RSSI and counts the packets it missed (from the number in the text).
 *
 * Both devices must have the same radio settings (frequency, bit rate, deviation, bandwidth,
 * preamble length, sync word). Keep the antennas at least 1 m apart and use a low TX power for
 * a bench test. Check that the frequency is allowed in your country.
 *
 * Nothing is saved to the flash, so the settings of the AT command interface or earlier sketches
 * are not changed (the work mode of this sketch is not saved either).
 */
#include <WisBlockLoRaWAN.h>

#if defined ARDUINO_ARCH_RP2040
#define ROLE_SENDER 1 // 1 = sender, 0 = receiver
#else
#define ROLE_SENDER 0 // 1 = sender, 0 = receiver
#endif

// Radio settings, the same on both devices
#define FSK_FREQUENCY_HZ 916400000UL
#define FSK_BITRATE 50000	 // bit/s
#define FSK_DEVIATION_HZ 25000 // Hz
#define FSK_BANDWIDTH_HZ 117300 // Hz, at least bit rate + 2 * deviation
#define FSK_PREAMBLE_BYTES 8
#define FSK_TX_POWER_DBM 5

WisBlockLoRaWAN lora;
uint32_t lastTxMs = 0;
uint32_t txCounter = 0;
int32_t lastRxCounter = -1;
uint32_t rxOk = 0;
uint32_t rxMissed = 0;

#ifndef LED_GREEN
#define LED_GREEN (23)
#endif
#ifndef LED_BLUE
#define LED_BLUE (24)
#endif

/**
 * @brief FSK TX finished callback
 *
 * @param result TX result with the airtime of the packet
 */
void onTxDone(const WisBlockTxResult &result)
{
	Serial.printf("[FSK] TX done, airtime %lu ms\n", (unsigned long)result.airtimeMs);
}

/**
 * @brief FSK RX callback
 *
 * A length of 0 is a packet with a wrong CRC, the continuous receive goes on.
 *
 * @param result RX result with payload, length and RSSI
 */
void onRxDone(const WisBlockRxResult &result)
{
	if (result.length == 0)
	{
		Serial.println("[FSK] RX without a valid packet");
		return;
	}
	rxOk++;
	Serial.printf("[FSK] RX %u bytes, RSSI %d dBm: %.*s\n", result.length, result.rssi, result.length, (const char *)result.data);

	// Count lost packets from the number in "FSK test <n>"
	const char *num = strrchr((const char *)result.data, ' ');
	if (num != nullptr)
	{
		int32_t n = atoi(num + 1);
		if (lastRxCounter >= 0 && n > lastRxCounter + 1)
		{
			rxMissed += n - lastRxCounter - 1;
		}
		lastRxCounter = n;
		Serial.printf("[FSK] received %lu, missed %lu\n", (unsigned long)rxOk, (unsigned long)rxMissed);
	}
}

// Receiver diagnostics: prints once per second the highest instantaneous RSSI seen and the packet
// counters of the radio. Set to 0 to switch it off.
#ifndef FSK_DEBUG
#define FSK_DEBUG 1
#endif
// 1 = run the radio in LoRa mode instead of FSK (diagnostic: does the RSSI reading work at all?)
#ifndef FSK_TEST_AS_LORA
#define FSK_TEST_AS_LORA 0
#endif
#if FSK_DEBUG && ROLE_SENDER != 1
int16_t rssiMax = -200;
uint32_t lastDebugMs = 0;

/**
 * @brief Print the diagnostics of the receiver once per second
 *
 * The RSSI is polled as fast as loop() runs, a packet of a few milliseconds shows up as a peak.
 * - RSSI stays at the noise floor (about -100 dBm or lower) when the sender transmits: the signal
 *   does not arrive (frequency, antenna, sender does not radiate).
 * - RSSI peaks every 2 seconds but the counters stay 0: the signal arrives but is not recognized
 *   (preamble, sync word, bit rate, deviation, bandwidth).
 * - The crc counter increases: the signal is recognized but the data is damaged (CRC, whitening, deviation).
 */
void debugReceiver()
{
	int16_t rssi;
	if (lora.getP2PInstantRssi(rssi) && rssi > rssiMax)
	{
		rssiMax = rssi;
	}
	if (millis() - lastDebugMs >= 1000)
	{
		lastDebugMs = millis();
		uint16_t ok = 0, crc = 0, len = 0;
		lora.getP2PFskStats(ok, crc, len);
		uint8_t mode = 0, cmd = 0;
		uint16_t errors = 0;
		lora.getP2PRadioStatus(mode, cmd, errors);
		Serial.printf("[FSK] RSSI max %d dBm, radio counters: ok %u, crc error %u, length error %u, chip mode %u (5 = RX), cmd status %u, errors 0x%04X\n",
					  rssiMax, ok, crc, len, mode, cmd, errors);
		rssiMax = -200;
	}
}
#endif

/**
 * @brief Arduino setup function
 *
 * Starts the library, switches to FSK mode, sets the radio parameters and, for the receiver,
 * starts continuous reception.
 */
void setup()
{

	Serial.begin(115200);
	time_t serial_timeout = millis();
	// On nRF52840 the USB serial is not available immediately
	while (!Serial)
	{
		if ((millis() - serial_timeout) < 5000)
		{
			delay(100);
			digitalWrite(LED_GREEN, !digitalRead(LED_GREEN));
		}
		else
		{
			break;
		}
	}
	delay(2000);

	lora.begin();
#if FSK_TEST_AS_LORA
	// Comparison test of the receiver: LoRa instead of FSK, only the RSSI of the debug line is meaningful
	lora.setWorkMode(WISBLOCK_MODE_LORA_P2P);
#else
	lora.setWorkMode(WISBLOCK_MODE_FSK_P2P);
#endif
	lora.setP2PFrequency(FSK_FREQUENCY_HZ);
	lora.setP2PFskBitrate(FSK_BITRATE);
	lora.setP2PFskFrequencyDeviation(FSK_DEVIATION_HZ);
	lora.setP2PFskBandwidth(FSK_BANDWIDTH_HZ);
	lora.setP2PPreambleLength(FSK_PREAMBLE_BYTES); // bytes in FSK mode
	lora.setP2PTxPower(FSK_TX_POWER_DBM);

	lora.onP2PTxFinished(onTxDone);
	lora.onP2PRxFinished(onRxDone);

#if ROLE_SENDER == 1
	Serial.println("[FSK] sender");
#else
	Serial.println("[FSK] receiver");
	lora.startP2PReceive(WISBLOCK_P2P_RX_CONTINUOUS_TX); // continuous reception
#endif
}

/**
 * @brief Arduino loop function
 *
 * Runs the library events. The sender transmits a numbered packet every 2 seconds.
 */
void loop()
{
	lora.handleEvents();
#if FSK_DEBUG && ROLE_SENDER != 1
	debugReceiver();
#endif

#if ROLE_SENDER == 1
	if (millis() - lastTxMs >= 2000)
	{
		lastTxMs = millis();
		char text[32];
		int len = snprintf(text, sizeof(text), "FSK test %lu", (unsigned long)txCounter++);
		Serial.printf("[FSK] sending \"%s\"\n", text);
		lora.sendP2P((const uint8_t *)text, (uint8_t)len);
	}
#endif
}
