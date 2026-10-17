/**
 * @file LoRaWANClassB.ino
 * @brief LoRaWAN Class B with the state of the Class B set-up printed
 *
 * @details Joins with OTAA, requests Class B and prints every step of the Class B set-up, the same
 * S0 ... S3 that RUI3 shows with AT+CLASS=? (B,S0 ... B,S3):
 *
 *   S0  Class B requested, not joined yet (or nothing sent since the join)
 *   S1  uplink sent or beacon search started, the stack waits for the beacon of the gateway
 *   S2  beacon locked, the ping slots are open (Class B downlinks are received)
 *   S3  beacon lock failed or lost, the stack keeps searching
 *
 * The state is reported by the callback onClassBStateChanged() and can be read with getClassBState().
 * The AT command interface shows it too: AT+CLASS=? returns for example AT+CLASS=B,S2.
 *
 * The gateway has to send beacons (the network server must support Class B and must answer the time
 * and ping slot requests). After the join the stack sends its own request uplink for the network time
 * (DeviceTimeReq) and the ping slot answer (PingSlotInfoReq), then it starts the beacon search; the
 * sketch also sends an application uplink right after the join, and then every 5 minutes. Replace
 * the OTAA credentials and the region with your own.
 */
#include <WisBlockLoRaAT.h>
#include <WisBlockLoRaWAN.h>

WisBlockLoRaWAN lora;
WisBlockLoRaAT at_serial;

// Replace with your device's real OTAA credentials.
uint8_t devEui[8] = {0xac, 0x1f, 0x09, 0xff, 0xfe, 0x06, 0x79, 0xdb};
uint8_t joinEui[8] = {0x70, 0xb3, 0xd5, 0x7e, 0xd0, 0x02, 0x01, 0xe1};
uint8_t appKey[16] = {0x2b, 0x84, 0xe0, 0xb0, 0x9b, 0x68, 0xe5, 0xcb, 0x42, 0x17, 0x6f, 0xe7, 0x53, 0xdc, 0xee, 0x79};

const uint32_t UPLINK_INTERVAL_MS = 300000;
uint32_t lastUplinkMs = 0;
bool firstUplinkSent = false;

/**
 * @brief Class B state callback
 *
 * Called when the state of the Class B set-up changes. Runs in the context of lora.handleEvents(),
 * keep it short.
 *
 * @param state New state S0 ... S3
 */
void onClassBState(WisBlockClassBState state)
{
	switch (state)
	{
	case WISBLOCK_CLASS_B_S0:
		Serial.println("[Class B] S0: requested, waiting for the join / first uplink");
		break;
	case WISBLOCK_CLASS_B_S1:
		Serial.println("[Class B] S1: uplink sent or beacon search started, waiting for the beacon");
		break;
	case WISBLOCK_CLASS_B_S2:
		Serial.println("[Class B] S2: beacon locked, ping slots open");
		break;
	case WISBLOCK_CLASS_B_S3:
		Serial.println("[Class B] S3: beacon lock failed or lost, still searching");
		break;
	}
}

void onJoined()
{
	Serial.println("[LoRaWAN] Join succeeded");
	firstUplinkSent = false; // send the first uplink right away
}

void onJoinFailed()
{
	Serial.println("[LoRaWAN] Join failed, retrying...");
	lora.join();
}

void onRxDone(const WisBlockRxResult &result)
{
	Serial.printf("[LoRaWAN] RX %u bytes on port %u, RSSI %d SNR %d\n", result.length, result.port, result.rssi, result.snr);
}

void setup()
{
	Serial.begin(115200);
	delay(2000);

	lora.begin();
	lora.setWorkMode(WISBLOCK_MODE_LORAWAN);
	lora.setOTAAKeys(devEui, joinEui, appKey);
	lora.setRegion(WISBLOCK_RUI3_BAND_EU868); // change to match your region - see WisBlockRUI3Band
	lora.setADR(true);

	lora.onJoinSuccess(onJoined);
	lora.onJoinFailed(onJoinFailed);
	lora.onLoRaWANRxFinished(onRxDone);
	lora.onClassBStateChanged(onClassBState);

	// Class B can only be set on a joined device: the request is stored and applied after the join.
	// The callback is called with S0 right here.
	lora.setDeviceClass(WISBLOCK_CLASS_B);

	at_serial.begin(lora, Serial); // AT+CLASS=? shows B,S0 ... B,S3
	lora.join();
}

void loop()
{
	at_serial.handleSerial();
	lora.handleEvents();

	// An uplink right after the join, then one every 5 minutes
	if (lora.isJoined() && (!firstUplinkSent || millis() - lastUplinkMs > UPLINK_INTERVAL_MS))
	{
		firstUplinkSent = true;
		lastUplinkMs = millis();
		uint8_t payload[2] = {0xB0, 0x01};
		lora.sendLoRaWAN(1, payload, sizeof(payload));
	}
}
