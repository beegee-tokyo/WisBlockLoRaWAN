/**
 * @file wisblock_lbm_task.h
 * @brief Optional FreeRTOS background task that eliminates the need to call
 * WisBlockLoRaWAN::handleEvents() from loop().
 *
 * Modeled directly on beegee-tokyo/SX126x-Arduino's own architecture
 * (src/boards/mcu/board.cpp's start_lora_task()/_lora_task(), and the
 * matching xSemaphoreGiveFromISR() call in src/radio/sx126x/radio.cpp's
 * DIO1 IRQ handling): a binary semaphore is given whenever there's radio
 * or timer work to do, and a background task blocks on that semaphore
 * (xSemaphoreTake(..., portMAX_DELAY)) instead of the application busy-
 * polling handleEvents() every loop() iteration.
 *
 * Why this matters for power consumption: with nothing to busy-poll,
 * loop() can be trivial (or itself sleep), and FreeRTOS's own tickless
 * idle behavior (built into both the Adafruit nRF52 core and the ESP32
 * Arduino core) automatically drops the MCU into a low-power idle state
 * whenever no task is ready to run - genuine current savings, not just
 * fewer CPU cycles spent polling.
 *
 * Three things determine how soon the background task wakes:
 *   - DIO1 radio IRQ (via notifyFromISR(), called from the DIO1 ISR in
 *     wisblock_lbm_port.cpp)
 *   - LBM's own software timer (retransmission/join backoff/etc. -
 *     scheduleTimer()/cancelTimer() replace the millis()-polled
 *     WisBlockLbmPort::tick() mechanism when task mode is active)
 *   - The wait budget `eventHandler` itself returns each call: LBM's
 *     smtc_modem_run_engine() docs are explicit that "this function must
 *     be called in main loop... it returns an amount of ms after which
 *     the function must at least be called again" - this is not optional,
 *     and nothing above guarantees it in the gap between an application
 *     calling e.g. join() and the first external trigger firing. The task
 *     bounds its semaphore wait by this returned value so it
 *     self-reschedules even if nothing else wakes it first.
 *
 * Platform availability:
 *   - nRF52 (RAK4631): FreeRTOS ships built into the Adafruit nRF52 core -
 *     no extra dependency.
 *   - ESP32 (RAK3312): the ESP32 Arduino core itself runs on FreeRTOS -
 *     no extra dependency.
 *   - RP2040 (RAK11310): NOT available by default on the plain
 *     arduino-pico core. Requires a FreeRTOS-Kernel port to be added to
 *     your project first (e.g. via the arduino-pico core's "FreeRTOS"
 *     option, where available, or the standalone FreeRTOS-Kernel library).
 *     start() returns false if FreeRTOS isn't available, and the caller
 *     should fall back to manual handleEvents() polling in that case.
 */
#ifndef WISBLOCK_LBM_TASK_H
#define WISBLOCK_LBM_TASK_H

#include <stdint.h>

namespace WisBlockLbmTask
{
/**
 * Starts the background task. `eventHandler` is called (from the task,
 * normal task context - not an ISR) every time the semaphore is given, OR
 * when the previous call's returned wait budget elapses, whichever comes
 * first - see the header comment above for why the latter is required
 * (LBM's smtc_modem_run_engine() must be called again within a bounded
 * time regardless of external events; a purely wait-forever task would
 * starve it in the gap between an API call like join() and the first
 * external trigger). Pass a function that calls
 * WisBlockLoRaWAN::handleEvents() and returns its result.
 *
 * Returns false if FreeRTOS isn't available on this platform/build, or if
 * task/semaphore creation failed - the caller should fall back to manual
 * handleEvents() polling in that case.
 */
bool start(uint32_t (*eventHandler)());

/** True if start() has previously succeeded. */
bool isActive();

/** Wakes the background task. Safe to call from ISR context (e.g. the DIO1 IRQ). */
void notifyFromISR();

/**
 * Wakes the background task from ordinary (non-ISR) task context - the
 * application's own loop() task, an AT-command handler, etc. FIX: nothing
 * previously did this. join()/sendLoRaWAN()/etc. all queue work directly
 * into LBM's own task/event queue via smtc_modem_api calls made from
 * whatever task the application happens to call them from - but queuing a
 * request doesn't itself cause smtc_modem_run_engine() to run again. If
 * the background task was already asleep waiting for its own
 * previously-computed deadline (bounded by kMaxWaitMs - up to a minute),
 * a freshly queued uplink could sit untouched for the entire remainder of
 * that wait before ever being dispatched to the radio, looking exactly
 * like the request had silently vanished (see the README's "Queued send
 * not dispatched promptly" note for a real log capture: over 30 seconds
 * between smtc_modem_request_uplink() succeeding and the actual over-the-
 * air transmission). join() and send() (WisBlockLoRaWAN.cpp) now call this
 * right after successfully queuing something with LBM, so the background
 * task gets a chance to call smtc_modem_run_engine() again promptly
 * instead of waiting out its old deadline.
 */
void notify();

/**
 * Replaces WisBlockLbmPort's millis()-polled software timer when task mode
 * is active: schedules `callback(context)` to run once, `milliseconds`
 * from now, then wakes the background task so handleEvents() gets pumped
 * right after - matching the two-step sequence (fire timer callback, then
 * call handleEvents()) the polled tick()-based mechanism already did, just
 * driven by an RTOS timer instead of a busy loop.
 */
void scheduleTimer(uint32_t milliseconds, void (*callback)(void *context), void *context);

/** Cancels a pending scheduleTimer() call, if any. */
void cancelTimer();

/**
 * Guards all access into LBM's smtc_modem_api. Needed because background
 * task mode means this module's own event task calls
 * smtc_modem_run_engine() from one FreeRTOS task, while AT commands
 * processed via WisBlockLoRaAT::enableBackgroundRx() can call other
 * smtc_modem_api functions (e.g. AT+SEND -> smtc_modem_request_uplink())
 * from a *different* task (the USB CDC RX callback's task context) - LBM's
 * internal state isn't documented as safe for concurrent access from
 * multiple tasks, so every entry point needs to serialize through this.
 *
 * No-op (returns/does nothing immediately) if start() was never called
 * successfully - a bare-metal, single-task build has nothing to race
 * against in the first place.
 */
void lock();
void unlock();
} // namespace WisBlockLbmTask

#endif // WISBLOCK_LBM_TASK_H
