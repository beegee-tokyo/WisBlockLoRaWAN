/**
 * @file wisblock_lbm_port.h
 * @brief Board glue for Semtech LoRa Basics Modem v4.9.0's `smtc_modem_hal.h`
 * porting contract (vendored at src/lbm/smtc_modem_hal/smtc_modem_hal.h).
 *
 * `wisblock_lbm_port.cpp` implements every `smtc_modem_hal_*` function LBM
 * requires directly (see that header for the authoritative list: reset,
 * watchdog, time, timer, IRQ enable/disable, context store/restore/erase,
 * panic, random, radio IRQ config, radio env (tcxo/antenna switch/battery/
 * board delay), trace, FUOTA metadata, temperature/voltage, crashlog, and
 * the RTOS notification hook). This header only exposes the one setup call
 * the rest of the library needs.
 */
#ifndef WISBLOCK_LBM_PORT_H
#define WISBLOCK_LBM_PORT_H

namespace WisBlockLbmPort
{
/**
 * Attaches the DIO1 interrupt (feeding smtc_modem_hal_irq_config_radio_irq's
 * registered callback) and initializes the software timer used by
 * smtc_modem_hal_start_timer/stop_timer. Call once from
 * WisBlockLoRaWAN::begin(), after WisBlockRadioHal::init().
 */
void init();

/**
 * Services the software timer (fires the callback smtc_modem_hal_start_timer
 * registered, once its deadline passes). Call every loop(); this is what
 * lets smtc_modem_run_engine()'s requested sleep_time_ms actually elapse on
 * a bare-metal Arduino build with no RTOS tick.
 */
void tick();

/**
 * True if DIO1 has risen since the last call (consumes/clears the flag).
 * LoRaWAN mode doesn't need this - LBM's own radio IRQ callback (registered
 * via smtc_modem_hal_irq_config_radio_irq once smtc_modem_init() runs)
 * already gets invoked straight from the same ISR. LoRaP2PEngine uses this
 * instead, since it drives the radio directly and never triggers that
 * registration.
 */
bool consumeRadioIrqFlag();
} // namespace WisBlockLbmPort

#endif // WISBLOCK_LBM_PORT_H
