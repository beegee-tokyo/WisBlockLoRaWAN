
#include <Arduino.h>
#include "WisBlockLoRaAT.h"
#include "WisBlockLoRaWAN_all.h" // WISBLOCK_LORAWAN_VERSION_STRING, used by the AT+VER handler
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h> // strcasecmp() - used for case-insensitive custom AT command name matching

#if defined(ARDUINO_ARCH_RP2040)
#include "pico/unique_id.h" // pico_get_unique_board_id() - used by the AT+SN=? handler
#endif

/**@brief Unique Devices IDs register set (nRF52)
 */
#define ID1 (0x10000060)
#define ID2 (0x10000064)

// #if defined ARDUINO_ARCH_ESP32
// #include "boards/mcu/board.h"
// #endif

namespace
{
	bool startsWith(const char *str, const char *prefix)
	{
		return strncmp(str, prefix, strlen(prefix)) == 0;
	}

	// Case-insensitive "AT" prefix check only - used to decide whether a
	// line should go through the (uppercasing) command parser at all,
	// before that uppercasing happens. Lines that don't start with "AT" in
	// any case are passed to unhandledDataCb() completely untouched - see
	// processLine()'s doc comment for why that path must not be uppercased.
	bool startsWithAtCaseInsensitive(const char *str)
	{
		return (str[0] == 'A' || str[0] == 'a') && (str[1] == 'T' || str[1] == 't');
	}

	// Shared by every query handler below that reads back a byte array
	// (DevEUI, JoinEUI, DevAddr, SN) as upper-case hex, matching the format
	// their corresponding setters accept.
	void printHex(Stream *port, const uint8_t *data, size_t len)
	{
		char buf[3];
		for (size_t i = 0; i < len; i++)
		{
			snprintf(buf, sizeof(buf), "%02X", data[i]);
			port->print(buf);
			delay(10);
		}
		port->printf("\r\n");
		port->flush();
	}

	// AT+BAND uses RUI3's region numbering (see the RUI3 AT command
	// manual), which doesn't match WisBlockRegion's own enum values or
	// order - this library predates the RUI3-compatibility pass and
	// numbered regions in whatever order they were originally added.
	// RUI3's EU433 (0) and LA915 (12) have no WisBlockRegion equivalent at
	// all - this vendored LBM build's main.h doesn't define REGION_EU_433
	// or REGION_LA_915, so those region tables were never compiled in;
	// setBandIndexToRegion() returns false for them rather than silently
	// picking something else.
	bool bandIndexToRegion(int index, WisBlockRegion &outRegion)
	{
		switch (index)
		{
		case 1:
			outRegion = WISBLOCK_REGION_CN470;
			return true;
		case 2:
			outRegion = WISBLOCK_REGION_RU864;
			return true;
		case 3:
			outRegion = WISBLOCK_REGION_IN865;
			return true;
		case 4:
			outRegion = WISBLOCK_REGION_EU868;
			return true;
		case 5:
			outRegion = WISBLOCK_REGION_US915;
			return true;
		case 6:
			outRegion = WISBLOCK_REGION_AU915;
			return true;
		case 7:
			outRegion = WISBLOCK_REGION_KR920;
			return true;
		case 8:
			outRegion = WISBLOCK_REGION_AS923_1;
			return true;
		case 9:
			outRegion = WISBLOCK_REGION_AS923_2;
			return true;
		case 10:
			outRegion = WISBLOCK_REGION_AS923_3;
			return true;
		case 11:
			outRegion = WISBLOCK_REGION_AS923_4;
			return true;
		default: // 0 (EU433), 12 (LA915), and anything else out of range
			return false;
		}
	}

	// Inverse of bandIndexToRegion() - returns -1 for WisBlockRegion values
	// with no RUI3 band index at all (WISBLOCK_REGION_CN470_RP_1_0,
	// WISBLOCK_REGION_WW2G4 - library-specific regions beyond RUI3's set).
	int regionToBandIndex(WisBlockRegion region)
	{
		switch (region)
		{
		case WISBLOCK_REGION_EU868:
			return 4;
		case WISBLOCK_REGION_US915:
			return 5;
		case WISBLOCK_REGION_AU915:
			return 6;
		case WISBLOCK_REGION_AS923_1:
			return 8;
		case WISBLOCK_REGION_AS923_2:
			return 9;
		case WISBLOCK_REGION_AS923_3:
			return 10;
		case WISBLOCK_REGION_AS923_4:
			return 11;
		case WISBLOCK_REGION_KR920:
			return 7;
		case WISBLOCK_REGION_IN865:
			return 3;
		case WISBLOCK_REGION_RU864:
			return 2;
		case WISBLOCK_REGION_CN470:
			return 1;
		default:
			return -1;
		}
	}
} // namespace

WisBlockLoRaAT *WisBlockLoRaAT::activeInstanceForRx = nullptr;

// ---------------------------------------------------------------------------
// Built-in AT command lookup table - one row per command name (no "AT"
// prefix, no "=", no "?") mapped to the member function that implements it.
// Replaces the old ~900-line if/else-if chain of strcmp()/startsWith() calls
// in processLine() with a single loop (see processLine() below), the same
// shape as RUI3's own atcmd_info_tbl[] in atcmd.c. Two rows (+APPEUI/
// +JOINEUI) intentionally point at the same handler - that alias existed in
// the old code too (see atAppEui()'s doc comment).
// ---------------------------------------------------------------------------
const WisBlockLoRaAT::AtCommandEntry WisBlockLoRaAT::atCommandTable[] = {
	{"+NWM", &WisBlockLoRaAT::atNwm},
	{"+DEVEUI", &WisBlockLoRaAT::atDevEui},
	{"+APPEUI", &WisBlockLoRaAT::atAppEui},
	{"+JOINEUI", &WisBlockLoRaAT::atAppEui},
	{"+APPKEY", &WisBlockLoRaAT::atAppKey},
	{"+DEVADDR", &WisBlockLoRaAT::atDevAddr},
	{"+NWKSKEY", &WisBlockLoRaAT::atNwkSKey},
	{"+APPSKEY", &WisBlockLoRaAT::atAppSKey},
	{"+BAND", &WisBlockLoRaAT::atBand},
	{"+MASK", &WisBlockLoRaAT::atMask},
	{"+LBT", &WisBlockLoRaAT::atLbt},
	{"+LBTRSSI", &WisBlockLoRaAT::atLbtRssi},
	{"+LBTSCANTIME", &WisBlockLoRaAT::atLbtScanTime},
	{"+PGSLOT", &WisBlockLoRaAT::atPgSlot},
	{"+BFREQ", &WisBlockLoRaAT::atBFreq},
	{"+BTIME", &WisBlockLoRaAT::atBTime},
	{"+DR", &WisBlockLoRaAT::atDr},
	{"+CLASS", &WisBlockLoRaAT::atClass},
	{"+NJM", &WisBlockLoRaAT::atNjm},
	{"+JOIN", &WisBlockLoRaAT::atJoin},
	{"+NJS", &WisBlockLoRaAT::atNjs},
	{"+ADR", &WisBlockLoRaAT::atAdr},
	{"+TXP", &WisBlockLoRaAT::atTxp},
	{"+CFM", &WisBlockLoRaAT::atCfm},
	{"+FPENDING", &WisBlockLoRaAT::atFPending},
	{"+SEND", &WisBlockLoRaAT::atSend},
	{"+LINKCHECK", &WisBlockLoRaAT::atLinkCheck},
	{"+TIMEREQ", &WisBlockLoRaAT::atTimeReq},
	{"+P2P", &WisBlockLoRaAT::atP2p},
	{"+CAD", &WisBlockLoRaAT::atCad},
	{"+RXBOOST", &WisBlockLoRaAT::atRxBoost},
	{"+PSEND", &WisBlockLoRaAT::atPSend},
	{"+PRECV", &WisBlockLoRaAT::atPRecv},
	{"+PRECVDC", &WisBlockLoRaAT::atPRecvDc},
	{"+LOWPOWER", &WisBlockLoRaAT::atLowPower},
	{"+SAVE", &WisBlockLoRaAT::atSave},
	{"+RESTORE", &WisBlockLoRaAT::atRestore},
	{"+FACTORY", &WisBlockLoRaAT::atFactory},
	{"+STATUS", &WisBlockLoRaAT::atStatus},
	{"+HWMODEL", &WisBlockLoRaAT::atHwModel},
	{"+HWID", &WisBlockLoRaAT::atHwId},
	{"+SN", &WisBlockLoRaAT::atSn},
	{"+VER", &WisBlockLoRaAT::atVer},
	{"+FIRMWAREVER", &WisBlockLoRaAT::atFirmwareVer},
	{"+ALIAS", &WisBlockLoRaAT::atAlias},
	{"Z", &WisBlockLoRaAT::atZ},
	{"R", &WisBlockLoRaAT::atR},
	{"+BOOT", &WisBlockLoRaAT::atBoot},
};
const size_t WisBlockLoRaAT::atCommandTableSize = sizeof(WisBlockLoRaAT::atCommandTable) / sizeof(WisBlockLoRaAT::atCommandTable[0]);

