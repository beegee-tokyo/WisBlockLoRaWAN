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

/**
 * Number of lines that WisBlockLoRaAT::onUnhandledDataInLoop() can hold until loop() fetches them.
 * Each line needs 256 bytes of RAM. Define a different value before including the library.
 */
#ifndef WB_AT_UNHANDLED_QUEUE_LINES
#define WB_AT_UNHANDLED_QUEUE_LINES 4
#endif

/** Status a custom AT command handler returns - see WisBlockLoRaAT::addCustomATCommand(). */
enum WisBlockAtStatus
{
	WISBLOCK_AT_OK = 0,
	WISBLOCK_AT_ERROR,
	WISBLOCK_AT_PARAM_ERROR,
};

/**
 * @brief AT command interface of the library
 *
 * Reads AT commands from a serial port and runs them on the WisBlockLoRaWAN object. Commands follow the RUI3 AT command set, see WisBlockLoRaWAN-AT-Commands.md. Applications can add their own ATC+ commands.
 */
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

	/**
	 * @brief Connect the AT parser to the library and a serial port
	 *
	 * `lora` must already have had begin() called. `port` is the Serial/UART used for AT I/O.
	 *
	 * @param lora Library object, begin() must have been called
	 * @param port Serial port used for the AT commands
	 */
	void begin(WisBlockLoRaWAN &lora, Stream &port);

	/**
	 * @brief Read the serial port and execute the received AT commands
	 *
	 * Call it in every loop(), or after the wake callback of setRxWakeCallback() was called.
	 * It reads the available bytes, parses the complete lines (terminated by CR or LF) and runs
	 * the AT commands. It does nothing while enableBackgroundRx() runs the commands in its own
	 * task, except delivering the lines for onUnhandledDataInLoop().
	 */
	void handleSerial();

	/**
	 * @brief Parse and execute one complete command line
	 *
	 * Parses and executes a single, already-complete command line (no CR/LF). Returns the response text.
	 *
	 * @param line Command line without CR/LF
	 */
	void processLine(const char *line);

	/**
	 * @brief Register a handler for lines that are not AT commands
	 *
	 * Registers a callback for lines that aren't one of this library's own
	 * AT commands (don't start with "AT") - lets the application handle
	 * its own custom serial protocol on the same port without it being
	 * swallowed as an AT error. Lines that DO start with "AT" but aren't a
	 * command this library recognizes still get the usual
	 * "ERROR: unknown command" reply, on the assumption a near-miss "AT..."
	 * line was meant for this parser, just malformed/unsupported.
	 *
	 * @param cb Function called with every line that does not start with AT
	 */
	void onUnhandledData(UnhandledDataCb cb) { unhandledDataCb = cb; }

	/**
	 * @brief Register a handler for lines that are not AT commands, called from loop()
	 *
	 * Same lines as onUnhandledData(), but they are not handled where they are read. With
	 * enableBackgroundRx() that is the task of the library, in a sketch with
	 * setRxWakeCallback() it is whatever context the wake callback runs in. This handler is
	 * called from handleSerial(), that means from loop(), so it can do what loop() can do. The
	 * lines wait in a queue (WB_AT_UNHANDLED_QUEUE_LINES lines of up to 255 characters). If the
	 * queue is full, new lines are dropped.
	 *
	 * Both handlers can be registered at the same time. If loop() sleeps, use the handler of
	 * onUnhandledData() to wake it up (set a flag or give a semaphore) and let loop() call
	 * handleSerial().
	 *
	 * @param cb Function called from handleSerial() for every line that does not start with AT, the text is only valid during the call
	 */
	void onUnhandledDataInLoop(UnhandledDataCb cb) { unhandledLoopCb = cb; }

	/** Up to this many custom commands can be registered - see addCustomATCommand(). */
	static const uint8_t MAX_CUSTOM_AT_COMMANDS = 16;

	/**
	 * @brief Register an application specific AT command (ATC+NAME)
	 *
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
	 *
	 * @param cmd Command name without the ATC+ prefix
	 * @param usage Help text for the command
	 * @param handler Function that executes the command
	 * @return true if the command was registered
	 */
	bool addCustomATCommand(const char *cmd, const char *usage, CustomAtHandler handler);

	/**
	 * Callback type for setRxWakeCallback().
	 */
	typedef void (*RxWakeCallback)(void);

	/**
	 * @brief Register the function that is called when USB data arrives
	 *
	 * Registers a function that is called whenever USB CDC RX data arrives.
	 * Call it BEFORE enableBackgroundRx(). The callback runs in the USB
	 * driver's task (or TinyUSB task) context, so it must be short and
	 * must not block or print: typically it sets an event flag and gives
	 * a semaphore / notifies a task so the application's own task wakes
	 * up and calls handleSerial(). The AT commands themselves then run on
	 * the application's task, not inside the USB driver.
	 *
	 * Optional. Without a wake callback enableBackgroundRx() starts a task of the library that
	 * runs the commands, then there is nothing to do for the sketch. Use the wake callback if
	 * the commands have to run in loop(), for example because loop() sleeps on a semaphore and
	 * handles all events in one place.
	 *
	 * @param cb Function called in the USB driver context, it must only set a flag or wake up a task
	 */
	void setRxWakeCallback(RxWakeCallback cb);

	/**
	 * @brief Run the AT commands in the background, without handleSerial() in loop()
	 *
	 * Hooks the receive notification of the USB serial port (`Serial`): tud_cdc_rx_cb() of
	 * TinyUSB on RAK4631 (nRF52840), on RAK3312 / RAK3112 (ESP32-S3) the receive handler of
	 * Serial (Serial.onReceive() for a UART, the USB CDC RX event for the native USB). Both
	 * platforms work the same way, in one of two modes:
	 *
	 *  - Task mode (no wake callback registered): the library starts a task of its own, called
	 *    "WB_AT". The receive callback only gives a semaphore, the task wakes up and calls
	 *    processIncomingBytes(). The commands run in that task, with its own stack (8 KB, change it
	 *    with `-DWB_AT_TASK_STACK_BYTES=...`), not in the USB driver, not in loop(). The task
	 *    priority is 1 (`WB_AT_TASK_PRIORITY`), on ESP32 it runs on the core of loop()
	 *    (`WB_AT_TASK_CORE`). Needs lora.enableBackgroundTask() to be running, because the commands
	 *    then call into the LoRa Basics Modem from a second task, and lockLbm() / unlockLbm() only
	 *    protect it if the background task is running. handleSerial() does nothing in this mode.
	 *  - Loop mode (wake callback registered with setRxWakeCallback()): the receive callback only
	 *    calls your wake callback. You call handleSerial() from loop(), the commands run there.
	 *
	 * Lines that are not AT commands go to onUnhandledData() (in the task or in the wake context)
	 * and to onUnhandledDataInLoop() (in loop()).
	 *
	 * IMPORTANT: only one WisBlockLoRaAT object can use it, `port` of begin() must be `Serial`,
	 * and the sketch must not define tud_cdc_rx_cb() or hook the receive event of Serial itself.
	 * Not available on RAK11310 (RP2040): there handleSerial() in loop() is the only way.
	 *
	 * @return true if the background processing is active, false if the platform has no receive
	 * hook, `port` is not `Serial`, another object already uses it, in task mode the background
	 * task of the library is not running or the task could not be created
	 */
	bool enableBackgroundRx();

	/**
	 * @brief Called from the USB receive callback when data arrives
	 *
	 * Called by the platform-specific receive callback (tud_cdc_rx_cb on RAK4631, the receive
	 * handler of Serial on RAK3312). In task mode it wakes up the task of the library, in loop
	 * mode it calls the wake callback, before enableBackgroundRx() it does nothing. Public because
	 * those callbacks are free functions outside this class, not because application code should
	 * call this directly.
	 */
	static void onBackgroundRxData();

	/**
	 * @brief Read all available bytes and execute every complete line
	 *
	 * Shared byte-accumulation logic: reads everything currently available
	 * from `port` and feeds it into the line buffer, dispatching
	 * processLine() on each complete line. Used by both handleSerial()
	 * (loop()-polled) and the background RX path (called from the USB CDC
	 * RX callback instead).
	 */
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
	/** Longest line (with the terminating zero) that is collected before it is run */
	static const size_t LINE_BUFFER_SIZE = 256;
	char lineBuffer[LINE_BUFFER_SIZE];
	uint16_t lineLength = 0;
	UnhandledDataCb unhandledDataCb = nullptr;
	UnhandledDataCb unhandledLoopCb = nullptr;
	bool backgroundRxActive = false; // task mode is running, handleSerial() must not read the port
	void *rxSemaphore = nullptr;     // task mode: given by the receive callback (SemaphoreHandle_t)
	void *rxTask = nullptr;          // task mode: the task of the library (TaskHandle_t)

	// Queue for onUnhandledDataInLoop(): one writer (the context that reads the port) and one
	// reader (handleSerial()), so the two indexes are enough, no lock is needed.
	char unhandledQueue[WB_AT_UNHANDLED_QUEUE_LINES][LINE_BUFFER_SIZE];
	uint8_t unhandledHead = 0; // next slot to write, only changed by the writer
	uint8_t unhandledTail = 0; // next slot to read, only changed by the reader

	/**
	 * @brief Put a line that is not an AT command into the queue for onUnhandledDataInLoop()
	 *
	 * Called in the context that reads the port. A full queue drops the line.
	 *
	 * @param line Zero terminated text, longer text is cut
	 */
	void queueUnhandledLine(const char *line);

	/**
	 * @brief Call the handler of onUnhandledDataInLoop() for every queued line
	 *
	 * Called by handleSerial(), so in the context of loop().
	 */
	void dispatchUnhandledLines();

	/**
	 * @brief Body of the task of the library in task mode (see enableBackgroundRx())
	 *
	 * Waits for the semaphore that the receive callback gives, then runs processIncomingBytes().
	 *
	 * @param param The WisBlockLoRaAT object
	 */
	static void rxTaskEntry(void *param);

	/**
	 * @brief Create the semaphore and the task for task mode
	 *
	 * @return true if the task is running
	 */
	bool startRxTask();

	/**
	 * @brief Send a text line followed by OK
	 *
	 * @param msg Text to send
	 */
	void reply(const char *msg);
	/**
	 * @brief Send the OK reply
	 */
	void replyOk();
	/**
	 * @brief Send an error reply
	 *
	 * @param reason Error token (AT_ERROR, AT_PARAM_ERROR) or an explanation that is followed by AT_ERROR
	 */
	void replyError(const char *reason = nullptr);
	/**
	 * @brief Print the mode and the settings of the active mode
	 */
	void handleStatusQuery();

	/**
	 * @brief Run a registered ATC+ command
	 *
	 * Looks up a "ATC+<CMD>..." line's command name against customCommands[]
	 * and, on a match, calls its handler and replies OK/AT_ERROR/
	 * AT_PARAM_ERROR accordingly. Returns false (no reply sent) if no
	 * custom command matches `cmd`, so the caller can fall back to the
	 * usual "unknown command" reply.
	 *
	 * @param cmd Complete upper case command, e.g. ATC+LED
	 * @param op Operation (query, write or run)
	 * @param value Text after the '='
	 * @return true if a registered command handled the line
	 */
	bool dispatchCustomCommand(const char *cmd, AtOp op, char *value);

	/**
	 * @brief Convert a hex string to bytes
	 *
	 * @param hex Hex string, exactly 2 characters per byte
	 * @param out Receives the bytes
	 * @param outLen Number of bytes expected
	 * @return true if the string was valid
	 */
	static bool parseHex(const char *hex, uint8_t *out, size_t outLen);

	// --- Built-in AT command handlers -------------------------------------
	/**
	 * @brief Handler for AT+NWM (LoRa network work mode)
	 *
	 * One method per command name in atCommandTable[] (a get/set pair - e.g.
	 * "AT+NWM=?" and "AT+NWM=1" - is one handler, not two), each replicating
	 * exactly what its old if/else-if branch(es) in processLine() used to
	 * do. `value` is only meaningful when op == AtOp::Write; null otherwise.
	 *
	 * @param op Operation: query (AT+NWM=?), write (AT+NWM=value) or run (AT+NWM)
	 * @param value Text after the '=' (null for query and run)
	 */
	void atNwm(AtOp op, const char *value);
	/**
	 * @brief Handler for AT+DEVEUI (Device EUI)
	 *
	 * @param op Operation: query (AT+DEVEUI=?), write (AT+DEVEUI=value) or run (AT+DEVEUI)
	 * @param value Text after the '=' (null for query and run)
	 */
	void atDevEui(AtOp op, const char *value);
	/**
	 * @brief Handler for AT+APPEUI (Application identifier (JoinEUI))
	 *
	 * @param op Operation: query (AT+APPEUI=?), write (AT+APPEUI=value) or run (AT+APPEUI)
	 * @param value Text after the '=' (null for query and run)
	 */
	void atAppEui(AtOp op, const char *value); // also serves the "+JOINEUI" alias row
	/**
	 * @brief Handler for AT+APPKEY (Application key)
	 *
	 * @param op Operation: query (AT+APPKEY=?), write (AT+APPKEY=value) or run (AT+APPKEY)
	 * @param value Text after the '=' (null for query and run)
	 */
	void atAppKey(AtOp op, const char *value);
	/**
	 * @brief Handler for AT+DEVADDR (Device address)
	 *
	 * @param op Operation: query (AT+DEVADDR=?), write (AT+DEVADDR=value) or run (AT+DEVADDR)
	 * @param value Text after the '=' (null for query and run)
	 */
	void atDevAddr(AtOp op, const char *value);
	/**
	 * @brief Handler for AT+NWKSKEY (Network session key)
	 *
	 * @param op Operation: query (AT+NWKSKEY=?), write (AT+NWKSKEY=value) or run (AT+NWKSKEY)
	 * @param value Text after the '=' (null for query and run)
	 */
	void atNwkSKey(AtOp op, const char *value);
	/**
	 * @brief Handler for AT+APPSKEY (Application session key)
	 *
	 * @param op Operation: query (AT+APPSKEY=?), write (AT+APPSKEY=value) or run (AT+APPSKEY)
	 * @param value Text after the '=' (null for query and run)
	 */
	void atAppSKey(AtOp op, const char *value);
	/**
	 * @brief Handler for AT+BAND (Active region)
	 *
	 * @param op Operation: query (AT+BAND=?), write (AT+BAND=value) or run (AT+BAND)
	 * @param value Text after the '=' (null for query and run)
	 */
	void atBand(AtOp op, const char *value);
	/**
	 * @brief Handler for AT+MASK (Set the channel mask, close or open the channel)
	 *
	 * @param op Operation: query (AT+MASK=?), write (AT+MASK=value) or run (AT+MASK)
	 * @param value Text after the '=' (null for query and run)
	 */
	void atMask(AtOp op, const char *value);
	/**
	 * @brief Handler for AT+LBT (LoRaWAN "Listen Before Talk" (LBT))
	 *
	 * @param op Operation: query (AT+LBT=?), write (AT+LBT=value) or run (AT+LBT)
	 * @param value Text after the '=' (null for query and run)
	 */
	void atLbt(AtOp op, const char *value);
	/**
	 * @brief Handler for AT+LBTRSSI (LoRaWAN "Listen Before Talk" RSSI (LBTRSSI))
	 *
	 * @param op Operation: query (AT+LBTRSSI=?), write (AT+LBTRSSI=value) or run (AT+LBTRSSI)
	 * @param value Text after the '=' (null for query and run)
	 */
	void atLbtRssi(AtOp op, const char *value);
	/**
	 * @brief Handler for AT+LBTSCANTIME (LoRaWAN "Listen Before Talk" Scantime (LBTSCANTIME))
	 *
	 * @param op Operation: query (AT+LBTSCANTIME=?), write (AT+LBTSCANTIME=value) or run (AT+LBTSCANTIME)
	 * @param value Text after the '=' (null for query and run)
	 */
	void atLbtScanTime(AtOp op, const char *value);
	/**
	 * @brief Handler for AT+PGSLOT (Periodicity)
	 *
	 * @param op Operation: query (AT+PGSLOT=?), write (AT+PGSLOT=value) or run (AT+PGSLOT)
	 * @param value Text after the '=' (null for query and run)
	 */
	void atPgSlot(AtOp op, const char *value);
	/**
	 * @brief Handler for AT+BFREQ (Beacon frequency)
	 *
	 * @param op Operation: query (AT+BFREQ=?), write (AT+BFREQ=value) or run (AT+BFREQ)
	 * @param value Text after the '=' (null for query and run)
	 */
	void atBFreq(AtOp op, const char *value);
	/**
	 * @brief Handler for AT+BTIME (Beacon time)
	 *
	 * @param op Operation: query (AT+BTIME=?), write (AT+BTIME=value) or run (AT+BTIME)
	 * @param value Text after the '=' (null for query and run)
	 */
	void atBTime(AtOp op, const char *value);
	/**
	 * @brief Handler for AT+DR (Data rate)
	 *
	 * @param op Operation: query (AT+DR=?), write (AT+DR=value) or run (AT+DR)
	 * @param value Text after the '=' (null for query and run)
	 */
	void atDr(AtOp op, const char *value);
	/**
	 * @brief Handler for AT+CLASS (LoRa Class)
	 *
	 * @param op Operation: query (AT+CLASS=?), write (AT+CLASS=value) or run (AT+CLASS)
	 * @param value Text after the '=' (null for query and run)
	 */
	void atClass(AtOp op, const char *value);
	/**
	 * @brief Handler for AT+NJM (Network join mode)
	 *
	 * @param op Operation: query (AT+NJM=?), write (AT+NJM=value) or run (AT+NJM)
	 * @param value Text after the '=' (null for query and run)
	 */
	void atNjm(AtOp op, const char *value);
	/**
	 * @brief Handler for AT+JOIN (Join LoRaWAN Network)
	 *
	 * @param op Operation: query (AT+JOIN=?), write (AT+JOIN=value) or run (AT+JOIN)
	 * @param value Text after the '=' (null for query and run)
	 */
	void atJoin(AtOp op, const char *value);
	/**
	 * @brief Handler for AT+NJS (Network join status)
	 *
	 * @param op Operation: query (AT+NJS=?), write (AT+NJS=value) or run (AT+NJS)
	 * @param value Text after the '=' (null for query and run)
	 */
	void atNjs(AtOp op, const char *value);
	/**
	 * @brief Handler for AT+ADR (Adaptive Rate)
	 *
	 * @param op Operation: query (AT+ADR=?), write (AT+ADR=value) or run (AT+ADR)
	 * @param value Text after the '=' (null for query and run)
	 */
	void atAdr(AtOp op, const char *value);
	/**
	 * @brief Handler for AT+TXP (Transmit power)
	 *
	 * @param op Operation: query (AT+TXP=?), write (AT+TXP=value) or run (AT+TXP)
	 * @param value Text after the '=' (null for query and run)
	 */
	void atTxp(AtOp op, const char *value);
	/**
	 * @brief Handler for AT+CFM (Confirm mode)
	 *
	 * @param op Operation: query (AT+CFM=?), write (AT+CFM=value) or run (AT+CFM)
	 * @param value Text after the '=' (null for query and run)
	 */
	void atCfm(AtOp op, const char *value);
	/**
	 * @brief Handler for AT+FPENDING (Fetch pending downlinks)
	 *
	 * @param op Operation: query (AT+FPENDING=?), write (AT+FPENDING=value) or run (AT+FPENDING)
	 * @param value Text after the '=' (null for query and run)
	 */
	void atFPending(AtOp op, const char *value);
	/**
	 * @brief Handler for AT+SEND (Send data)
	 *
	 * @param op Operation: query (AT+SEND=?), write (AT+SEND=value) or run (AT+SEND)
	 * @param value Text after the '=' (null for query and run)
	 */
	void atSend(AtOp op, const char *value);
	/**
	 * @brief Handler for AT+LINKCHECK (Verify network link status)
	 *
	 * @param op Operation: query (AT+LINKCHECK=?), write (AT+LINKCHECK=value) or run (AT+LINKCHECK)
	 * @param value Text after the '=' (null for query and run)
	 */
	void atLinkCheck(AtOp op, const char *value);
	/**
	 * @brief Handler for AT+TIMEREQ (Time request)
	 *
	 * @param op Operation: query (AT+TIMEREQ=?), write (AT+TIMEREQ=value) or run (AT+TIMEREQ)
	 * @param value Text after the '=' (null for query and run)
	 */
	void atTimeReq(AtOp op, const char *value);
	/**
	 * @brief Handler for AT+P2P (LoRa P2P radio parameters)
	 *
	 * @param op Operation: query (AT+P2P=?), write (AT+P2P=value) or run (AT+P2P)
	 * @param value Text after the '=' (null for query and run)
	 */
	void atP2p(AtOp op, const char *value);
	/**
	 * @brief Handler for AT+CAD (Channel Activity Detection before send)
	 *
	 * @param op Operation: query (AT+CAD=?), write (AT+CAD=value) or run (AT+CAD)
	 * @param value Text after the '=' (null for query and run)
	 */
	void atCad(AtOp op, const char *value);
	/**
	 * @brief Handler for AT+RXBOOST (RX boosted gain)
	 *
	 * @param op Operation: query (AT+RXBOOST=?), write (AT+RXBOOST=value) or run (AT+RXBOOST)
	 * @param value Text after the '=' (null for query and run)
	 */
	void atRxBoost(AtOp op, const char *value);
	/**
	 * @brief Handler for AT+PFREQ (P2P mode frequency)
	 *
	 * @param op Operation: query (AT+PFREQ=?), write (AT+PFREQ=value) or run (AT+PFREQ)
	 * @param value Text after the '=' (null for query and run)
	 */
	void atPFreq(AtOp op, const char *value);
	/**
	 * @brief Handler for AT+PSF (P2P mode spreading factor)
	 *
	 * @param op Operation: query (AT+PSF=?), write (AT+PSF=value) or run (AT+PSF)
	 * @param value Text after the '=' (null for query and run)
	 */
	void atPSf(AtOp op, const char *value);
	/**
	 * @brief Handler for AT+PBW (P2P mode bandwidth)
	 *
	 * @param op Operation: query (AT+PBW=?), write (AT+PBW=value) or run (AT+PBW)
	 * @param value Text after the '=' (null for query and run)
	 */
	void atPBw(AtOp op, const char *value);
	/**
	 * @brief Handler for AT+PCR (P2P mode coding rate)
	 *
	 * @param op Operation: query (AT+PCR=?), write (AT+PCR=value) or run (AT+PCR)
	 * @param value Text after the '=' (null for query and run)
	 */
	void atPCr(AtOp op, const char *value);
	/**
	 * @brief Handler for AT+PPL (P2P mode preamble length)
	 *
	 * @param op Operation: query (AT+PPL=?), write (AT+PPL=value) or run (AT+PPL)
	 * @param value Text after the '=' (null for query and run)
	 */
	void atPPl(AtOp op, const char *value);
	/**
	 * @brief Handler for AT+PTP (P2P mode TX power)
	 *
	 * @param op Operation: query (AT+PTP=?), write (AT+PTP=value) or run (AT+PTP)
	 * @param value Text after the '=' (null for query and run)
	 */
	void atPTp(AtOp op, const char *value);
	/**
	 * @brief Handler for AT+IQINVER (P2P IQ inversion)
	 *
	 * @param op Operation: query (AT+IQINVER=?), write (AT+IQINVER=value) or run (AT+IQINVER)
	 * @param value Text after the '=' (null for query and run)
	 */
	void atIqInver(AtOp op, const char *value);
	/**
	 * @brief Handler for AT+SYNCWORD (P2P sync word)
	 *
	 * @param op Operation: query (AT+SYNCWORD=?), write (AT+SYNCWORD=value) or run (AT+SYNCWORD)
	 * @param value Text after the '=' (null for query and run)
	 */
	void atSyncWord(AtOp op, const char *value);
	/**
	 * @brief Handler for AT+PSEND (P2P send data)
	 *
	 * @param op Operation: query (AT+PSEND=?), write (AT+PSEND=value) or run (AT+PSEND)
	 * @param value Text after the '=' (null for query and run)
	 */
	void atPSend(AtOp op, const char *value);
	/**
	 * @brief Handler for AT+PRECV (P2P receive)
	 *
	 * @param op Operation: query (AT+PRECV=?), write (AT+PRECV=value) or run (AT+PRECV)
	 * @param value Text after the '=' (null for query and run)
	 */
	void atPRecv(AtOp op, const char *value);
	/**
	 * @brief Handler for AT+PRECVDC (P2P receive with hardware duty cycle)
	 *
	 * @param op Operation: query (AT+PRECVDC=?), write (AT+PRECVDC=value) or run (AT+PRECVDC)
	 * @param value Text after the '=' (null for query and run)
	 */
	void atPRecvDc(AtOp op, const char *value);
	/**
	 * @brief Handler for AT+LOWPOWER (Low power flag)
	 *
	 * @param op Operation: query (AT+LOWPOWER=?), write (AT+LOWPOWER=value) or run (AT+LOWPOWER)
	 * @param value Text after the '=' (null for query and run)
	 */
	void atLowPower(AtOp op, const char *value);
	/**
	 * @brief Handler for AT+SAVE (Save configuration)
	 *
	 * @param op Operation: query (AT+SAVE=?), write (AT+SAVE=value) or run (AT+SAVE)
	 * @param value Text after the '=' (null for query and run)
	 */
	void atSave(AtOp op, const char *value);
	/**
	 * @brief Handler for AT+RESTORE (Restore saved configuration)
	 *
	 * @param op Operation: query (AT+RESTORE=?), write (AT+RESTORE=value) or run (AT+RESTORE)
	 * @param value Text after the '=' (null for query and run)
	 */
	void atRestore(AtOp op, const char *value);
	/**
	 * @brief Handler for AT+FACTORY (Save factory backup)
	 *
	 * @param op Operation: query (AT+FACTORY=?), write (AT+FACTORY=value) or run (AT+FACTORY)
	 * @param value Text after the '=' (null for query and run)
	 */
	void atFactory(AtOp op, const char *value);
	/**
	 * @brief Handler for AT+STATUS (Status dump)
	 *
	 * @param op Operation: query (AT+STATUS=?), write (AT+STATUS=value) or run (AT+STATUS)
	 * @param value Text after the '=' (null for query and run)
	 */
	void atStatus(AtOp op, const char *value);
	/**
	 * @brief Handler for AT+HWMODEL (The string of the hardware model)
	 *
	 * @param op Operation: query (AT+HWMODEL=?), write (AT+HWMODEL=value) or run (AT+HWMODEL)
	 * @param value Text after the '=' (null for query and run)
	 */
	void atHwModel(AtOp op, const char *value);
	/**
	 * @brief Handler for AT+HWID (The string of the hardware ID)
	 *
	 * @param op Operation: query (AT+HWID=?), write (AT+HWID=value) or run (AT+HWID)
	 * @param value Text after the '=' (null for query and run)
	 */
	void atHwId(AtOp op, const char *value);
	/**
	 * @brief Handler for AT+SN (Serial number)
	 *
	 * @param op Operation: query (AT+SN=?), write (AT+SN=value) or run (AT+SN)
	 * @param value Text after the '=' (null for query and run)
	 */
	void atSn(AtOp op, const char *value);
	/**
	 * @brief Handler for AT+VER (Version of the firmware)
	 *
	 * @param op Operation: query (AT+VER=?), write (AT+VER=value) or run (AT+VER)
	 * @param value Text after the '=' (null for query and run)
	 */
	void atVer(AtOp op, const char *value);
	/**
	 * @brief Handler for AT+ALIAS (Alias name of the device)
	 *
	 * @param op Operation: query (AT+ALIAS=?), write (AT+ALIAS=value) or run (AT+ALIAS)
	 * @param value Text after the '=' (null for query and run)
	 */
	void atAlias(AtOp op, const char *value);
	/**
	 * @brief Handler for AT+FIRMWAREVER (Free-form firmware version label)
	 *
	 * @param op Operation: query (AT+FIRMWAREVER=?), write (AT+FIRMWAREVER=value) or run (AT+FIRMWAREVER)
	 * @param value Text after the '=' (null for query and run)
	 */
	void atFirmwareVer(AtOp op, const char *value);
	/**
	 * @brief Handler for AT+ADDMULC (Add multicast group)
	 *
	 * @param op Operation: query (AT+ADDMULC=?), write (AT+ADDMULC=value) or run (AT+ADDMULC)
	 * @param value Text after the '=' (null for query and run)
	 */
	void atAddMulc(AtOp op, const char *value);
	/**
	 * @brief Handler for AT+RMVMULC (Remove multicast group)
	 *
	 * @param op Operation: query (AT+RMVMULC=?), write (AT+RMVMULC=value) or run (AT+RMVMULC)
	 * @param value Text after the '=' (null for query and run)
	 */
	void atRmvMulc(AtOp op, const char *value);
	/**
	 * @brief Handler for AT+LSTMULC (Multicast list)
	 *
	 * @param op Operation: query (AT+LSTMULC=?), write (AT+LSTMULC=value) or run (AT+LSTMULC)
	 * @param value Text after the '=' (null for query and run)
	 */
	void atLstMulc(AtOp op, const char *value);
	/**
	 * @brief Handler for ATZ (MCU Reset)
	 *
	 * @param op Operation: query (ATZ=?), write (ATZ=value) or run (ATZ)
	 * @param value Text after the '=' (null for query and run)
	 */
	void atZ(AtOp op, const char *value);
	/**
	 * @brief Handler for ATR (Restore factory defaults)
	 *
	 * @param op Operation: query (ATR=?), write (ATR=value) or run (ATR)
	 * @param value Text after the '=' (null for query and run)
	 */
	void atR(AtOp op, const char *value);
	/**
	 * @brief Handler for AT+BOOT (Bootloader mode)
	 *
	 * @param op Operation: query (AT+BOOT=?), write (AT+BOOT=value) or run (AT+BOOT)
	 * @param value Text after the '=' (null for query and run)
	 */
	void atBoot(AtOp op, const char *value);

	// The USB CDC RX callbacks are plain C-style hooks (TinyUSB/ESP32 core
	// call them directly, no way to pass a `this` pointer), so a single
	// static instance pointer bridges back to this instance - same pattern
	// as WisBlockLoRaWAN::activeInstanceForTask. Only one WisBlockLoRaAT
	// instance can use background RX mode at a time.
	static WisBlockLoRaAT *activeInstanceForRx;
	static RxWakeCallback rxWakeCb;
};

#endif // WISBLOCK_LORA_AT_H
