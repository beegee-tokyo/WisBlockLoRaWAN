/**
 * @file WisBlockLoRaAT.h
 * @brief AT command parser/dispatcher, a thin text protocol over
 * WisBlockLoRaWAN's C++ API (see README.md for the full command table).
 */
#ifndef WISBLOCK_LORA_AT_H
#define WISBLOCK_LORA_AT_H

#include "WisBlockLoRaWAN.h"
#include <Stream.h>
#include <stddef.h> // size_t

class WisBlockLoRaAT
{
public:
	/** Called for a complete line that doesn't start with "AT" - i.e. not one of this library's own commands. */
	using UnhandledDataCb = void (*)(const char *line);

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

private:
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

	// Shared byte-accumulation logic: reads everything currently available
	// from `port` and feeds it into the line buffer, dispatching
	// processLine() on each complete line. Used by both handleSerial()
	// (loop()-polled) and the background RX path (called from the USB CDC
	// RX callback instead).
	void processIncomingBytes();

	static bool parseHex(const char *hex, uint8_t *out, size_t outLen);

	// The USB CDC RX callbacks are plain C-style hooks (TinyUSB/ESP32 core
	// call them directly, no way to pass a `this` pointer), so a single
	// static instance pointer bridges back to this instance - same pattern
	// as WisBlockLoRaWAN::activeInstanceForTask. Only one WisBlockLoRaAT
	// instance can use background RX mode at a time.
	static WisBlockLoRaAT *activeInstanceForRx;
};

#endif // WISBLOCK_LORA_AT_H