#ifdef ARDUINO_ARCH_ESP32
#include <esp_system.h>
void usbEventCallback(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
#endif

void WisBlockLoRaAT::begin(WisBlockLoRaWAN &loraRef, Stream &portRef)
{
	lora = &loraRef;
	port = &portRef;
	lineLength = 0;

#ifdef ARDUINO_ARCH_ESP32
	backgroundRxActive = true;
	Serial.onEvent(usbEventCallback);
#endif
}

void WisBlockLoRaAT::handleSerial()
{
	// Once background RX mode (see enableBackgroundRx()) owns serial
	// processing, calling this from loop() too would let two different
	// contexts read from the same Stream concurrently. Harmless no-op
	// instead of removing it outright, same reasoning as
	// WisBlockLoRaWAN::handleEvents()'s equivalent guard.
	if (backgroundRxActive)
	{
		return;
	}
	processIncomingBytes();
}

void WisBlockLoRaAT::processIncomingBytes()
{
	while (port->available())
	{
		char c = (char)port->read();
		if (c == '\r' || c == '\n')
		{
			if (lineLength > 0)
			{
				lineBuffer[lineLength] = '\0';
				if (lora)
				{
					lora->lockLbm(); // no-op unless WisBlockLoRaWAN::enableBackgroundTask() is active
				}
				processLine(lineBuffer);
				if (lora)
				{
					lora->unlockLbm();
				}
				lineLength = 0;
			}
		}
		else if (lineLength < sizeof(lineBuffer) - 1)
		{
			lineBuffer[lineLength++] = c;
		}
		delay(5);
	}
}

bool WisBlockLoRaAT::parseHex(const char *hex, uint8_t *out, size_t outLen)
{
	if (strlen(hex) != outLen * 2)
	{
		return false;
	}
	for (size_t i = 0; i < outLen; i++)
	{
		char byteStr[3] = {hex[i * 2], hex[i * 2 + 1], '\0'};
		char *endPtr;
		long val = strtol(byteStr, &endPtr, 16);
		if (*endPtr != '\0')
		{
			return false;
		}
		out[i] = (uint8_t)val;
	}
	return true;
}

void WisBlockLoRaAT::reply(const char *msg)
{
	port->printf("%s\r\n", msg);
	port->printf("OK\r\n");
	port->flush();
}

void WisBlockLoRaAT::replyOk()
{
	port->printf("OK\r\n");
	port->flush();
}

void WisBlockLoRaAT::replyError(const char *reason)
{
	// `reason` is used two different ways across this file's call sites:
	//  - a protocol status token ("AT_ERROR", "AT_PARAM_ERROR") is meant to
	//    BE the entire reply on its own.
	//  - anything else (e.g. "bad hex, expected 16 chars") is a
	//    human-readable explanation, meant as an extra line ahead of the
	//    standard "AT_ERROR" sentinel that terminates every other error
	//    reply.
	// FIX: this used to always print `reason` (when given) and then
	// unconditionally append a second, hardcoded "AT_ERROR" line after it
	// regardless of which of the two cases above it was - so
	// replyError("AT_ERROR") sent "AT_ERROR" twice, and
	// replyError("AT_PARAM_ERROR") sent "AT_PARAM_ERROR" followed by a
	// redundant second "AT_ERROR" line.
	if (reason && startsWith(reason, "AT_"))
	{
		port->print(reason);
		port->print("\r\n");
		port->flush();
		return;
	}
	if (reason)
	{
		port->println(reason);
	}
	port->print("AT_ERROR\r\n");
	port->flush();
}

void WisBlockLoRaAT::handleStatusQuery()
{
	const WisBlockPersistedConfig &cfg = lora->getConfig();
	port->print("MODE=");
	port->println(cfg.workMode == WISBLOCK_MODE_LORAWAN ? "LORAWAN" : "LORA_P2P");
	if (cfg.workMode == WISBLOCK_MODE_LORAWAN)
	{
		port->print("REGION=");
		port->println((int)cfg.lorawan.region);
		port->print("CLASS=");
		port->println(cfg.lorawan.deviceClass == WISBLOCK_CLASS_A ? "A" : cfg.lorawan.deviceClass == WISBLOCK_CLASS_B ? "B"
																													  : "C");
		port->print("ADR=");
		port->println(cfg.lorawan.adrEnabled ? "1" : "0");
		port->print("MASK=");
		{
			char buf[5];
			snprintf(buf, sizeof(buf), "%04X", cfg.lorawan.channelMask);
			port->println(buf);
		}
		port->print("JOINSTATE=");
		port->println((int)lora->joinState());
	}
	else
	{
		port->print("FREQ=");
		port->println(cfg.p2p.frequencyHz);
		port->print("SF=");
		port->println(cfg.p2p.spreadingFactor);
		port->print("BW=");
		port->println((int)cfg.p2p.bandwidth);
		port->print("CR=");
		port->println((int)cfg.p2p.codingRate);
		port->print("CAD=");
		port->println(cfg.p2p.cadEnabled ? "1" : "0");
	}
}

// ---------------------------------------------------------------------------
// Dispatcher
// ---------------------------------------------------------------------------

void WisBlockLoRaAT::processLine(const char *line)
{
	// FIX: AT commands were case-sensitive - "AT+MODE=1" worked, "at+mode=1"
	// didn't, which doesn't match how AT command sets are conventionally
	// expected to behave (case-insensitive is the norm - e.g. Hayes/3GPP AT
	// command sets). Checked and uppercased here, deliberately *after* the
	// unhandledDataCb() branch below would otherwise fire - that path is
	// for arbitrary non-AT application data passed through the same serial
	// stream, which must reach the application byte-for-byte, not
	// uppercased. Only once a line is confirmed to actually be an AT
	// command does it get uppercased, so every subsequent comparison (all
	// already written against uppercase literals) just works regardless of
	// what case the caller sent, without needing every startsWith()/
	// strcmp() call site updated individually. Safe to uppercase
	// unconditionally for every command this parser accepts: every value
	// is either numeric (case has no effect) or hex (uppercase and
	// lowercase hex digits parse identically via parseHex()/strtol()).
	if (!startsWithAtCaseInsensitive(line))
	{
		if (unhandledDataCb)
		{
			unhandledDataCb(line);
		}
		else
		{
			replyError("AT_ERROR"); // expected AT prefix
		}
		return;
	}

	char upperLine[sizeof(lineBuffer)];
	size_t len = strlen(line);
	if (len >= sizeof(upperLine))
	{
		len = sizeof(upperLine) - 1;
	}
	for (size_t i = 0; i < len; i++)
	{
		upperLine[i] = (char)toupper((unsigned char)line[i]);
	}
	upperLine[len] = '\0';

	char *fullLine = upperLine;
	char *cmd = fullLine + 2; // skip "AT"

	if (cmd[0] == '\0')
	{
		replyOk(); // bare "AT" -> liveness check
		return;
	}

	// Splits `cmd` into a bare command name plus an operation, replacing
	// the old design's per-command "=?" / "=" string literals: every
	// handler below is told once whether it was invoked bare ("AT+CMD"),
	// as a query ("AT+CMD=?") or as a set ("AT+CMD=value") instead of
	// re-deriving that itself from raw string comparisons.
	char *eq = strchr(cmd, '=');
	AtOp op;
	char *value = nullptr;
	if (eq)
	{
		*eq = '\0'; // also trims `fullLine` at the '=', which dispatchCustomCommand() relies on below
		value = eq + 1;
		if (strcmp(value, "?") == 0)
		{
			op = AtOp::Query;
			value = nullptr;
		}
		else
		{
			op = AtOp::Write;
		}
	}
	else
	{
		op = AtOp::Run;
	}

	// "ATC+<CMD>" - an application-registered custom command (see
	// addCustomATCommand()); "C+" is RUI3's own prefix convention for
	// custom AT commands (see api.system.atMode.add() in RAKSystem.h),
	// kept here for the same reason the rest of this command set follows
	// RUI3 naming where it can: familiarity for anyone coming from RUI3
	// firmware.
	if (startsWith(cmd, "C+"))
	{
		if (dispatchCustomCommand(fullLine, op, value))
		{
			return;
		}
	}
	else
	{
		for (size_t i = 0; i < atCommandTableSize; i++)
		{
			if (strcmp(atCommandTable[i].name, cmd) == 0)
			{
				(this->*atCommandTable[i].handler)(op, value);
				return;
			}
		}
	}

	replyError("AT_ERROR"); // unknown command
}

bool WisBlockLoRaAT::dispatchCustomCommand(const char *cmd, AtOp op, char *value)
{
	// `cmd` is the full uppercased command, e.g. "ATC+LED" (already trimmed
	// at '=' by processLine()) - skip "ATC+" to get the name applications
	// registered with addCustomATCommand().
	const char *customName = cmd + 4;
	for (uint8_t i = 0; i < MAX_CUSTOM_AT_COMMANDS; i++)
	{
		if (customCommands[i].cmd && strcasecmp(customCommands[i].cmd, customName) == 0)
		{
			const char *args = (op == AtOp::Query) ? "?" : value;
			WisBlockAtStatus status = customCommands[i].handler(*port, cmd, const_cast<char *>(args));
			switch (status)
			{
			case WISBLOCK_AT_OK:
				replyOk();
				break;
			case WISBLOCK_AT_PARAM_ERROR:
				replyError("AT_PARAM_ERROR");
				break;
			default:
				replyError("AT_ERROR");
				break;
			}
			return true;
		}
	}
	return false;
}

bool WisBlockLoRaAT::addCustomATCommand(const char *cmd, const char *usage, CustomAtHandler handler)
{
	if (!cmd || !handler || cmd[0] == '\0')
	{
		return false;
	}
	int freeSlot = -1;
	for (uint8_t i = 0; i < MAX_CUSTOM_AT_COMMANDS; i++)
	{
		if (!customCommands[i].cmd)
		{
			if (freeSlot < 0)
			{
				freeSlot = i;
			}
			continue;
		}
		if (strcasecmp(customCommands[i].cmd, cmd) == 0)
		{
			return false; // already registered
		}
	}
	if (freeSlot < 0)
	{
		return false; // table full (MAX_CUSTOM_AT_COMMANDS)
	}
	customCommands[freeSlot].cmd = cmd;
	customCommands[freeSlot].usage = usage;
	customCommands[freeSlot].handler = handler;
	return true;
}

// ---------------------------------------------------------------------------
// Built-in AT command handlers - one method per row in atCommandTable[]
// above. `value` is only meaningful when op == AtOp::Write; null otherwise.
// Each of these replicates exactly what its old if/else-if branch(es) in
// processLine() used to do, except where noted "CLEANUP:" - see
// Creation-Log-From-Claude-AI.md for the full list of behavioral fixes made
// while doing this rewrite.
// ---------------------------------------------------------------------------

void WisBlockLoRaAT::atNwm(AtOp op, const char *value)
{
	// RUI3 numbering: 0 = P2P_LORA, 1 = LoRaWAN, 2 = P2P_FSK (FSK not
	// implemented by this library - see the write branch below).
	if (op == AtOp::Query)
	{
		port->printf("AT+NWM=");
		port->println(lora->getWorkMode() == WISBLOCK_MODE_LORAWAN ? 1 : 0);
		replyOk();
	}
	else if (op == AtOp::Write)
	{
		int v = atoi(value);
		if (v == 2)
		{
			replyError("AT_PARAM_ERROR"); // P2P_FSK not supported by this library
			return;
		}
		lora->setWorkMode(v == 1 ? WISBLOCK_MODE_LORAWAN : WISBLOCK_MODE_LORA_P2P);
		replyOk();
	}
	else
	{
		replyError("AT_ERROR");
	}
}

void WisBlockLoRaAT::atDevEui(AtOp op, const char *value)
{
	if (op == AtOp::Query)
	{
		port->printf("AT+DEVEUI=");
		printHex(port, lora->getConfig().lorawan.otaa.devEui, 8);
		replyOk();
	}
	else if (op == AtOp::Write)
	{
		uint8_t eui[8];
		if (!parseHex(value, eui, 8))
		{
			replyError("AT_PARAM_ERROR"); // bad hex, expected 16 chars
			return;
		}
		WisBlockOTAAKeys keys = lora->getConfig().lorawan.otaa;
		memcpy(keys.devEui, eui, 8);
		lora->setOTAAKeys(keys.devEui, keys.joinEui, keys.appKey);
		replyOk();
	}
	else
	{
		replyError("AT_ERROR");
	}
}

// Serves both the "+APPEUI" and "+JOINEUI" rows in atCommandTable[] - the
// old code treated them as synonyms too (RUI3 itself calls this field
// AppEUI; the LoRaWAN 1.1 spec renamed it JoinEUI - this library accepts
// either name for the same underlying value).
void WisBlockLoRaAT::atAppEui(AtOp op, const char *value)
{
	if (op == AtOp::Query)
	{
		port->printf("AT+APPEUI=");
		printHex(port, lora->getConfig().lorawan.otaa.joinEui, 8);
		replyOk();
	}
	else if (op == AtOp::Write)
	{
		uint8_t eui[8];
		if (!parseHex(value, eui, 8))
		{
			replyError("AT_PARAM_ERROR"); // bad hex, expected 16 chars
			return;
		}
		WisBlockOTAAKeys keys = lora->getConfig().lorawan.otaa;
		memcpy(keys.joinEui, eui, 8);
		lora->setOTAAKeys(keys.devEui, keys.joinEui, keys.appKey);
		replyOk();
	}
	else
	{
		replyError("AT_ERROR");
	}
}

void WisBlockLoRaAT::atAppKey(AtOp op, const char *value)
{
	if (op == AtOp::Query)
	{
		// Plaintext readback is intentional (confirmed by the maintainer,
		// overriding the SECURITY-flavored comment this line used to carry
		// - see Creation-Log-From-Claude-AI.md's 2026-09-22 entries for the
		// full back-and-forth). Left as-is: printHex(port, ...16) below.
		port->printf("AT+APPKEY=");
		printHex(port, lora->getConfig().lorawan.otaa.appKey, 16);
		replyOk();
	}
	else if (op == AtOp::Write)
	{
		uint8_t key[16];
		if (!parseHex(value, key, 16))
		{
			replyError("AT_PARAM_ERROR"); // bad hex, expected 32 chars
			return;
		}
		WisBlockOTAAKeys keys = lora->getConfig().lorawan.otaa;
		memcpy(keys.appKey, key, 16);
		lora->setOTAAKeys(keys.devEui, keys.joinEui, keys.appKey);
		replyOk();
	}
	else
	{
		replyError("AT_ERROR");
	}
}

void WisBlockLoRaAT::atDevAddr(AtOp op, const char *value)
{
	if (op == AtOp::Query)
	{
		uint32_t addr = lora->getConfig().lorawan.abp.devAddr;
		uint8_t bytes[4] = {(uint8_t)(addr >> 24), (uint8_t)(addr >> 16), (uint8_t)(addr >> 8), (uint8_t)addr};
		port->printf("AT+DEVADDR=");
		printHex(port, bytes, 4);
		replyOk();
	}
	else if (op == AtOp::Write)
	{
		uint8_t addrBytes[4];
		if (!parseHex(value, addrBytes, 4))
		{
			replyError("AT_PARAM_ERROR"); // bad hex, expected 8 chars
			return;
		}
		uint32_t addr = ((uint32_t)addrBytes[0] << 24) | ((uint32_t)addrBytes[1] << 16) |
						((uint32_t)addrBytes[2] << 8) | addrBytes[3];
		WisBlockABPKeys keys = lora->getConfig().lorawan.abp;
		lora->setABPKeys(addr, keys.nwkSKey, keys.appSKey);
		replyOk();
	}
	else
	{
		replyError("AT_ERROR");
	}
}

void WisBlockLoRaAT::atNwkSKey(AtOp op, const char *value)
{
	if (op == AtOp::Query)
	{
		// Plaintext readback is intentional - same as +APPKEY=? above.
		//
		// CLEANUP (kept - unrelated to the plaintext-vs-masked question
		// above): this used to echo its tag as "AT+NWSKEY=" (missing the
		// "K"), which didn't match the command's own name, AT+NWKSKEY.
		port->printf("AT+NWKSKEY=");
		printHex(port, lora->getConfig().lorawan.abp.nwkSKey, 16);
		replyOk();
	}
	else if (op == AtOp::Write)
	{
		uint8_t key[16];
		if (!parseHex(value, key, 16))
		{
			replyError("AT_PARAM_ERROR"); // bad hex, expected 32 chars
			return;
		}
		WisBlockABPKeys keys = lora->getConfig().lorawan.abp;
		memcpy(keys.nwkSKey, key, 16);
		lora->setABPKeys(keys.devAddr, keys.nwkSKey, keys.appSKey);
		replyOk();
	}
	else
	{
		replyError("AT_ERROR");
	}
}

void WisBlockLoRaAT::atAppSKey(AtOp op, const char *value)
{
	if (op == AtOp::Query)
	{
		// Plaintext readback is intentional - same as +APPKEY=? above.
		port->printf("AT+APPSKEY=");
		printHex(port, lora->getConfig().lorawan.abp.appSKey, 16);
		replyOk();
	}
	else if (op == AtOp::Write)
	{
		uint8_t key[16];
		if (!parseHex(value, key, 16))
		{
			replyError("AT_PARAM_ERROR"); // bad hex, expected 32 chars
			return;
		}
		WisBlockABPKeys keys = lora->getConfig().lorawan.abp;
		memcpy(keys.appSKey, key, 16);
		lora->setABPKeys(keys.devAddr, keys.nwkSKey, keys.appSKey);
		replyOk();
	}
	else
	{
		replyError("AT_ERROR");
	}
}

void WisBlockLoRaAT::atBand(AtOp op, const char *value)
{
	if (op == AtOp::Query)
	{
		int idx = regionToBandIndex(lora->getConfig().lorawan.region);
		if (idx < 0)
		{
			replyError("current region has no RUI3 band index");
			return;
		}
		port->printf("AT+BAND=");
		port->println(idx);
		replyOk();
	}
	else if (op == AtOp::Write)
	{
		WisBlockRegion region;
		if (!bandIndexToRegion(atoi(value), region))
		{
			replyError("unsupported band index - EU433/LA915 not built into this LBM vendoring");
			return;
		}
		lora->setRegion(region);
		replyOk();
	}
	else
	{
		replyError("AT_ERROR");
	}
}

void WisBlockLoRaAT::atMask(AtOp op, const char *value)
{
	if (op == AtOp::Query)
	{
		// Only meaningful for US915/AU915/CN470/CN470_RP_1_0 - see
		// LoRaWANEngine::setChannelMask()'s doc comment. Matches RUI3's
		// AT+MASK: 4 hex digits, bit N (0-indexed) = sub-band N+1 enabled,
		// 0000 = all channels (no restriction).
		char buf[5];
		snprintf(buf, sizeof(buf), "%04X", lora->getChannelMask());
		port->printf("AT+MASK=");
		port->println(buf);
		replyOk();
	}
	else if (op == AtOp::Write)
	{
		char *end = nullptr;
		unsigned long mask = strtoul(value, &end, 16);
		if (end == value || mask > 0xFFFF)
		{
			replyError("AT_PARAM_ERROR"); // expected 4 hex digits
			return;
		}
		lora->setChannelMask((uint16_t)mask);
		replyOk();
	}
	else
	{
		replyError("AT_ERROR");
	}
}

void WisBlockLoRaAT::atLbt(AtOp op, const char *value)
{
	if (op == AtOp::Query)
	{
		port->printf("AT+LBT=");
		port->println(lora->getLbtEnabled() ? 1 : 0);
		replyOk();
	}
	else if (op == AtOp::Write)
	{
		lora->setLbtEnabled(atoi(value) != 0);
		replyOk();
	}
	else
	{
		replyError("AT_ERROR");
	}
}

void WisBlockLoRaAT::atLbtRssi(AtOp op, const char *value)
{
	if (op == AtOp::Query)
	{
		port->printf("AT+LBTRSSI=");
		port->println(lora->getLbtThreshold());
		replyOk();
	}
	else if (op == AtOp::Write)
	{
		char *end = nullptr;
		long thresholdDbm = strtol(value, &end, 10);
		if (end == value)
		{
			replyError("AT_PARAM_ERROR");
			return;
		}
		lora->setLbtThreshold((int16_t)thresholdDbm);
		replyOk();
	}
	else
	{
		replyError("AT_ERROR");
	}
}

void WisBlockLoRaAT::atLbtScanTime(AtOp op, const char *value)
{
	if (op == AtOp::Query)
	{
		port->printf("AT+LBTSCANTIME=");
		port->println(lora->getLbtScanTime());
		replyOk();
	}
	else if (op == AtOp::Write)
	{
		char *end = nullptr;
		unsigned long scanTimeMs = strtoul(value, &end, 10);
		if (end == value)
		{
			replyError("AT_PARAM_ERROR");
			return;
		}
		lora->setLbtScanTime((uint32_t)scanTimeMs);
		replyOk();
	}
	else
	{
		replyError("AT_ERROR");
	}
}

void WisBlockLoRaAT::atPgSlot(AtOp op, const char *value)
{
	if (op == AtOp::Query)
	{
		port->printf("AT+PGSLOT=");
		port->println(lora->getPingSlotPeriodicity());
		replyOk();
	}
	else if (op == AtOp::Write)
	{
		lora->setPingSlotPeriodicity((uint8_t)atoi(value));
		replyOk();
	}
	else
	{
		replyError("AT_ERROR");
	}
}

void WisBlockLoRaAT::atBFreq(AtOp op, const char *value)
{
	// Query-only, same as the old code (there was never a bare/write form).
	if (op != AtOp::Query)
	{
		replyError("AT_ERROR");
		return;
	}
	uint32_t frequencyHz = 0;
	uint8_t dr = 0;
	lora->getBeaconFrequencyAndDr(frequencyHz, dr);
	port->printf("AT+BFREQ=");
	port->print(dr);
	port->print(", ");
	port->println(frequencyHz / 1000000.0, 3); // Hz -> MHz, matching RUI3's own "3, 869.525" style
	replyOk();
}

void WisBlockLoRaAT::atBTime(AtOp op, const char *value)
{
	if (op != AtOp::Query)
	{
		replyError("AT_ERROR");
		return;
	}
	port->printf("AT+BTIME=");
	port->println(lora->getBeaconTime());
	replyOk();
}

void WisBlockLoRaAT::atDr(AtOp op, const char *value)
{
	if (op == AtOp::Query)
	{
		port->printf("AT+DR=");
		port->println(lora->getConfig().lorawan.dataRate);
		replyOk();
	}
	else if (op == AtOp::Write)
	{
		// See LoRaWANEngine::setADR()'s doc comment: a false return here
		// doesn't mean the command failed - the requested DR is stored and
		// retried automatically after every uplink - just that it isn't
		// active on the radio yet, most commonly right after a fresh join
		// before the network's channel-widening MAC commands have landed.
		if (!lora->setDataRate((uint8_t)atoi(value)))
		{
			port->println("PENDING - not yet valid for the currently enabled channels, will retry automatically");
		}
		replyOk();
	}
	else
	{
		replyError("AT_ERROR");
	}
}

void WisBlockLoRaAT::atClass(AtOp op, const char *value)
{
	if (op == AtOp::Query)
	{
		WisBlockDeviceClass dc = lora->getConfig().lorawan.deviceClass;
		port->printf("AT+CLASS=");
		// CLEANUP: this used to call reply() (which already prints "OK")
		// and then replyOk() again right after, sending two "OK\r\n" lines
		// back for a single query. reply() alone is correct.
		reply(dc == WISBLOCK_CLASS_B ? "B" : dc == WISBLOCK_CLASS_C ? "C"
																	 : "A");
	}
	else if (op == AtOp::Write)
	{
		char c = value[0];
		WisBlockDeviceClass dc = (c == 'B' || c == 'b') ? WISBLOCK_CLASS_B : (c == 'C' || c == 'c') ? WISBLOCK_CLASS_C
																									  : WISBLOCK_CLASS_A;
		lora->setDeviceClass(dc);
		replyOk();
	}
	else
	{
		replyError("AT_ERROR");
	}
}

void WisBlockLoRaAT::atNjm(AtOp op, const char *value)
{
	if (op == AtOp::Query)
	{
		// RUI3 numbering: 0 = ABP, 1 = OTAA - inverted from this library's
		// own WisBlockJoinMode enum (WISBLOCK_JOIN_OTAA = 0), so this
		// translates rather than casting directly.
		port->printf("AT+NJM=");
		port->println(lora->getConfig().lorawan.joinMode == WISBLOCK_JOIN_ABP ? 0 : 1);
		replyOk();
	}
	else if (op == AtOp::Write)
	{
		lora->setJoinMode(atoi(value) == 0 ? WISBLOCK_JOIN_ABP : WISBLOCK_JOIN_OTAA);
		replyOk();
	}
	else
	{
		replyError("AT_ERROR");
	}
}

void WisBlockLoRaAT::atJoin(AtOp op, const char *value)
{
	if (op == AtOp::Run)
	{
		lora->join();
		replyOk();
	}
	else if (op == AtOp::Query)
	{
		// AT+JOIN=w:x:y:z - w: currently joining/joined; x: auto-join on power-up;
		// y: reattempt interval (s); z: max join attempts (0 = unlimited).
		port->printf("AT+JOIN=");
		port->print(lora->isJoined() || lora->joinState() == WISBLOCK_JOIN_IN_PROGRESS ? 1 : 0);
		port->print(":");
		port->print(lora->getAutoJoin() ? 1 : 0);
		port->print(":");
		port->print(lora->getJoinReattemptInterval());
		port->print(":");
		port->println(lora->getMaxJoinAttempts());
		replyOk();
	}
	else // AtOp::Write
	{
		// Parameters are positional and all optional after the first - only w is required;
		// x/y/z each apply (and persist) only if actually present, matching RUI3's own
		// "configure then join" AT+JOIN=w:x:y:z, where a shorter form leaves the rest as
		// previously configured rather than resetting them to default.
		char buf[32];
		strncpy(buf, value, sizeof(buf) - 1);
		buf[sizeof(buf) - 1] = '\0';

		char *tok = strtok(buf, ":");
		if (!tok)
		{
			replyError("AT_PARAM_ERROR");
			return;
		}
		int joinNow = atoi(tok);

		tok = strtok(nullptr, ":");
		if (tok)
		{
			lora->setAutoJoin(atoi(tok) != 0);
		}
		tok = strtok(nullptr, ":");
		if (tok)
		{
			lora->setJoinReattemptInterval((uint8_t)atoi(tok));
		}
		tok = strtok(nullptr, ":");
		if (tok)
		{
			lora->setMaxJoinAttempts((uint8_t)atoi(tok));
		}

		if (joinNow != 0)
		{
			lora->join();
		}
		else
		{
			lora->stopJoin();
		}
		replyOk();
	}
}

void WisBlockLoRaAT::atNjs(AtOp op, const char *value)
{
	if (op != AtOp::Query)
	{
		replyError("AT_ERROR");
		return;
	}
	// RUI3: plain joined/not-joined boolean, not this library's own
	// multi-state WisBlockJoinState (IDLE/IN_PROGRESS/SUCCEEDED/FAILED)
	// - isJoined() is the right underlying call to match it, not a cast.
	port->printf("AT+NJS=");
	port->println(lora->isJoined() ? 1 : 0);
	replyOk();
}

void WisBlockLoRaAT::atAdr(AtOp op, const char *value)
{
	if (op == AtOp::Query)
	{
		port->printf("AT+ADR=");
		port->println(lora->getConfig().lorawan.adrEnabled ? "1" : "0");
		replyOk();
	}
	else if (op == AtOp::Write)
	{
		// See LoRaWANEngine::setADR()'s doc comment - same meaning as the
		// AT+DR= case above: a false return means the request is stored
		// and will retry automatically, not that anything failed outright.
		if (!lora->setADR(atoi(value) != 0))
		{
			port->println("PENDING - not yet valid for the currently enabled channels, will retry automatically");
		}
		replyOk();
	}
	else
	{
		replyError("AT_ERROR");
	}
}

void WisBlockLoRaAT::atTxp(AtOp op, const char *value)
{
	if (op == AtOp::Query)
	{
		port->printf("AT+TXP=");
		port->println(lora->getConfig().lorawan.txPower);
		replyOk();
	}
	else if (op == AtOp::Write)
	{
		lora->setTxPower((uint8_t)atoi(value));
		replyOk();
	}
	else
	{
		replyError("AT_ERROR");
	}
}

void WisBlockLoRaAT::atCfm(AtOp op, const char *value)
{
	if (op == AtOp::Query)
	{
		port->printf("AT+CFM=");
		port->println(lora->getConfig().lorawan.confirmedUplinks ? "1" : "0");
		replyOk();
	}
	else if (op == AtOp::Write)
	{
		lora->setConfirmedUplinks(atoi(value) != 0);
		replyOk();
	}
	else
	{
		replyError("AT_ERROR");
	}
}

void WisBlockLoRaAT::atFPending(AtOp op, const char *value)
{
	if (op == AtOp::Query)
	{
		// Library-specific (no RUI3 equivalent) - see
		// WisBlockLoRaWANSettings::fetchPendingDownlinks's doc comment.
		port->printf("AT+FPENDING=");
		port->println(lora->getFetchPendingDownlinks() ? "1" : "0");
		replyOk();
	}
	else if (op == AtOp::Write)
	{
		lora->setFetchPendingDownlinks(atoi(value) != 0);
		replyOk();
	}
	else
	{
		replyError("AT_ERROR");
	}
}

void WisBlockLoRaAT::atSend(AtOp op, const char *value)
{
	// Write-only, same as the old code (there was never a bare/query form).
	if (op != AtOp::Write)
	{
		replyError("AT_ERROR");
		return;
	}
	const char *args = value;
	const char *colon = strchr(args, ':');
	if (!colon)
	{
		replyError("AT_PARAM_ERROR"); // expected <port>:<hexpayload>
		return;
	}
	char portStr[8] = {0};
	size_t portLen = colon - args;
	if (portLen >= sizeof(portStr))
	{
		replyError("AT_PARAM_ERROR"); // port too long
		return;
	}
	memcpy(portStr, args, portLen);
	uint8_t port_ = (uint8_t)atoi(portStr);

	const char *hex = colon + 1;
	size_t hexLen = strlen(hex);
	if (hexLen % 2 != 0 || hexLen / 2 > 242)
	{
		replyError("AT_PARAM_ERROR"); // bad payload hex
		return;
	}
	uint8_t payload[242];
	if (!parseHex(hex, payload, hexLen / 2))
	{
		replyError("AT_PARAM_ERROR"); // bad payload hex
		return;
	}
	bool ok = lora->sendLoRaWAN(port_, payload, (uint8_t)(hexLen / 2));
	ok ? replyOk() : replyError("AT_NO_NETWORK_JOINED"); // send failed (not joined?)
}

void WisBlockLoRaAT::atLinkCheck(AtOp op, const char *value)
{
	if (op == AtOp::Query)
	{
		// RUI3 semantics: this queries the current MODE (0/1/2), not the
		// last check's result - see setLinkCheckMode()'s doc comment. The
		// actual result, once one arrives, comes asynchronously via
		// onLinkCheckAnswer() - poll-style access to it via
		// getLinkCheckResult() is still available at the C++ layer, just
		// no longer surfaced under this AT command name (RUI3 doesn't
		// have an equivalent poll command either - it's event-driven
		// there too).
		port->printf("AT+LINKCHECK=");
		port->println(lora->getLinkCheckMode());
		replyOk();
	}
	else if (op == AtOp::Write)
	{
		// AT+LINKCHECK=<0/1/2> - 0 disabled, 1 request once (consumed on
		// the next uplink), 2 request automatically on every uplink from
		// here on. See LoRaWANEngine::setLinkCheckMode()'s doc comment for
		// exactly how modes 1/2 get applied at send() time.
		uint8_t mode = (uint8_t)atoi(value);
		if (mode > 2)
		{
			replyError("AT_PARAM_ERROR"); // bad mode - expected 0, 1, or 2
			return;
		}
		lora->setLinkCheckMode(mode);
		replyOk();
	}
	else
	{
		replyError("AT_ERROR");
	}
}

void WisBlockLoRaAT::atTimeReq(AtOp op, const char *value)
{
	// Run-only action, same as the old code.
	if (op != AtOp::Run)
	{
		replyError("AT_ERROR");
		return;
	}
	// CLEANUP: this used to start a reply with port->printf("AT+TIMEREQ=")
	// and never finish it (no value, no newline) before replyOk() printed
	// "OK\r\n" right after - producing a single malformed
	// "AT+TIMEREQ=OK\r\n" line instead of a clean "OK\r\n". The result
	// itself only ever arrives asynchronously (see requestDeviceTime()'s
	// doc comment), so there was never a value to print here - removed.
	lora->requestDeviceTime();
	replyOk();
}

void WisBlockLoRaAT::atP2p(AtOp op, const char *value)
{
	if (op == AtOp::Query)
	{
		// Same field order as the write branch below: <freqHz>:<sf>:<bw>:<cr>:<preamble>:<txpower>
		const WisBlockP2PSettings &s = lora->getP2PSettings();
		port->printf("AT+P2P=");
		port->print(s.frequencyHz);
		port->print(":");
		port->print(s.spreadingFactor);
		port->print(":");
		port->print((int)s.bandwidth);
		port->print(":");
		port->print((int)s.codingRate);
		port->print(":");
		port->print(s.preambleLength);
		port->print(":");
		port->println(s.txPowerDbm);
		replyOk();
	}
	else if (op == AtOp::Write)
	{
		// AT+P2P=<freqHz>:<sf>:<bw>:<cr>:<preamble>:<txpower>
		char buf[96];
		strncpy(buf, value, sizeof(buf) - 1);
		buf[sizeof(buf) - 1] = '\0';

		char *tok = strtok(buf, ":");
		uint32_t freq = tok ? strtoul(tok, nullptr, 10) : 0;
		tok = strtok(nullptr, ":");
		uint8_t sf = tok ? (uint8_t)atoi(tok) : 7;
		tok = strtok(nullptr, ":");
		WisBlockP2PBandwidth bw = tok ? (WisBlockP2PBandwidth)atoi(tok) : WISBLOCK_BW_125;
		tok = strtok(nullptr, ":");
		WisBlockP2PCodingRate cr = tok ? (WisBlockP2PCodingRate)atoi(tok) : WISBLOCK_CR_4_5;
		tok = strtok(nullptr, ":");
		uint16_t preamble = tok ? (uint16_t)atoi(tok) : 8;
		tok = strtok(nullptr, ":");
		int8_t txp = tok ? (int8_t)atoi(tok) : 14;

		if (freq == 0)
		{
			replyError("AT_PARAM_ERROR"); // bad frequency
			return;
		}
		lora->setP2PFrequency(freq);
		lora->setP2PSpreadingFactor(sf);
		lora->setP2PBandwidth(bw);
		lora->setP2PCodingRate(cr);
		lora->setP2PPreambleLength(preamble);
		lora->setP2PTxPower(txp);
		replyOk();
	}
	else
	{
		replyError("AT_ERROR");
	}
}

void WisBlockLoRaAT::atCad(AtOp op, const char *value)
{
	if (op == AtOp::Query)
	{
		port->printf("AT+CAD=");
		port->println(lora->getP2PSettings().cadEnabled ? "1" : "0");
		replyOk();
	}
	else if (op == AtOp::Write)
	{
		lora->setP2PCad(atoi(value) != 0);
		replyOk();
	}
	else
	{
		replyError("AT_ERROR");
	}
}

void WisBlockLoRaAT::atRxBoost(AtOp op, const char *value)
{
	if (op == AtOp::Query)
	{
		port->printf("AT+RXBOOST=");
		port->println(lora->getP2PSettings().rxBoostedGainEnabled ? "1" : "0");
		replyOk();
	}
	else if (op == AtOp::Write)
	{
		// AT+RXBOOST=<0/1> - see WisBlockP2PSettings::rxBoostedGainEnabled's
		// doc comment for the RX-current-vs-sensitivity tradeoff this
		// controls.
		lora->setP2PRxBoostedGain(atoi(value) != 0);
		replyOk();
	}
	else
	{
		replyError("AT_ERROR");
	}
}

void WisBlockLoRaAT::atPSend(AtOp op, const char *value)
{
	// Write-only, same as the old code.
	if (op != AtOp::Write)
	{
		replyError("AT_ERROR");
		return;
	}
	const char *hex = value;
	size_t hexLen = strlen(hex);
	if (hexLen % 2 != 0 || hexLen / 2 > 255)
	{
		replyError("AT_PARAM_ERROR"); // bad payload hex
		return;
	}
	uint8_t payload[255];
	if (!parseHex(hex, payload, hexLen / 2))
	{
		replyError("AT_PARAM_ERROR"); // bad payload hex
		return;
	}
	bool ok = lora->sendP2P(payload, (uint8_t)(hexLen / 2));
	ok ? replyOk() : replyError("AT_ERROR"); // send failed
}

void WisBlockLoRaAT::atPRecv(AtOp op, const char *value)
{
	// Write-only, same as the old code.
	if (op != AtOp::Write)
	{
		replyError("AT_ERROR");
		return;
	}
	uint32_t timeout = strtoul(value, nullptr, 10);
	lora->startP2PReceive(timeout);
	replyOk();
}

void WisBlockLoRaAT::atPRecvDc(AtOp op, const char *value)
{
	// Write-only, same as the old code.
	if (op != AtOp::Write)
	{
		replyError("AT_ERROR");
		return;
	}
	if (startsWith(value, "AUTO"))
	{
		// AT+PRECVDC=AUTO or AT+PRECVDC=AUTO:<txPreambleLengthSymbols> -
		// computes rxTimeMs/sleepTimeMs instead of requiring the caller to
		// work out the numbers themselves. Prefer giving the transmitter's
		// actual preamble length explicitly (AUTO:<n>) whenever you know
		// it - see WisBlockLoRaWAN::computeP2PRxDutyCycleTiming()'s doc
		// comment for why the plain AUTO form (falling back to this
		// radio's own configured preambleLength) is a less reliable
		// stand-in for it.
		uint32_t rxTimeMs = 0, sleepTimeMs = 0;
		const char *afterAuto = value + 4; // strlen("AUTO")
		bool ok;
		if (afterAuto[0] == ':' && afterAuto[1] != '\0')
		{
			uint16_t txPreambleLengthSymbols = (uint16_t)strtoul(afterAuto + 1, nullptr, 10);
			ok = lora->computeP2PRxDutyCycleTiming(txPreambleLengthSymbols, rxTimeMs, sleepTimeMs);
		}
		else
		{
			ok = lora->computeP2PRxDutyCycleTiming(rxTimeMs, sleepTimeMs);
		}
		if (!ok)
		{
			replyError("AT_PARAM_ERROR"); // preamble too short for any usable duty-cycle window
			return;
		}
		lora->startP2PReceiveDutyCycle(rxTimeMs, sleepTimeMs);
		replyOk();
		return;
	}

	// AT+PRECVDC=<rxTimeMs>:<sleepTimeMs> - see
	// LoRaP2PEngine::startReceiveDutyCycle()'s doc comment for the full
	// picture of what this does differently from AT+PRECV.
	char buf[32];
	strncpy(buf, value, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';

	char *tok = strtok(buf, ":");
	uint32_t rxTimeMs = tok ? strtoul(tok, nullptr, 10) : 0;
	tok = strtok(nullptr, ":");
	uint32_t sleepTimeMs = tok ? strtoul(tok, nullptr, 10) : 0;

	if (rxTimeMs == 0 || sleepTimeMs == 0)
	{
		replyError("AT_PARAM_ERROR"); // bad rxTimeMs/sleepTimeMs
		return;
	}
	lora->startP2PReceiveDutyCycle(rxTimeMs, sleepTimeMs);
	replyOk();
}

void WisBlockLoRaAT::atLowPower(AtOp op, const char *value)
{
	if (op == AtOp::Query)
	{
		port->printf("AT+LOWPOWER=");
		port->println(lora->isLowPowerEnabled() ? "1" : "0");
		replyOk();
	}
	else if (op == AtOp::Write)
	{
		lora->setLowPowerEnabled(atoi(value) != 0);
		replyOk();
	}
	else
	{
		replyError("AT_ERROR");
	}
}

void WisBlockLoRaAT::atSave(AtOp op, const char *value)
{
	if (op != AtOp::Run)
	{
		replyError("AT_ERROR");
		return;
	}
	lora->saveConfig() ? replyOk() : replyError("AT_ERROR"); // flash write failed
}

void WisBlockLoRaAT::atRestore(AtOp op, const char *value)
{
	if (op != AtOp::Run)
	{
		replyError("AT_ERROR");
		return;
	}
	lora->restoreConfig() ? replyOk() : replyError("AT_ERROR"); // no saved config found, defaults loaded
}

void WisBlockLoRaAT::atFactory(AtOp op, const char *value)
{
	// Run-only action ("AT+FACTORY"), same shape as AT+SAVE/AT+RESTORE/ATZ.
	if (op != AtOp::Run)
	{
		replyError("AT_ERROR");
		return;
	}
	// Snapshots the CURRENT live configuration - not compiled-in struct
	// defaults - into a separate "factory" flash slot untouched by ordinary
	// AT+SAVE/AT+RESTORE traffic (see WisBlockLoRaWAN::saveFactoryDefaults()'s
	// doc comment). Intended one-time production use, see this library's
	// README "Production flow" section: flash firmware, set this unit's
	// unique AT+DEVEUI= (everything else - JoinEUI/AppKey/region/mode/join
	// mode - is left at its compiled-in default, see WisBlockLoRaWANTypes.h),
	// then AT+FACTORY. Resets the device afterward so the next boot (and
	// every ATR from here on) starts from a clean, fully-reinitialized
	// engine rather than whatever radio/session state was live in this boot.
	if (!lora->saveFactoryDefaults())
	{
		replyError("AT_ERROR"); // flash write failed
		return;
	}
	// Sent *before* resetting - sd_nvic_SystemReset()/esp_restart() below
	// don't return, so anything printed after them never reaches the host
	// (unlike atZ()'s/atBoot()'s equivalent calls above, which print their
	// OK - unreachably - after the reset call instead of before it).
	replyOk();
	port->flush();
#ifdef NRF52_SERIES
	sd_nvic_SystemReset();
#endif
#ifdef ESP32
	esp_restart();
#endif
}

void WisBlockLoRaAT::atR(AtOp op, const char *value)
{
	// Run-only action ("ATR"), same shape as AT+SAVE/AT+RESTORE/ATZ above.
	if (op != AtOp::Run)
	{
		replyError("AT_ERROR");
		return;
	}
	// Copies the factory backup saved by AT+FACTORY back over the regular
	// *user* config, both in RAM and on flash (restoreFactoryDefaults()
	// itself calls wisblockConfigSave() as its last step) - the "undo
	// whatever I've broken" command. Deliberately does NOT reset the device
	// afterward, matching AT+RESTORE's existing no-reset behavior above:
	// restoreFactoryDefaults() already reapplies everything live via the
	// same applyLoRaWANSettings()/applyP2PSettings() calls AT+RESTORE uses.
	// Only AT+FACTORY resets - it runs once, in a controlled production
	// step, where a clean reboot is expected/convenient; ATR is meant to be
	// safe to run anytime a field device looks broken.
	if (!lora->restoreFactoryDefaults())
	{
		replyError("no factory backup saved yet - run AT+FACTORY first");
		return;
	}
	replyOk();
}

void WisBlockLoRaAT::atStatus(AtOp op, const char *value)
{
	if (op != AtOp::Run)
	{
		replyError("AT_ERROR");
		return;
	}
	handleStatusQuery();
	replyOk();
}

void WisBlockLoRaAT::atHwModel(AtOp op, const char *value)
{
	if (op != AtOp::Query)
	{
		replyError("AT_ERROR");
		return;
	}
#ifdef NRF52_SERIES
	port->println("rak4630");
#elif defined(ARDUINO_ARCH_ESP32)
	port->println("rak3112");
#elif defined(ARDUINO_ARCH_RP2040)
	// CLEANUP: RAK11310/RP2040 was missing from this #ifdef entirely (it's
	// present below for AT+VER=?, and this library otherwise supports
	// RP2040 throughout - see WisBlockLoRaBoards.h's WISBLOCK_BOARD_NAME) -
	// AT+HWMODEL=? on an RP2040 build used to print nothing at all before
	// replying OK. Added for parity with the other two platforms.
	port->println("rak11310");
#endif
	replyOk();
}

void WisBlockLoRaAT::atHwId(AtOp op, const char *value)
{
	if (op != AtOp::Query)
	{
		replyError("AT_ERROR");
		return;
	}
#ifdef NRF52_SERIES
	port->println("nrf52840");
#elif defined(ARDUINO_ARCH_ESP32)
	port->println("esp32-s3");
#elif defined(ARDUINO_ARCH_RP2040)
	// CLEANUP: same RP2040 gap as AT+HWMODEL=? above.
	port->println("rp2040");
#endif
	replyOk();
}

void WisBlockLoRaAT::atSn(AtOp op, const char *value)
{
	if (op != AtOp::Query)
	{
		replyError("AT_ERROR");
		return;
	}
	uint8_t id[8];
#ifdef NRF52_SERIES
	id[7] = ((*(uint32_t *)ID1));
	id[6] = ((*(uint32_t *)ID1)) >> 8;
	id[5] = ((*(uint32_t *)ID1)) >> 16;
	id[4] = ((*(uint32_t *)ID1)) >> 24;
	id[3] = ((*(uint32_t *)ID2));
	id[2] = ((*(uint32_t *)ID2)) >> 8;
	id[1] = ((*(uint32_t *)ID2)) >> 16;
	id[0] = ((*(uint32_t *)ID2)) >> 24;
#elif defined(ARDUINO_ARCH_ESP32)
	uint64_t uniqueId = ESP.getEfuseMac();
	// Using ESP32 MAC (48 bytes only, so upper 2 bytes will be 0)
	id[7] = (uint8_t)(uniqueId >> 56);
	id[6] = (uint8_t)(uniqueId >> 48);
	id[5] = (uint8_t)(uniqueId >> 40);
	id[4] = (uint8_t)(uniqueId >> 32);
	id[3] = (uint8_t)(uniqueId >> 24);
	id[2] = (uint8_t)(uniqueId >> 16);
	id[1] = (uint8_t)(uniqueId >> 8);
	id[0] = (uint8_t)(uniqueId);
#elif defined(ARDUINO_ARCH_RP2040)
	// CLEANUP: same RP2040 gap as AT+HWMODEL=?/AT+HWID=? above, but worse
	// here - this platform fell all the way through this #ifdef with
	// `id[]` never written, so AT+SN=? printed 8 bytes of uninitialized
	// stack memory on RAK11310 builds. pico_get_unique_board_id()
	// (pico/unique_id.h, part of the arduino-pico core) is this platform's
	// documented equivalent of the nRF52 FICR/ESP32 eFuse MAC reads above.
	// Not build- or hardware-tested as part of this change (no RP2040
	// toolchain available) - please verify on real RAK11310 hardware.
	pico_unique_board_id_t board_id;
	pico_get_unique_board_id(&board_id);
	memcpy(id, board_id.id, 8);
#endif
	port->printf("AT+SN=");
	printHex(port, id, 8);
	replyOk();
}

void WisBlockLoRaAT::atVer(AtOp op, const char *value)
{
	if (op != AtOp::Query)
	{
		replyError("AT_ERROR");
		return;
	}
#ifdef NRF52_SERIES
	port->println("AT+VER=RUI_comp_" WISBLOCK_LORAWAN_VERSION_STRING "_RAK4631");
#elif defined(ARDUINO_ARCH_ESP32)
	port->println("AT+VER=RUI_comp_" WISBLOCK_LORAWAN_VERSION_STRING "_RAK3312");
#elif defined(ARDUINO_ARCH_RP2040)
	port->println("AT+VER=RUI_comp_" WISBLOCK_LORAWAN_VERSION_STRING "_RAK11310");
#endif
	replyOk();
}

void WisBlockLoRaAT::atAlias(AtOp op, const char *value)
{
	if (op == AtOp::Query)
	{
		port->printf("AT+ALIAS=");
		port->println(lora->getAlias());
		replyOk();
	}
	else if (op == AtOp::Write)
	{
		if (!lora->setAlias(value))
		{
			replyError("AT_PARAM_ERROR"); // NULL or longer than RUI3's 16-character limit
			return;
		}
		replyOk();
	}
	else
	{
		replyError("AT_ERROR");
	}
}

void WisBlockLoRaAT::atFirmwareVer(AtOp op, const char *value)
{
	if (op == AtOp::Query)
	{
		port->printf("AT+FIRMWAREVER=");
		port->println(lora->getFirmwareVer());
		replyOk();
	}
	else if (op == AtOp::Write)
	{
		if (!lora->setFirmwareVer(value))
		{
			replyError("AT_PARAM_ERROR"); // NULL or longer than RUI3's 16-character limit
			return;
		}
		replyOk();
	}
	else
	{
		replyError("AT_ERROR");
	}
}

void WisBlockLoRaAT::atZ(AtOp op, const char *value)
{
	// Run-only action ("ATZ"), same as the old code.
	if (op != AtOp::Run)
	{
		replyError("AT_ERROR");
		return;
	}
#ifdef NRF52_SERIES
	sd_nvic_SystemReset();
#endif
#ifdef ESP32
	esp_restart();
#endif
	replyOk();
}

void WisBlockLoRaAT::atBoot(AtOp op, const char *value)
{
	// Run-only action ("AT+BOOT"), same as the old code.
	if (op != AtOp::Run)
	{
		replyError("AT_ERROR");
		return;
	}
#if defined NRF52_SERIES
	NRF_POWER->GPREGRET = 0x57; // 0xA8 OTA, 0x4e Serial, 0x57 UF2
	sd_nvic_SystemReset();		// or NVIC_SystemReset();
#endif
#if defined ESP32
	// No way to go into bootloader programmatically, just restart
	ESP.restart();
#endif
	replyOk();
}

// ---------------------------------------------------------------------------
// Background RX: USB CDC RX callback hooks per platform. Both call straight
// into onBackgroundRxData() -> processIncomingBytes(), reading and
// dispatching whatever's available right there in the callback rather than
// signaling a separate task to do it - see WisBlockLoRaAT.h's
// enableBackgroundRx() doc comment for the one-instance-only and
// can't-coexist-with-your-own-USB-callback caveats this implies.
//
// Thread-safety note: these callbacks run in the TinyUSB device task's
// context (RAK4631) or the ESP32 core's USB/event task context (RAK3312) -
// a real FreeRTOS task, not a hard ISR, so calling into this library (and
// therefore LBM) is *safe to attempt*, but it's a *different* task than
// WisBlockLbmTask's own background event task if
// WisBlockLoRaWAN::enableBackgroundTask() is also active. processIncomingBytes()
// already wraps each dispatched line in lora->lockLbm()/unlockLbm() to
// serialize against that - see wisblock_lbm_task.h for why.
// ---------------------------------------------------------------------------

#include <Arduino.h>
extern SemaphoreHandle_t g_task_sem;
extern volatile uint16_t g_task_event_type;
#ifdef ARDUINO_ARCH_ESP32
static BaseType_t xHigherPriorityTaskWoken = pdFALSE;
#endif
void WisBlockLoRaAT::onBackgroundRxData()
{
	g_task_event_type |= 0b0000000000100000; // #define AT_CMD
	if (g_task_sem != NULL)
	{
#ifdef ESP32ARDUINO_ARCH_ESP32
		xSemaphoreGiveFromISR(g_task_sem, &xHigherPriorityTaskWoken);
#endif
#ifdef NRF52_SERIES
		xSemaphoreGiveFromISR(g_task_sem, pdFALSE);
#endif
	}

	// if (activeInstanceForRx)
	// {
	// 	activeInstanceForRx->processIncomingBytes();
	// }
}

#if defined(ARDUINO_ARCH_NRF52) || defined(NRF52840_XXAA)

#include <Adafruit_TinyUSB.h>

bool WisBlockLoRaAT::enableBackgroundRx()
{
	activeInstanceForRx = this;
	backgroundRxActive = true;
	return true;
}

// TinyUSB weak-symbol hook - fires whenever USB CDC RX data arrives. Only
// one definition of this can exist in the whole linked program; see the
// class doc comment on enableBackgroundRx().
extern "C" void tud_cdc_rx_cb(uint8_t itf)
{
	if (itf != 0)
	{
		return; // this library only drives the primary CDC interface (Serial)
	}
	WisBlockLoRaAT::onBackgroundRxData();
}

#elif defined(ARDUINO_ARCH_ESP32)

#include <Arduino.h>
#include <HWCDC.h> // ARDUINO_HW_CDC_EVENTS / ARDUINO_HW_CDC_RX_EVENT - see the TODO below if your core version differs

// namespace
// {
void usbEventCallback(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
	(void)arg;
	(void)event_data;
	// ARDUINO_HW_CDC_EVENTS/_RX_EVENT matches the ESP32-S3 native USB CDC
	// (HWCDC) event API. TODO: if your esp32-arduino core version exposes
	// this under a different class/event-base name (USBCDC vs HWCDC has
	// varied across core releases), adjust this match accordingly - the
	// rest of this file doesn't need to change.
	if (event_base == ARDUINO_HW_CDC_EVENTS && event_id == ARDUINO_HW_CDC_RX_EVENT)
	{
		WisBlockLoRaAT::onBackgroundRxData();
	}
}
// } // namespace

bool WisBlockLoRaAT::enableBackgroundRx()
{
	// activeInstanceForRx = this;
	// Serial.onEvent(ARDUINO_HW_CDC_EVENTS, usbEventCallback);
	// backgroundRxActive = true;
	return true;
}

#else

bool WisBlockLoRaAT::enableBackgroundRx()
{
	// Not available on RAK11310 (RP2040) - see the class doc comment.
	return false;
}

#endif
