/**
 * @file RAK3401_RAK13702.ino
 * @brief OTAA join and periodic uplink on a RAK3401 (RAK3400 with a RAK13702 Semtech LR2021 module)
 *
 * @details Same sketch as RAK3401_RAK13300.ino (OTAA join and periodic uplink, Class A, EU868), for a
 * RAK3400 WisDuo module used as WisBlock Core module together with a RAK13702 (Semtech LR2021)
 * transceiver module. The only difference is the line in setup():
 *
 *     lora.begin(wisblockLoRaHwConfigRAK3401_RAK13702());
 *
 * The LR2021 support is OFF by default, so it does not add anything to builds for the SX1262 modules.
 * To build this sketch enable it first, either by removing the // in front of
 * `#define WISBLOCK_ENABLE_LR2021` in src/wb_lbm_config.h of this library, or in PlatformIO with
 * `build_flags = -DWISBLOCK_ENABLE_LR2021`. Without it begin() prints a message and does nothing.
 *
 * NOT TESTED ON HARDWARE yet. Two things of the RAK13702 are assumptions that have to be checked
 * (see wisblock_ral_lr20xx_bsp.c): the truth table of the two antenna switches, and the transmit power
 * amplifier settings, so the real output power can differ from the requested one.
 *
 * 2.4 GHz LoRaWAN: set USE_2G4_REGION to 1 below. The 2.4 GHz region WW2G4 is selected with
 * lora.setRegion(WISBLOCK_REGION_WW2G4), see the comment in setup().
 *
 * Only builds for nRF52840 targets.
 */
#define RAK3401

// 1 = join in the 2.4 GHz region WW2G4, 0 = sub-GHz region (EU868 below)
#define USE_2G4_REGION 0
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

	// The only RAK3401/RAK13702-specific line in this sketch: configures
	// SPI + the radio GPIOs from the built-in RAK3401 preset (including its
	// second SPI peripheral, see WisBlockLoRaHwConfig.cpp) instead of the
	// compile-time RAK4631 default plain lora.begin() would otherwise pick
	// for an nRF52840 target.
	lora.begin(wisblockLoRaHwConfigRAK3401_RAK13702());

	lora.setWorkMode(WISBLOCK_MODE_LORAWAN);
	lora.setOTAAKeys(devEui, joinEui, appKey);
#if USE_2G4_REGION
	// 2.4 GHz LoRaWAN (WW2G4, DR0-DR7 = SF12-SF5): band number 13, a library addition (RUI3 has no
	// 2.4 GHz region). Returns false (and join() would refuse) if the radio is not an LR2021.
	if (!lora.setRegion(WISBLOCK_RUI3_BAND_WW2G4))
	{
		Serial.println("2.4 GHz region needs the LR2021 and WISBLOCK_ENABLE_LR2021");
	}
#else
	lora.setRegion(WISBLOCK_RUI3_BAND_EU868); // change to match your region - see WisBlockRUI3Band
#endif
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
