/**
 * @file LoRaP2PEngine.h
 * @brief Direct SX1262 radio control for LoRa P2P mode (no LoRaWAN MAC).
 *
 * Calls Semtech's standalone `sx126x_driver` (vendored at
 * src/lbm/smtc_modem_core/radio_drivers/sx126x_driver/) directly, reusing
 * the *same* `sx126x_hal_*` board glue (wisblock_radio_hal_*.cpp) that the
 * LoRaWAN engine's radio driver instance uses. One radio driver, one SPI
 * bus owner, two calling conventions on top of it: LoRaWAN mode drives it
 * through LBM's MAC layer, P2P mode drives it directly from here.
 */
#ifndef LORA_P2P_ENGINE_H
#define LORA_P2P_ENGINE_H

#include "WisBlockLoRaWANTypes.h"

class LoRaP2PEngine
{
public:
	using TxFinishedCb = void (*)(const WisBlockTxResult &);
	using RxFinishedCb = void (*)(const WisBlockRxResult &);
	using CadResultCb = void (*)(WisBlockCADResult);

	void begin(const WisBlockP2PSettings &settings);
	void applySettings(const WisBlockP2PSettings &settings);

	bool send(const uint8_t *data, uint8_t length); // optionally preceded by CAD, see settings.cadEnabled
	void startReceive(uint32_t timeoutMs);		 // 0 = continuous RX
	/**
	 * SX1262 hardware RX duty-cycling (SetRxDutyCycle): the chip
	 * autonomously alternates RX_time / sleep_time on its own, without any
	 * MCU-side wake/resend cycle - genuinely lower average current than
	 * MCU-driven "wake, listen for a bit, sleep, repeat" for the same
	 * effective duty cycle, since the chip handles the alternation itself
	 * instead of needing checkDeviceReady()'s wake sequence (antenna
	 * power, SPI, BUSY wait) run over and over from software.
	 *
	 * Per the datasheet: if a preamble is detected during an RX phase, the
	 * chip extends reception to receive the full packet regardless of
	 * rxTimeMs, then reports RX_DONE/CRC_ERROR/etc. the same as a normal
	 * startReceive() - IRQ routing and event dispatch are identical, this
	 * only changes how the radio behaves *between* packets. If nothing is
	 * ever received, this runs indefinitely, continuing to alternate RX
	 * and sleep phases - call stopReceive() to cancel it.
	 *
	 * @param rxTimeMs How long each RX phase listens before sleeping, if
	 *   nothing is detected.
	 * @param sleepTimeMs How long the chip sleeps between RX phases.
	 */
	void startReceiveDutyCycle(uint32_t rxTimeMs, uint32_t sleepTimeMs);
	void stopReceive();

	/** Read-only access to the currently applied P2P radio settings - frequency, SF, bandwidth, preamble length, etc. */
	const WisBlockP2PSettings &getSettings() const { return settings; }

	/**
	 * Computes safe rxTimeMs/sleepTimeMs for startReceiveDutyCycle() from
	 * the *currently configured* bandwidth/SF/preamble length (getSettings()) -
	 * see startReceiveDutyCycle()'s doc comment for the underlying
	 * constraint this respects: rxTimeMs + sleepTimeMs must stay under the
	 * transmitting side's actual over-the-air preamble duration, with
	 * margin, or a packet can start and finish its entire preamble while
	 * this radio is asleep and never get caught.
	 *
	 * This assumes the transmitting node's preamble length matches
	 * settings.preambleLength - it usually doesn't in practice, since only
	 * the transmitter's actual over-the-air preamble length affects
	 * detectability (the receiver's own configured preambleLength has no
	 * bearing on what it can detect - two devices can, and often do,
	 * complete a link perfectly normally with completely different
	 * preambleLength settings on each side). Use the overload below and
	 * pass the transmitter's real value whenever you know it, which is
	 * effectively always - this overload exists mainly for the AUTO
	 * convenience path (AT+PRECVDC=AUTO) where nothing else is available to
	 * go on.
	 *
	 * @param marginSymbols Controls both the RX window size and the extra
	 *   headroom subtracted from the sleep budget - increase this if you
	 *   see missed packets in practice. NOTE: real hardware testing found
	 *   this needs considerably more margin than the datasheet's own
	 *   worked examples suggest to reliably catch every packet - this
	 *   function's default assumptions have already been adjusted based on
	 *   that testing (see the implementation's comments and the README's
	 *   "RX duty-cycle timing needed more margin than the textbook
	 *   formula" note), but treat the result as a starting point to verify
	 *   on your own hardware/link, not a guaranteed-correct value.
	 * @param rxTimeMs Out: computed RX phase duration, scaled from
	 *   marginSymbols.
	 * @param sleepTimeMs Out: computed sleep phase duration - the rest of
	 *   the preamble's duration after rxTimeMs and the margin.
	 * @returns false (outputs left untouched) if the current preamble is
	 *   too short to fit any usable window at all - call
	 *   setP2PPreambleLength() with a larger value first in that case, or
	 *   just use startReceive() instead of duty-cycling.
	 */
	bool computeRxDutyCycleTiming(uint32_t &rxTimeMs, uint32_t &sleepTimeMs, uint8_t marginSymbols = 5) const;

