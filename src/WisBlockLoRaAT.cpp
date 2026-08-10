
#include <Arduino.h>
#include "WisBlockLoRaAT.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

	// Shared by every "=?" getter below that reads back a byte array
	// (DevEUI, JoinEUI, DevAddr) as upper-case hex, matching the format
	// their corresponding setters accept.
	void printHex(Stream *port, const uint8_t *data, size_t len)
	{
		char buf[3];
		for (size_t i = 0; i < len; i++)
		{
			snprintf(buf, sizeof(buf), "%02X", data[i]);
			port->print(buf);
		}
		port->println();
	}
} // namespace

WisBlockLoRaAT *WisBlockLoRaAT::activeInstanceForRx = nullptr;

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
	port->println(msg);
}

void WisBlockLoRaAT::replyOk()
{
	port->println("OK");
}

void WisBlockLoRaAT::replyError(const char *reason)
{
	if (reason)
	{
		port->print("ERROR: ");
		port->println(reason);
	}
	else
	{
		port->println("ERROR");
	}
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
		port->print("JOINSTATE=");
		port->println((int)lora->joinState());
		port->print("RELAY=");
		port->println((int)cfg.lorawan.relayMode);
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
			replyError("expected AT prefix");
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
	line = upperLine;
	const char *cmd = line + 2; // skip "AT"

	if (strcmp(cmd, "") == 0)
	{
		replyOk(); // bare "AT" -> liveness check
		return;
	}

	if (startsWith(cmd, "+MODE=?"))
	{
		reply(lora->getWorkMode() == WISBLOCK_MODE_LORAWAN ? "LORAWAN" : "LORA_P2P");
	}
	else if (startsWith(cmd, "+MODE="))
	{
		int v = atoi(cmd + 6);
		lora->setWorkMode(v == 1 ? WISBLOCK_MODE_LORA_P2P : WISBLOCK_MODE_LORAWAN);
		replyOk();
	}
	else if (strcmp(cmd, "+DEVEUI=?") == 0)
	{
		printHex(port, lora->getConfig().lorawan.otaa.devEui, 8);
	}
	else if (startsWith(cmd, "+DEVEUI="))
	{
		uint8_t eui[8];
		if (!parseHex(cmd + 8, eui, 8))
		{
			replyError("bad hex, expected 16 chars");
			return;
		}
		WisBlockOTAAKeys keys = lora->getConfig().lorawan.otaa;
		memcpy(keys.devEui, eui, 8);
		lora->setOTAAKeys(keys.devEui, keys.joinEui, keys.appKey);
		replyOk();
	}
	else if (strcmp(cmd, "+APPEUI=?") == 0 || strcmp(cmd, "+JOINEUI=?") == 0)
	{
		printHex(port, lora->getConfig().lorawan.otaa.joinEui, 8);
	}
	else if (startsWith(cmd, "+APPEUI=") || startsWith(cmd, "+JOINEUI="))
	{
		const char *hex = strchr(cmd, '=') + 1;
		uint8_t eui[8];
		if (!parseHex(hex, eui, 8))
		{
			replyError("bad hex, expected 16 chars");
			return;
		}
		WisBlockOTAAKeys keys = lora->getConfig().lorawan.otaa;
		memcpy(keys.joinEui, eui, 8);
		lora->setOTAAKeys(keys.devEui, keys.joinEui, keys.appKey);
		replyOk();
	}
	else if (strcmp(cmd, "+APPKEY=?") == 0)
	{
		// SECURITY: deliberately not read back in plaintext, unlike
		// DevEUI/JoinEUI/DevAddr above - the AppKey is secret key
		// material, and an AT interface that echoes it back over serial
		// (often USB, sometimes UART with no physical security at all) is
		// exactly the kind of thing a real product's AT command set
		// normally guards against. Reports only whether one has been set,
		// not the value. If your application genuinely needs plaintext
		// readback (e.g. a provisioning tool that already trusts this
		// serial link), change this block to printHex(port,
		// lora->getConfig().lorawan.otaa.appKey, 16) instead - deliberately
		// not the default.
		const uint8_t *key = lora->getConfig().lorawan.otaa.appKey;
		bool isSet = false;
		for (int i = 0; i < 16 && !isSet; i++)
		{
			isSet = (key[i] != 0);
		}
		reply(isSet ? "SET" : "UNSET");
	}
	else if (startsWith(cmd, "+APPKEY="))
	{
		uint8_t key[16];
		if (!parseHex(cmd + 8, key, 16))
		{
			replyError("bad hex, expected 32 chars");
			return;
		}
		WisBlockOTAAKeys keys = lora->getConfig().lorawan.otaa;
		memcpy(keys.appKey, key, 16);
		lora->setOTAAKeys(keys.devEui, keys.joinEui, keys.appKey);
		replyOk();
	}
	else if (strcmp(cmd, "+DEVADDR=?") == 0)
	{
		uint32_t addr = lora->getConfig().lorawan.abp.devAddr;
		uint8_t bytes[4] = {(uint8_t)(addr >> 24), (uint8_t)(addr >> 16), (uint8_t)(addr >> 8), (uint8_t)addr};
		printHex(port, bytes, 4);
	}
	else if (startsWith(cmd, "+DEVADDR="))
	{
		uint8_t addrBytes[4];
		if (!parseHex(cmd + 9, addrBytes, 4))
		{
			replyError("bad hex, expected 8 chars");
			return;
		}
		uint32_t addr = ((uint32_t)addrBytes[0] << 24) | ((uint32_t)addrBytes[1] << 16) |
						((uint32_t)addrBytes[2] << 8) | addrBytes[3];
		WisBlockABPKeys keys = lora->getConfig().lorawan.abp;
		lora->setABPKeys(addr, keys.nwkSKey, keys.appSKey);
		replyOk();
	}
	else if (strcmp(cmd, "+NWKSKEY=?") == 0)
	{
		// SECURITY: same reasoning as +APPKEY=? above - session key
		// material, masked by default.
		const uint8_t *key = lora->getConfig().lorawan.abp.nwkSKey;
		bool isSet = false;
		for (int i = 0; i < 16 && !isSet; i++)
		{
			isSet = (key[i] != 0);
		}
		reply(isSet ? "SET" : "UNSET");
	}
	else if (startsWith(cmd, "+NWKSKEY="))
	{
		uint8_t key[16];
		if (!parseHex(cmd + 9, key, 16))
		{
			replyError("bad hex, expected 32 chars");
			return;
		}
		WisBlockABPKeys keys = lora->getConfig().lorawan.abp;
		memcpy(keys.nwkSKey, key, 16);
		lora->setABPKeys(keys.devAddr, keys.nwkSKey, keys.appSKey);
		replyOk();
	}
	else if (strcmp(cmd, "+APPSKEY=?") == 0)
	{
		// SECURITY: same reasoning as +APPKEY=? above.
		const uint8_t *key = lora->getConfig().lorawan.abp.appSKey;
		bool isSet = false;
		for (int i = 0; i < 16 && !isSet; i++)
		{
			isSet = (key[i] != 0);
		}
		reply(isSet ? "SET" : "UNSET");
	}
	else if (startsWith(cmd, "+APPSKEY="))
	{
		uint8_t key[16];
		if (!parseHex(cmd + 9, key, 16))
		{
			replyError("bad hex, expected 32 chars");
			return;
		}
		WisBlockABPKeys keys = lora->getConfig().lorawan.abp;
		memcpy(keys.appSKey, key, 16);
		lora->setABPKeys(keys.devAddr, keys.nwkSKey, keys.appSKey);
		replyOk();
	}
	else if (strcmp(cmd, "+REGION=?") == 0)
	{
		port->println((int)lora->getConfig().lorawan.region);
	}
	else if (startsWith(cmd, "+REGION="))
	{
		lora->setRegion((WisBlockRegion)atoi(cmd + 8));
		replyOk();
	}
	else if (strcmp(cmd, "+DR=?") == 0)
	{
		port->println(lora->getConfig().lorawan.dataRate);
	}
	else if (startsWith(cmd, "+DR="))
	{
		lora->setDataRate((uint8_t)atoi(cmd + 4));
		replyOk();
	}
	else if (strcmp(cmd, "+CLASS=?") == 0)
	{
		WisBlockDeviceClass dc = lora->getConfig().lorawan.deviceClass;
		reply(dc == WISBLOCK_CLASS_B ? "B" : dc == WISBLOCK_CLASS_C ? "C" : "A");
	}
	else if (startsWith(cmd, "+CLASS="))
	{
		char c = cmd[7];
		WisBlockDeviceClass dc = (c == 'B' || c == 'b') ? WISBLOCK_CLASS_B : (c == 'C' || c == 'c') ? WISBLOCK_CLASS_C
																									: WISBLOCK_CLASS_A;
		lora->setDeviceClass(dc);
		replyOk();
	}
	else if (strcmp(cmd, "+JOINMODE=?") == 0)
	{
		port->println(lora->getConfig().lorawan.joinMode == WISBLOCK_JOIN_ABP ? 1 : 0);
	}
	else if (startsWith(cmd, "+JOINMODE="))
	{
		lora->setJoinMode(atoi(cmd + 10) == 1 ? WISBLOCK_JOIN_ABP : WISBLOCK_JOIN_OTAA);
		replyOk();
	}
	else if (strcmp(cmd, "+JOIN") == 0)
	{
		lora->join();
		replyOk();
	}
	else if (strcmp(cmd, "+JOIN=?") == 0)
	{
		port->println((int)lora->joinState());
	}
	else if (strcmp(cmd, "+ADR=?") == 0)
	{
		port->println(lora->getConfig().lorawan.adrEnabled ? "1" : "0");
	}
	else if (startsWith(cmd, "+ADR="))
	{
		lora->setADR(atoi(cmd + 5) != 0);
		replyOk();
	}
	else if (strcmp(cmd, "+TXP=?") == 0)
	{
		port->println(lora->getConfig().lorawan.txPower);
	}
	else if (startsWith(cmd, "+TXP="))
	{
		lora->setTxPower((uint8_t)atoi(cmd + 5));
		replyOk();
	}

	else if (strcmp(cmd, "+RELAY=?") == 0)
	{
		port->println((int)lora->getConfig().lorawan.relayMode);
	}
	else if (startsWith(cmd, "+RELAY="))
	{
		int v = atoi(cmd + 7);
		lora->setRelayMode(v == 1 ? WISBLOCK_RELAY_ED : v == 2 ? WISBLOCK_RELAY_SERVING
															   : WISBLOCK_RELAY_OFF);
		replyOk();
	}
	else if (strcmp(cmd, "+RELAYED=?") == 0)
	{
		// Same field order as the setter above: <activationMode>:<smartLevel>:<backoff>:<missedWorAckToNoSync>:<secondChEnable>:<secondChFreqHz>:<secondChAckFreqHz>:<secondChDr>
		const WisBlockRelayEDConfig &cfg = lora->getConfig().lorawan.relayEDConfig;
		port->print(cfg.activationMode);
		port->print(":");
		port->print(cfg.smartLevel);
		port->print(":");
		port->print(cfg.backoff);
		port->print(":");
		port->print(cfg.missedWorAckToNoSync);
		port->print(":");
		port->print(cfg.secondChannelEnable ? 1 : 0);
		port->print(":");
		port->print(cfg.secondChannelFreqHz);
		port->print(":");
		port->print(cfg.secondChannelAckFreqHz);
		port->print(":");
		port->println(cfg.secondChannelDr);
	}
	else if (startsWith(cmd, "+RELAYED="))
	{
		// AT+RELAYED=<activationMode>:<smartLevel>:<backoff>:<missedWorAckToNoSync>:<secondChEnable>:<secondChFreqHz>:<secondChAckFreqHz>:<secondChDr>
		char buf[96];
		strncpy(buf, cmd + 9, sizeof(buf) - 1);
		buf[sizeof(buf) - 1] = '\0';

		WisBlockRelayEDConfig cfg;
		char *tok = strtok(buf, ":");
		if (tok)
			cfg.activationMode = (uint8_t)atoi(tok);
		tok = strtok(nullptr, ":");
		if (tok)
			cfg.smartLevel = (uint8_t)atoi(tok);
		tok = strtok(nullptr, ":");
		if (tok)
			cfg.backoff = (uint8_t)atoi(tok);
		tok = strtok(nullptr, ":");
		if (tok)
			cfg.missedWorAckToNoSync = (uint8_t)atoi(tok);
		tok = strtok(nullptr, ":");
		if (tok)
			cfg.secondChannelEnable = atoi(tok) != 0;
		tok = strtok(nullptr, ":");
		if (tok)
			cfg.secondChannelFreqHz = strtoul(tok, nullptr, 10);
		tok = strtok(nullptr, ":");
		if (tok)
			cfg.secondChannelAckFreqHz = strtoul(tok, nullptr, 10);
		tok = strtok(nullptr, ":");
		if (tok)
			cfg.secondChannelDr = (uint8_t)atoi(tok);

		lora->configureRelayED(cfg);
		replyOk();
	}
	else if (strcmp(cmd, "+RELAYSRV=?") == 0)
	{
		// Same field order as the setter above: <cadPeriod>:<freqHz>:<ackFreqHz>:<dr>:<errorPpm>:<cadToRxSymb>
		const WisBlockRelayServingConfig &cfg = lora->getConfig().lorawan.relayServingConfig;
		port->print(cfg.cadPeriod);
		port->print(":");
		port->print(cfg.channelFreqHz);
		port->print(":");
		port->print(cfg.channelAckFreqHz);
		port->print(":");
		port->print(cfg.channelDr);
		port->print(":");
		port->print(cfg.errorPpm);
		port->print(":");
		port->println(cfg.cadToRxSymb);
	}
	else if (startsWith(cmd, "+RELAYSRV="))
	{
		// AT+RELAYSRV=<cadPeriod>:<freqHz>:<ackFreqHz>:<dr>:<errorPpm>:<cadToRxSymb>
		char buf[96];
		strncpy(buf, cmd + 10, sizeof(buf) - 1);
		buf[sizeof(buf) - 1] = '\0';

		WisBlockRelayServingConfig cfg;
		char *tok = strtok(buf, ":");
		if (tok)
			cfg.cadPeriod = (uint8_t)atoi(tok);
		tok = strtok(nullptr, ":");
		if (tok)
			cfg.channelFreqHz = strtoul(tok, nullptr, 10);
		tok = strtok(nullptr, ":");
		if (tok)
			cfg.channelAckFreqHz = strtoul(tok, nullptr, 10);
		tok = strtok(nullptr, ":");
		if (tok)
			cfg.channelDr = (uint8_t)atoi(tok);
		tok = strtok(nullptr, ":");
		if (tok)
			cfg.errorPpm = (uint8_t)atoi(tok);
		tok = strtok(nullptr, ":");
		if (tok)
			cfg.cadToRxSymb = (uint8_t)atoi(tok);

		lora->configureRelayServing(cfg);
		replyOk();
	}
	else if (strcmp(cmd, "+RELAYDEV=?") == 0)
	{
		// Deliberately not implemented, unlike every other getter in this
		// file: the registered trusted-device list isn't kept anywhere
		// readable on this side (see WisBlockRelayTrustedDevice's own doc
		// comment in WisBlockLoRaWANTypes.h - "Not persisted... unlike the
		// two configs above") - each AT+RELAYDEV= call pushes straight into
		// LBM with no local copy retained to read back, and the entries
		// contain a root session key (rootWorSKey) that shouldn't be echoed
		// in plaintext regardless (see the +APPKEY=? SECURITY note above).
		replyError("not supported - trusted device list has no local readable copy");
	}
	else if (startsWith(cmd, "+RELAYDEV="))
	{
		// AT+RELAYDEV=<idx>:<devAddrHex8>:<rootWorSKeyHex32>:<unlimitedFwd>:<bucketFactor>:<reloadRate>
		// Registers a trusted end-device with the serving relay - required
		// before it will forward anything for that device (see
		// LoRaWANRelay.h). No effect unless AT+RELAY=2 (serving) is active.
		const char *args = cmd + 10;
		char idxStr[8] = {0}, addrStr[16] = {0}, keyStr[40] = {0}, unlimStr[8] = {0}, bucketStr[8] = {0}, reloadStr[8] = {0};
		if (sscanf(args, "%7[^:]:%15[^:]:%39[^:]:%7[^:]:%7[^:]:%7[^:]", idxStr, addrStr, keyStr, unlimStr, bucketStr, reloadStr) != 6)
		{
			replyError("expected <idx>:<devaddr8hex>:<key32hex>:<unlimited0/1>:<bucket>:<reload>");
			return;
		}

		WisBlockRelayTrustedDevice device;
		device.index = (uint8_t)atoi(idxStr);
		uint8_t addrBytes[4];
		if (!parseHex(addrStr, addrBytes, 4))
		{
			replyError("bad devaddr hex, expected 8 chars");
			return;
		}
		device.devAddr = ((uint32_t)addrBytes[0] << 24) | ((uint32_t)addrBytes[1] << 16) |
						 ((uint32_t)addrBytes[2] << 8) | addrBytes[3];
		if (!parseHex(keyStr, device.rootWorSKey, 16))
		{
			replyError("bad key hex, expected 32 chars");
			return;
		}
		device.unlimitedForward = atoi(unlimStr) != 0;
		device.bucketFactor = (uint8_t)atoi(bucketStr);
		device.reloadRate = (uint8_t)atoi(reloadStr);

		lora->addRelayTrustedDevice(device) ? replyOk() : replyError("failed (is AT+RELAY=2 active? was LBM built with ADD_RELAY_RX?)");
	}
	else if (startsWith(cmd, "+RELAYDEVDEL="))
	{
		uint8_t idx = (uint8_t)atoi(cmd + 13);
		lora->removeRelayTrustedDevice(idx) ? replyOk() : replyError("failed");
	}
	else if (strcmp(cmd, "+CFM=?") == 0)
	{
		port->println(lora->getConfig().lorawan.confirmedUplinks ? "1" : "0");
	}
	else if (startsWith(cmd, "+CFM="))
	{
		lora->setConfirmedUplinks(atoi(cmd + 5) != 0);
		replyOk();
	}
	else if (startsWith(cmd, "+SEND="))
	{
		const char *args = cmd + 6;
		const char *colon = strchr(args, ':');
		if (!colon)
		{
			replyError("expected <port>:<hexpayload>");
			return;
		}
		char portStr[8] = {0};
		size_t portLen = colon - args;
		if (portLen >= sizeof(portStr))
		{
			replyError("port too long");
			return;
		}
		memcpy(portStr, args, portLen);
		uint8_t port_ = (uint8_t)atoi(portStr);

		const char *hex = colon + 1;
		size_t hexLen = strlen(hex);
		if (hexLen % 2 != 0 || hexLen / 2 > 242)
		{
			replyError("bad payload hex");
			return;
		}
		uint8_t payload[242];
		if (!parseHex(hex, payload, hexLen / 2))
		{
			replyError("bad payload hex");
			return;
		}
		bool ok = lora->sendLoRaWAN(port_, payload, (uint8_t)(hexLen / 2));
		ok ? replyOk() : replyError("send failed (not joined?)");
	}
	else if (strcmp(cmd, "+LINKCHECK") == 0)
	{
		lora->requestLinkCheck();
		replyOk();
	}
	else if (strcmp(cmd, "+LINKCHECK=?") == 0)
	{
		WisBlockLinkCheckResult r;
		if (lora->getLinkCheckResult(r))
		{
			port->print("MARGIN=");
			port->println(r.demodMargin);
			port->print("GWCNT=");
			port->println(r.gatewayCount);
			replyOk();
		}
		else
		{
			replyError("no link check answered yet - try AT+LINKCHECK first");
		}
	}
	else if (strcmp(cmd, "+TIMEREQ") == 0)
	{
		lora->requestDeviceTime();
		replyOk();
	}
	else if (strcmp(cmd, "+P2P=?") == 0)
	{
		// Same field order as the setter above: <freqHz>:<sf>:<bw>:<cr>:<preamble>:<txpower>
		const WisBlockP2PSettings &s = lora->getP2PSettings();
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
	}
	else if (startsWith(cmd, "+P2P="))
	{
		// AT+P2P=<freqHz>:<sf>:<bw>:<cr>:<preamble>:<txpower>
		char buf[96];
		strncpy(buf, cmd + 5, sizeof(buf) - 1);
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
			replyError("bad frequency");
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
	else if (strcmp(cmd, "+CAD=?") == 0)
	{
		port->println(lora->getP2PSettings().cadEnabled ? "1" : "0");
	}
	else if (startsWith(cmd, "+CAD="))
	{
		lora->setP2PCad(atoi(cmd + 5) != 0);
		replyOk();
	}
	else if (strcmp(cmd, "+RXBOOST=?") == 0)
	{
		port->println(lora->getP2PSettings().rxBoostedGainEnabled ? "1" : "0");
	}
	else if (startsWith(cmd, "+RXBOOST="))
	{
		// AT+RXBOOST=<0/1> - see WisBlockP2PSettings::rxBoostedGainEnabled's
		// doc comment for the RX-current-vs-sensitivity tradeoff this
		// controls.
		lora->setP2PRxBoostedGain(atoi(cmd + 9) != 0);
		replyOk();
	}
	else if (startsWith(cmd, "+PSEND="))
	{
		const char *hex = cmd + 7;
		size_t hexLen = strlen(hex);
		if (hexLen % 2 != 0 || hexLen / 2 > 255)
		{
			replyError("bad payload hex");
			return;
		}
		uint8_t payload[255];
		if (!parseHex(hex, payload, hexLen / 2))
		{
			replyError("bad payload hex");
			return;
		}
		bool ok = lora->sendP2P(payload, (uint8_t)(hexLen / 2));
		ok ? replyOk() : replyError("send failed");
	}
	else if (startsWith(cmd, "+PRECV="))
	{
		uint32_t timeout = strtoul(cmd + 7, nullptr, 10);
		lora->startP2PReceive(timeout);
		replyOk();
	}
	else if (startsWith(cmd, "+PRECVDC=AUTO"))
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
		const char *afterAuto = cmd + 13; // strlen("+PRECVDC=AUTO")
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
			replyError("preamble too short for any usable duty-cycle window");
			return;
		}
		lora->startP2PReceiveDutyCycle(rxTimeMs, sleepTimeMs);
		replyOk();
	}
	else if (startsWith(cmd, "+PRECVDC="))
	{
		// AT+PRECVDC=<rxTimeMs>:<sleepTimeMs> - see
		// LoRaP2PEngine::startReceiveDutyCycle()'s doc comment for the full
		// picture of what this does differently from AT+PRECV.
		char buf[32];
		strncpy(buf, cmd + 9, sizeof(buf) - 1);
		buf[sizeof(buf) - 1] = '\0';

		char *tok = strtok(buf, ":");
		uint32_t rxTimeMs = tok ? strtoul(tok, nullptr, 10) : 0;
		tok = strtok(nullptr, ":");
		uint32_t sleepTimeMs = tok ? strtoul(tok, nullptr, 10) : 0;

		if (rxTimeMs == 0 || sleepTimeMs == 0)
		{
			replyError("bad rxTimeMs/sleepTimeMs");
			return;
		}
		lora->startP2PReceiveDutyCycle(rxTimeMs, sleepTimeMs);
		replyOk();
	}
	else if (strcmp(cmd, "+LOWPOWER=?") == 0)
	{
		port->println(lora->isLowPowerEnabled() ? "1" : "0");
	}
	else if (startsWith(cmd, "+LOWPOWER="))
	{
		lora->setLowPowerEnabled(atoi(cmd + 10) != 0);
		replyOk();
	}
	else if (strcmp(cmd, "+SAVE") == 0)
	{
		lora->saveConfig() ? replyOk() : replyError("flash write failed");
	}
	else if (strcmp(cmd, "+RESTORE") == 0)
	{
		lora->restoreConfig() ? replyOk() : replyError("no saved config found, defaults loaded");
	}
	else if (strcmp(cmd, "+FACTORY") == 0)
	{
		lora->factoryReset() ? replyOk() : replyError("flash erase failed");
	}
	else if (strcmp(cmd, "+STATUS") == 0)
	{
		handleStatusQuery();
		replyOk();
	}
	else
	{
		replyError("unknown command");
	}
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
