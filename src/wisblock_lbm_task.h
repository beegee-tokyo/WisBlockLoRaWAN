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
 * fewer CPU cycles spent polling. (Arduino-Pico's FreeRTOS SMP has NO
 * tickless idle: the 1 kHz tick keeps running and the idle task only does
 * __wfe(). The RP2040 saves CPU time, but not as much current.)
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
 *   - RP2040 (RAK11300/RAK11310, Arduino-Pico core): FreeRTOS SMP is part of
 *     the core but has to be enabled: Arduino IDE Tools -> Operating System
 *     -> FreeRTOS SMP, PlatformIO build_flags = -DPIO_FRAMEWORK_ARDUINO_ENABLE_FREERTOS
 *     (both define __FREERTOS). The task runs at priority configMAX_PRIORITIES/2+1
 *     (loop() runs at configMAX_PRIORITIES/2) pinned to core 0. Call the library
 *     from setup()/loop() (core 0), not from setup1()/loop1(). The FreeRTOS timer
 *     task cannot be pinned and runs at priority 2. start() returns false if
 *     FreeRTOS isn't enabled, and the caller should fall back to manual
 *     handleEvents() polling in that case.
 */
#ifndef WISBLOCK_LBM_TASK_H
#define WISBLOCK_LBM_TASK_H

#include <stdint.h>

namespace WisBlockLbmTask
{
/**
 * @brief Start the background task that drives the LoRa engine
 *
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
 *
 * @param eventHandler Function called every time the task is woken up, it returns the time in ms until it must be called again
 * @return true if the task is running, false if FreeRTOS is not available
 */
bool start(uint32_t (*eventHandler)());

/**
 * @brief Check if the background task is running
 *
 * True if start() has previously succeeded.
 *
 * @return true if start() succeeded
 */
bool isActive();

/**
 * @brief Wake up the background task from an interrupt
 *
 * Wakes the background task. Safe to call from ISR context (e.g. the DIO1 IRQ).
 */
void notifyFromISR();

/**
 * @brief Wake up the background task from a normal task
 *
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
 * @brief Start the software timer of the LoRa Basics Modem
 *
 * Replaces WisBlockLbmPort's millis()-polled software timer when task mode
 * is active: schedules `callback(context)` to run once, `milliseconds`
 * from now, then wakes the background task so handleEvents() gets pumped
 * right after - matching the two-step sequence (fire timer callback, then
 * call handleEvents()) the polled tick()-based mechanism already did, just
 * driven by an RTOS timer instead of a busy loop.
 *
 * @param milliseconds Time until the callback is called
 * @param callback Function called when the timer expires
 * @param context Context passed to the callback
 */
void scheduleTimer(uint32_t milliseconds, void (*callback)(void *context), void *context);

/**
 * @brief Cancel the software timer
 *
 * Cancels a pending scheduleTimer() call, if any.
 */
void cancelTimer();

/**
 * @brief Take the lock that guards the access to the LoRa Basics Modem
 *
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
/**
 * @brief Release the lock taken with lock()
 */
void unlock();
} // namespace WisBlockLbmTask

#endif // WISBLOCK_LBM_TASK_H
