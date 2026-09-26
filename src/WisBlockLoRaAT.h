/**
 * @file WisBlockLoRaAT.h
 * @brief AT command parser/dispatcher, a thin text protocol over
 * WisBlockLoRaWAN's C++ API (see README.md for the full command table).
 *
 * Dispatch is table-driven, the same shape as RUI3's own AT command core
 * (cores/nRF5/component/service/mode/cli/atcmd.c in RAKWireless/RAK-nRF52-RUI):
 * one row per command name, pointing at a handler that gets told whether it
 * was invoked bare ("AT+CMD"), as a query ("AT+CMD=?") or as a set
 * ("AT+CMD=value") rather than the old approach of a long if/else-if chain
 * of strcmp()/startsWith() calls repeated for every command.
 */
#ifndef WISBLOCK_LORA_AT_H
#define WISBLOCK_LORA_AT_H

#include "WisBlockLoRaWAN.h"
#include <Stream.h>
#include <stddef.h> // size_t
#include <stdint.h>

/** Status a custom AT command handler returns - see WisBlockLoRaAT::addCustomATCommand(). */
enum WisBlockAtStatus
{
	WISBLOCK_AT_OK = 0,
	WISBLOCK_AT_ERROR,
	WISBLOCK_AT_PARAM_ERROR,
};

class WisBlockLoRaAT
{
public:
	/** Called for a complete line that doesn't start with "AT" - i.e. not one of this library's own commands. */
	using UnhandledDataCb = void (*)(const char *line);

	/**
	 * Handler for a custom "ATC+<CMD>" AT command, registered with
	 * addCustomATCommand() - modeled on RUI3's PF_handle(port, cmd, param)
	 * signature (see api.system.atMode.add() in RAKSystem.h) but adapted to
	 * this library's plain-C-string argument model instead of RUI3's
	 * colon-split stParam/argv.
	 *
	 * `cmd` is the full uppercased command name as typed, e.g. "ATC+LED"
	 * (this parser uppercases every line before dispatch - see
	 * processLine()'s doc comment) - handed back to the handler the same
	 * way RUI3 does, so one handler function can serve several registered
	 * names and still know which one fired.
	 *
	 * `args` is:
	 *   - nullptr for a bare "ATC+LED" (no '=' anywhere in the line)
	 *   - the literal string "?" for a query, "ATC+LED=?"
	 *   - the text after '=' for a set, "ATC+LED=1:2" - handed to the
	 *     handler whole, unsplit; if your command takes several
	 *     colon/comma-separated fields, parse `args` yourself the same way
	 *     this library's own +JOIN=/+P2P=/+PRECVDC= handlers do.
	 *
	 * Return WISBLOCK_AT_OK to have this library reply "OK"; anything else
	 * gets "AT_ERROR" or "AT_PARAM_ERROR", same as a failed built-in
	 * command.
	 */
	using CustomAtHandler = WisBlockAtStatus (*)(Stream &port, const char *cmd, char *args);

	/** `lora` must already have had begin() called. `port` is the Serial/UART used for AT I/O. */
	void begin(WisBlockLoRaWAN &lora, Stream &port);

	/** Call every loop(); reads available bytes, parses complete lines terminated by \r or \n. */
	void handleSerial();

	/** Parses and executes a single, already-complete command line (no CR/LF). Returns the response text. */
	void processLine(const char *line);

	/**
	 * Registers a callback for lines that aren't one of this library's own
	 * AT commands (don't start with "AT") - lets the application handle
	 * its own custom serial protocol on the same port without it being
	 * swallowed as an AT error. Lines that DO start with "AT" but aren't a
	 * command this library recognizes still get the usual
	 * "ERROR: unknown command" reply, on the assumption a near-miss "AT..."
	 * line was meant for this parser, just malformed/unsupported.
	 */
	void onUnhandledData(UnhandledDataCb cb) { unhandledDataCb = cb; }

	/** Up to this many custom commands can be registered - see addCustomATCommand(). */
	static const uint8_t MAX_CUSTOM_AT_COMMANDS = 16;