	/**
	 * Same computation as above, but against an explicitly given
	 * transmitter preamble length instead of this radio's own
	 * settings.preambleLength - use this one. See the other overload's
	 * doc comment for why the two can, and usually do, legitimately
	 * differ: the receiver's own preambleLength setting doesn't gate what
	 * it can detect, only the transmitter's actual over-the-air preamble
	 * length does.
	 *
	 * @param txPreambleLengthSymbols The *transmitting* node's actual
	 *   configured preamble length, in symbols - not this radio's own.
	 */
	bool computeRxDutyCycleTiming(uint16_t txPreambleLengthSymbols, uint32_t &rxTimeMs, uint32_t &sleepTimeMs,
								   uint8_t marginSymbols = 5) const;

	void startCad(); // one-shot CAD; result delivered via onCadResult callback

	/**
	 * Puts the radio into low-power sleep (SX126x SetSleep, cold-start).
	 *
	 * FIX: was previously warm-start (configuration retained across sleep).
	 * That's the more convenient option - the next send()/startReceive()/
	 * startCad() can just wake and go, no reconfiguration needed - but
	 * warm-start's RAM retention requires an internal regulator to stay
	 * active through the whole sleep, and a real hardware A/B comparison
	 * against a known-good reference implementation on the same board
	 * (SX126x-Arduino, ~50uA P2P sleep) turned up roughly 20x more idle
	 * current than that reference - after every other candidate (antenna
	 * power, wake-sequence latency) had already been ruled out by direct
	 * testing. Cold-start drops that retention regulator entirely, at the
	 * cost of losing all radio configuration - so every wake now needs a
	 * full reconfigure (see reconfigureAfterColdSleep()) before the radio
	 * can do anything, handled transparently by send()/startReceive()/
	 * startCad() each calling it first.
	 *
	 * Documented as a well-reasoned but NOT YET HARDWARE-CONFIRMED fix -
	 * unlike this library's other fixes, this one hasn't had a before/after
	 * power measurement on real hardware yet. If it doesn't move the
	 * needle, warm-start plus the retention regulator was never the actual
	 * cause of the residual idle current, and something else entirely is -
	 * worth ruling this in or out with a real capture before chasing
	 * anything further.
	 */
	void sleep();

	/** Checks/clears radio IRQ status and dispatches callbacks. Call every loop().
	 * Always returns a large fixed value (P2P mode has no self-scheduling contract like
	 * LoRaWAN's smtc_modem_run_engine() - it's purely IRQ-driven) - see LoRaWANEngine::handleEvents(). */
	uint32_t handleEvents();

	void onTxFinished(TxFinishedCb cb) { txFinishedCb = cb; }
	void onRxFinished(RxFinishedCb cb) { rxFinishedCb = cb; }
	void onCadResult(CadResultCb cb) { cadResultCb = cb; }

private:
	WisBlockP2PSettings settings;
	TxFinishedCb txFinishedCb = nullptr;
	RxFinishedCb rxFinishedCb = nullptr;
	CadResultCb cadResultCb = nullptr;
	uint8_t lastTxPayloadLength = 0; // captured in send(), consumed in handleEvents() to compute airtime on TX_DONE

	// FIX: see sleep()'s doc comment - cold-start SLEEP loses all radio
	// configuration, so every wake needs a full reconfigure before the
	// radio can do anything. Set true in sleep(); reconfigureAfterColdSleep()
	// clears it once the reconfigure has run.
	bool needsReconfigureAfterSleep = false;

	void applyRadioParams(); // pushes settings into sx126x_set_lora_mod_params / sx126x_set_lora_pkt_params
	/** Re-runs everything begin() does except sx126x_reset() - the part of
	 * initial setup a cold-start sleep wipes. No-op if not currently needed
	 * (i.e. the radio was never put to cold sleep, or already reconfigured
	 * since the last wake) - safe to call unconditionally at the top of
	 * every send()/startReceive()/startCad(). */
	void reconfigureAfterColdSleep();
};

#endif // LORA_P2P_ENGINE_H
