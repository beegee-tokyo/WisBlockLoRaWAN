/**
 * ATCommandInterface.ino
 * Exposes the full AT command set (see README.md) over USB serial.
 * Useful for provisioning devices from a host script/terminal without
 * flashing per-device firmware.
 *
 * Demonstrates fully background, non-polled operation on RAK4631/RAK3312:
 *   - lora.enableBackgroundTask()      - LoRaWAN/P2P event handling
 *   - atCommands.enableBackgroundRx()  - AT command processing
 * Both fall back to their loop()-polled equivalents automatically if
 * unavailable (e.g. RAK11310, or a build without FreeRTOS) - this sketch
 * works unmodified either way since loop() still calls both handleEvents()
 * and handleSerial(), which become harmless no-ops once their background
 * counterpart is active.
 */
#include <WisBlockLoRaAT.h>
#include <WisBlockLoRaWAN.h>

WisBlockLoRaWAN lora;
WisBlockLoRaAT atCommands;

// Wire the callbacks to unsolicited AT-style notifications so a host script
// watching the serial port sees events as they happen.
void onJoined() { Serial.println("+EVT:JOINED"); }
void onJoinFailed() { Serial.println("+EVT:JOIN_FAILED"); }
void onTxDone(const WisBlockTxResult &r) { Serial.printf("+EVT:TXDONE=%d\n", r.success); }
void onRxDone(const WisBlockRxResult &r) { Serial.printf("+EVT:RXDONE=%u,%d,%d\n", r.length, r.rssi, r.snr); }
void onP2PTx(const WisBlockTxResult &r) { Serial.printf("+EVT:PTXDONE=%d\n", r.success); }
void onP2PRx(const WisBlockRxResult &r) { Serial.printf("+EVT:PRXDONE=%u,%d,%d\n", r.length, r.rssi, r.snr); }
void onCad(WisBlockCADResult r) { Serial.printf("+EVT:CAD=%d\n", (int)r); }

// Anything typed that doesn't start with "AT" lands here instead of being
// swallowed as an AT error - use this to run your own serial protocol
// alongside this library's AT command set.
void onUnhandledLine(const char *line)
{
	Serial.print("+APP: got non-AT line: ");
	Serial.println(line);
}

void setup()
{
	Serial.begin(115200);
	delay(2000);

	lora.begin();
	lora.onJoinSuccess(onJoined);
	lora.onJoinFailed(onJoinFailed);
	lora.onLoRaWANTxFinished(onTxDone);
	lora.onLoRaWANRxFinished(onRxDone);
	lora.onP2PTxFinished(onP2PTx);
	lora.onP2PRxFinished(onP2PRx);
	lora.onP2PCadResult(onCad);

	atCommands.begin(lora, Serial);
	atCommands.onUnhandledData(onUnhandledLine);

	lora.enableBackgroundTask();
	atCommands.enableBackgroundRx();

	Serial.println("Ready. Try: AT+STATUS");
}

void loop()
{
	// Both become harmless no-ops once their background counterpart above
	// is active - kept here so this sketch still works correctly on
	// platforms/builds where one or both aren't available.
	atCommands.handleSerial();
	lora.handleEvents();
}