	/**
	 * Registers a custom "ATC+<CMD>" AT command - the same idea as RUI3's
	 * api.system.atMode.add() (see RAKSystem.h), letting application code
	 * add its own AT commands to this parser instead of (ab)using
	 * onUnhandledData() to run a whole separate protocol on the same port.
	 *
	 * `cmd` is the command name only - no "AT" or "ATC+" prefix, no
	 * trailing "=" - e.g. "LED" registers "ATC+LED"/"ATC+LED=?"/"ATC+LED=x".
	 * Matched case-insensitively for the same reason every other command
	 * in this parser is: see processLine()'s doc comment on why the whole
	 * line gets uppercased before any comparison happens. `usage` is a
	 * short human-readable description, currently unused by this library
	 * but kept (as RUI3's own table does) for a future AT+CMD=? help
	 * listing and so application code has one obvious place to document
	 * its own commands.
	 *
	 * Returns false if `cmd` or `handler` is null, `cmd` is empty, the
	 * MAX_CUSTOM_AT_COMMANDS table is already full, or `cmd` is already
	 * registered.
	 */
	bool addCustomATCommand(const char *cmd, const char *usage, CustomAtHandler handler);

	/**
	 * Starts background, interrupt/callback-driven AT command processing -
	 * eliminates the need to call handleSerial() from loop() at all, the
	 * same way WisBlockLoRaWAN::enableBackgroundTask() eliminates
	 * handleEvents() polling. `port` (passed to begin()) must be the
	 * physical USB CDC Serial - the underlying OS hooks this wires up
	 * (TinyUSB's tud_cdc_rx_cb on RAK4631, the native USB CDC RX event on
	 * RAK3312) are tied to that specific peripheral, not an arbitrary
	 * Stream.
	 *
	 * IMPORTANT: this can only be enabled for ONE WisBlockLoRaAT instance,
	 * and it installs a weak-symbol/global event hook that cannot coexist
	 * with your own sketch defining tud_cdc_rx_cb()/a USB CDC RX event
	 * handler - pick one or the other.
	 *
	 * Not available on RAK11310 (RP2040) - handleSerial() polling remains
	 * the only option there. Returns false if unsupported on this
	 * platform/build.
	 */
	bool enableBackgroundRx();

	/**
	 * Called by the platform-specific USB CDC RX callback (tud_cdc_rx_cb on
	 * RAK4631, the ARDUINO_HW_CDC_EVENTS handler on RAK3312) - public
	 * because those are free functions outside this class, not because
	 * application code should call this directly.
	 */
	static void onBackgroundRxData();

	// Shared byte-accumulation logic: reads everything currently available
	// from `port` and feeds it into the line buffer, dispatching
	// processLine() on each complete line. Used by both handleSerial()
	// (loop()-polled) and the background RX path (called from the USB CDC
	// RX callback instead).
	void processIncomingBytes();

private:
	// Whether a command was invoked bare ("AT+CMD"), as a query
	// ("AT+CMD=?") or as a set ("AT+CMD=value") - every built-in handler
	// below gets told this instead of re-deriving it from the raw string,
	// the way RUI3's own atcmd.c derives "help"/is_write once in At_Parser()
	// and leaves each individual At_*() handler to just branch on it.
	enum class AtOp : uint8_t
	{
		Run,
		Query,
		Write
	};

	// One row per built-in AT command name (no "AT" prefix, no "=", no
	// "?") mapped to the member function that implements it - the lookup
	// table itself lives in the .cpp file (atCommandTable[]) next to the
	// handler implementations, the same way RUI3's atcmd_info_tbl[] sits in
	// atcmd.c alongside the At_*() functions it points at.
	struct AtCommandEntry
	{
		const char *name;
		void (WisBlockLoRaAT::*handler)(AtOp op, const char *value);
	};
	static const AtCommandEntry atCommandTable[];
	static const size_t atCommandTableSize;

	struct CustomAtCommandEntry
	{
		const char *cmd; // nullptr = unused slot
		const char *usage;
		CustomAtHandler handler;
	};
	CustomAtCommandEntry customCommands[MAX_CUSTOM_AT_COMMANDS] = {};

