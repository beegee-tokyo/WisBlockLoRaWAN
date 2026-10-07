/**
 * @file ATCommandInterface.ino
 * @brief Full AT command interface over the USB serial port
 *
 * @details Exposes the complete AT command set (see WisBlockLoRaWAN-AT-Commands.md) over the
 * USB serial port. A device can be provisioned from a host script or terminal without
 * flashing per-device firmware. All LoRaWAN and LoRa P2P events are also printed as
 * unsolicited `+EVT:...` lines, and every received line that is not an AT command is
 * passed to onUnhandledLine().
 *
 * LoRaWAN and P2P events are handled by the library's background task
 * (lora.enableBackgroundTask()). Without FreeRTOS, loop() polls lora.handleEvents().
 *
 * AT commands: atCommands.enableBackgroundRx() is called without a wake callback.
 *  - RAK4631: the commands are processed directly in the TinyUSB receive callback
 *    (inline mode, handleSerial() is a no-op).
 *  - RAK3312 and RAK11310: enableBackgroundRx() returns false and the commands are
 *    processed by atCommands.handleSerial() in loop().
 *
 * See LowPowerLoRaWAN.ino for the recommended way (wake callback, commands processed in loop()).
 */
#include <WisBlockLoRaAT.h>
#include <WisBlockLoRaWAN.h>

WisBlockLoRaWAN lora;
WisBlockLoRaAT atCommands;

// Wire the callbacks to unsolicited AT-style notifications so a host script
// watching the serial port sees events as they happen.
/**
 * @brief LoRaWAN join success callback
 *
 * Prints the unsolicited notification `+EVT:JOINED`.
 */
void onJoined() { Serial.println("+EVT:JOINED"); }
/**
 * @brief LoRaWAN join failed callback
 *
 * Prints the unsolicited notification `+EVT:JOIN_FAILED`. The library retries the join by itself.
 */
void onJoinFailed() { Serial.println("+EVT:JOIN_FAILED"); }
/**
 * @brief LoRaWAN TX finished callback
 *
 * Prints `+EVT:TXDONE=<success>`.
 *
 * @param result TX result, result.success is true if the uplink was sent
 */
void onTxDone(const WisBlockTxResult &result) { Serial.printf("+EVT:TXDONE=%d\n", result.success); }
/**
 * @brief LoRaWAN downlink received callback
 *
 * Prints `+EVT:RXDONE=<length>,<rssi>,<snr>`.
 *
 * @param result RX result with payload, length, RSSI and SNR
 */
void onRxDone(const WisBlockRxResult &result) { Serial.printf("+EVT:RXDONE=%u,%d,%d\n", result.length, result.rssi, result.snr); }
/**
 * @brief LoRa P2P TX finished callback
 *
 * Prints `+EVT:PTXDONE=<success>`.
 *
 * @param result TX result, result.success is true if the packet was sent
 */
void onP2PTx(const WisBlockTxResult &result) { Serial.printf("+EVT:PTXDONE=%d\n", result.success); }
/**
 * @brief LoRa P2P packet received callback
 *
 * Prints `+EVT:PRXDONE=<length>,<rssi>,<snr>`. A length of 0 means the RX window ended without a packet.
 *
 * @param result RX result with payload, length, RSSI and SNR
 */
void onP2PRx(const WisBlockRxResult &result) { Serial.printf("+EVT:PRXDONE=%u,%d,%d\n", result.length, result.rssi, result.snr); }
/**
 * @brief LoRa P2P channel activity detection (CAD) result callback
 *
 * Prints `+EVT:CAD=<result>`.
 *
 * @param result CAD result, channel clear or channel detected
 */
void onCad(WisBlockCADResult result) { Serial.printf("+EVT:CAD=%d\n", (int)result); }

/**
 * @brief Handler for received lines that are not AT commands
 *
 * Anything that does not start with "AT" lands here instead of being answered with an AT error.
 * Use it to run your own serial protocol alongside the AT command set. This example prints the line.
 *
 * @param line Zero terminated text of the received line
 */
void onUnhandledLine(const char *line)
{
	Serial.print("+APP: got non-AT line: ");
	Serial.println(line);
}

/**
 * @brief Arduino setup function
 *
 * Starts the serial port and the library, registers all LoRaWAN and P2P callbacks, starts the AT
 * command interface and enables the background modes of the library.
 */
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

/**
 * @brief Arduino loop function
 *
 * Calls atCommands.handleSerial() and lora.handleEvents(). Each of them does nothing while its
 * background counterpart is active.
 */
void loop()
{
	// Both become harmless no-ops once their background counterpart above
	// is active - kept here so this sketch still works correctly on
	// platforms/builds where one or both aren't available.
	atCommands.handleSerial();
	lora.handleEvents();
}