	WisBlockLoRaWAN *lora = nullptr;
	Stream *port = nullptr;
	char lineBuffer[256];
	uint16_t lineLength = 0;
	UnhandledDataCb unhandledDataCb = nullptr;
	bool backgroundRxActive = false;

	void reply(const char *msg);
	void replyOk();
	void replyError(const char *reason = nullptr);
	void handleStatusQuery();

	// Looks up a "ATC+<CMD>..." line's command name against customCommands[]
	// and, on a match, calls its handler and replies OK/AT_ERROR/
	// AT_PARAM_ERROR accordingly. Returns false (no reply sent) if no
	// custom command matches `cmd`, so the caller can fall back to the
	// usual "unknown command" reply.
	bool dispatchCustomCommand(const char *cmd, AtOp op, char *value);

	static bool parseHex(const char *hex, uint8_t *out, size_t outLen);

	// --- Built-in AT command handlers -------------------------------------
	// One method per command name in atCommandTable[] (a get/set pair - e.g.
	// "AT+NWM=?" and "AT+NWM=1" - is one handler, not two), each replicating
	// exactly what its old if/else-if branch(es) in processLine() used to
	// do. `value` is only meaningful when op == AtOp::Write; null otherwise.
	void atNwm(AtOp op, const char *value);
	void atDevEui(AtOp op, const char *value);
	void atAppEui(AtOp op, const char *value); // also serves the "+JOINEUI" alias row
	void atAppKey(AtOp op, const char *value);
	void atDevAddr(AtOp op, const char *value);
	void atNwkSKey(AtOp op, const char *value);
	void atAppSKey(AtOp op, const char *value);
	void atBand(AtOp op, const char *value);
	void atMask(AtOp op, const char *value);
	void atLbt(AtOp op, const char *value);
	void atLbtRssi(AtOp op, const char *value);
	void atLbtScanTime(AtOp op, const char *value);
	void atPgSlot(AtOp op, const char *value);
	void atBFreq(AtOp op, const char *value);
	void atBTime(AtOp op, const char *value);
	void atDr(AtOp op, const char *value);
	void atClass(AtOp op, const char *value);
	void atNjm(AtOp op, const char *value);
	void atJoin(AtOp op, const char *value);
	void atNjs(AtOp op, const char *value);
	void atAdr(AtOp op, const char *value);
	void atTxp(AtOp op, const char *value);
	void atCfm(AtOp op, const char *value);
	void atFPending(AtOp op, const char *value);
	void atSend(AtOp op, const char *value);
	void atLinkCheck(AtOp op, const char *value);
	void atTimeReq(AtOp op, const char *value);
	void atP2p(AtOp op, const char *value);
	void atCad(AtOp op, const char *value);
	void atRxBoost(AtOp op, const char *value);
	void atPSend(AtOp op, const char *value);
	void atPRecv(AtOp op, const char *value);
	void atPRecvDc(AtOp op, const char *value);
	void atLowPower(AtOp op, const char *value);
	void atSave(AtOp op, const char *value);
	void atRestore(AtOp op, const char *value);
	void atFactory(AtOp op, const char *value);
	void atStatus(AtOp op, const char *value);
	void atHwModel(AtOp op, const char *value);
	void atHwId(AtOp op, const char *value);
	void atSn(AtOp op, const char *value);
	void atVer(AtOp op, const char *value);
	void atAlias(AtOp op, const char *value);
	void atFirmwareVer(AtOp op, const char *value);
	void atAddMulc(AtOp op, const char *value);
	void atRmvMulc(AtOp op, const char *value);
	void atLstMulc(AtOp op, const char *value);
	void atZ(AtOp op, const char *value);
	void atR(AtOp op, const char *value);
	void atBoot(AtOp op, const char *value);

	// The USB CDC RX callbacks are plain C-style hooks (TinyUSB/ESP32 core
	// call them directly, no way to pass a `this` pointer), so a single
	// static instance pointer bridges back to this instance - same pattern
	// as WisBlockLoRaWAN::activeInstanceForTask. Only one WisBlockLoRaAT
	// instance can use background RX mode at a time.
	static WisBlockLoRaAT *activeInstanceForRx;
};

#endif // WISBLOCK_LORA_AT_H
