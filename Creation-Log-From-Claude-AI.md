# WisBlockLoRaWAN

Arduino library skeleton for **LoRaWAN (Class A/B/C + Relay)** and **LoRa P2P** on
RAKwireless WisBlock modules using the built-in **Semtech SX1262** transceiver:

| Board     | MCU                | Arduino core         |
|-----------|--------------------|-----------------------|
| RAK4631   | Nordic nRF52840    | Adafruit nRF52 core   |
| RAK3312   | Espressif ESP32-S3 | ESP32 Arduino core    |
| RAK11310  | Raspberry Pi RP2040| arduino-pico core     |

The LoRaWAN MAC layer is designed to sit on top of **Semtech's LoRa Basics Modem
(LBM / "BasicModem")**, `smtc_modem_api`, rather than reimplementing the MAC
from scratch. LoRa P2P uses the SX1262 radio driver directly (via
`SX126x-Arduino` or the `smtc_ral`/`smtc_radio` HAL from LBM, either works).

## ⚠️ Honest scope statement from Claude AI

A full certified LoRaWAN Class A/B/C + Relay stack is a multi-thousand-hour
engineering effort (this is why Semtech ships it as a closed, tested library
rather than sample code). **This repository is an integration scaffold, not a
finished, certified MAC layer.** It gives you:

- A complete, working **AT command parser** and **C++ API surface**.
- A complete **persistent configuration store** (per-MCU flash/NVS backends).
- A complete **board / pin abstraction** for the three targets.
- A **porting layer** (`wisblock_lbm_port.*`) with the exact function
  signatures LBM expects (`smtc_modem_hal_*`), stubbed and commented with
  what each must do.
- All **call sites** into `smtc_modem_api.h` (join, send, class switch, ADR,
  relay, time req, link check, CAD, P2P tx/rx) wired to the API/AT layer,
  marked `/* TODO: verify against your LBM version */` where the exact
  struct layout depends on the LBM release you vendor in.

To finish it you need to:

1. Clone Semtech's `LoRaBasicsModem` (LBM) repo and vendor `smtc_modem_api`,
   `smtc_modem_core`, and the SX1262 radio driver (`radio_planner`,
   `sx126x_*`) into `src/lbm/`.
2. Implement the ~20 functions in `wisblock_lbm_port.cpp`
   (`smtc_modem_hal_*`: get_time, timers, IRQ, flash read/write for LBM's
   own context store, RNG, watchdog, trace).
3. ~~Implement `wisblock_radio_hal.cpp` — SPI + DIO1/DIO2/BUSY/RESET GPIO
   glue per board~~ **Done for all three boards** in
   `wisblock_radio_hal_rak4631.cpp` / `wisblock_radio_hal_rak3312.cpp` /
   `wisblock_radio_hal_rak11310.cpp`: each implements Semtech's real
   `sx126x_hal_reset/wakeup/write/read` contract (over the nRF52, ESP32-S3,
   and arduino-pico Arduino SPI cores respectively), so they satisfy the BSP
   requirement of both the standalone `sx126x_driver` and LBM's radio
   wrapper with no adapter code needed. All three deliberately ignore the
   `context` pointer LBM passes in and operate on a static, board-specific
   context instead, since LBM's own `smtc_modem.c` instantiates the radio
   as `RALF_SX126X_INSTANTIATE(NULL)` - the pointer is always NULL in
   practice, and each backend only supports one radio anyway.
4. ~~Fill in the relay config struct in `LoRaWANRelay.h`~~ **Done and
   verified against v4.9.0 source** in `LoRaWANRelay.h`/`.cpp`: relay TX
   (end-device role) is a clean public API
   (`smtc_modem_relay_tx_enable/disable`, `smtc_modem_relay_tx_config_t`),
   fully wired up including `AT+RELAYED`. Relay RX (serving role) has no
   public API entry point in v4.9.0 - it's reached via the internal
   `lorawan_api_stack_mac_get()` + `relay_init()`/`relay_rx_api.h`, gated
   behind an `ADD_RELAY_RX` build flag (`AT+RELAYSRV`, `AT+RELAYDEV`).
   **Important finding**: a serving relay forwards nothing until you
   register each trusted end-device's DevAddr + WOR root session key via
   `relay_fwd_uplink_add_device()` (`AT+RELAYDEV`) - there's no
   auto-discovery/auto-trust mechanism, and this is easy to miss since
   `relay_start()` succeeds silently either way. The relay RX path is
   still **unverified against real hardware/network traffic** (reconstructed
   from reading the internal API, not a documented Semtech example) - test
   it thoroughly before relying on it.
5. Run Semtech's / LoRa Alliance's certification test suite before shipping
   anything class-B/C or relay onto a real network — timing (especially
   Class B ping slot / beacon sync) is unforgiving of shortcuts.

## Patches made to vendored LBM source

This library keeps the vendored `src/lbm/` tree as close to upstream as
possible, but the following changes were necessary:

1. **Renamed `stream.h`/`stream.c` → `lbm_stream_service.h`/`.c`** (in
   `smtc_modem_core/modem_services/`, since deleted - see below). LBM's own
   internal FUOTA streaming-service header had the same filename as
   Arduino core's `Stream.h` - harmless on a case-sensitive filesystem, but
   a silent, catastrophic collision on Windows/macOS, where the two are
   indistinguishable and whichever one lands earlier on the include path
   wins everywhere in the project, including unrelated libraries.
2. **Added `__attribute__((noreturn))`** to `smtc_modem_hal_on_panic()`'s
   declaration in `smtc_modem_hal/smtc_modem_hal.h`. Purely cosmetic - our
   implementation of that function (`wisblock_lbm_port.cpp`) genuinely
   never returns (it spins forever), and marking it as such quiets a batch
   of harmless `-Wreturn-type` warnings GCC otherwise emits throughout
   `lorawan_api.c` for every function that panics on a bad `stack_id`.

(A third, temporary diagnostic patch - printing the return status of each
call inside `ral_sx126x_set_tx_cfg()`/`ral_sx126x_init()`/
`ralf_sx126x_setup_lora()` - was added and later fully removed while
chasing a `PANIC ... ralf_setup_lora(...) == RAL_STATUS_OK` at join time.
**Root cause**: `wisblock_radio_hal_*.cpp`'s SPI HAL didn't track SX126x
sleep state - see "Radio HAL: sleep-state-aware wake handling" below for
the actual fix, which lives entirely in this library's own code, not
vendored LBM source.)

Additionally, several LBM features **not requested in the original spec are
deliberately not vendored at all**: FUOTA (fragmented data block transport,
firmware management protocol, multi-package access), multicast, LoRaWAN
application layer clock sync, cloud device management, LFU (log file
upload), store-and-forward, the beacon-TX *test/demo* service (a simulated
beacon transmitter for exercising Class B without a real gateway - not
needed for normal Class B operation), almanac packages, geolocation
services (LR11xx-only, not applicable to the SX1262 modules these boards
use), and the WW2G4 (2.4GHz worldwide) region (LR11xx/SX128x-only, not
applicable to SX1262's sub-GHz-only radio - unlike the other exclusions
above, `region_ww_2g4.c` was NOT self-guarded behind an `#ifdef`, so
leaving it in without defining `REGION_WW_2G4` produced a real compile
error rather than silently compiling to nothing; removing the file was the
fix, not an extra define), and the LR11xx hardware crypto engine backend
(`smtc_modem_crypto/lr11xx_crypto_engine/` - LBM supports either this,
tied to the LR11xx's built-in secure element, or `soft_secure_element/`,
pure-software AES/CMAC with no special hardware; SX1262 has no secure
element, so `soft_secure_element` is the only applicable backend and is
what's vendored). All of their `#include`s elsewhere in LBM are
properly guarded behind `#if defined(...)` macros that this library
doesn't define, so removing the directories is safe - verified by grepping
every reference to each removed header across the full vendored tree
before deletion. If you need any of these later, see the comment at the
top of `extra_script.py` for how to re-vendor and wire one back in.

## Low power: background task mode (no more loop()-polled handleEvents())

By default, `handleEvents()` must be called from `loop()` every iteration -
it pumps `smtc_modem_run_engine()`, drains `smtc_modem_get_event()`, and
services the software timer LBM's own retransmission/join-backoff
scheduling relies on. Busy-polling this from `loop()` works, but it means
`loop()` itself can never really idle, which gets in the way of real power
savings.

`WisBlockLoRaWAN::enableBackgroundTask()` (call once, after `begin()`)
replaces that polling with a FreeRTOS task that blocks on a binary
semaphore (`xSemaphoreTake(..., portMAX_DELAY)`) instead - modeled directly
on beegee-tokyo/SX126x-Arduino's own architecture
(`start_lora_task()`/`_lora_task()` in `board.cpp`, and the matching
`xSemaphoreGiveFromISR()` in `radio.cpp`'s DIO1 handling). Two wake sources
converge on the same semaphore: the DIO1 radio IRQ, and LBM's own software
timer (now backed by a real FreeRTOS software timer instead of a
`millis()`-polled deadline). Once enabled, `loop()` no longer needs to call
`handleEvents()` at all (calling it anyway is a harmless no-op, so existing
sketches can adopt this incrementally) - see `examples/LowPowerLoRaWAN/`.

The actual power savings come from FreeRTOS's own tickless idle behavior
(built into both the Adafruit nRF52 core and the ESP32 Arduino core):
with nothing demanding CPU time between events, the scheduler automatically
drops the MCU into a low-power idle state on its own.

**Platform availability**: works out of the box on RAK4631 (nRF52) and
RAK3312 (ESP32-S3) - both cores already run FreeRTOS. RAK11310 (RP2040)
needs a FreeRTOS-Kernel port added to the project first (the plain
arduino-pico core doesn't include one by default); `enableBackgroundTask()`
returns `false` if FreeRTOS isn't available, and the library falls back to
requiring `loop()`-level `handleEvents()` polling exactly as before - see
`wisblock_lbm_task.h` for the full detail.

## Background AT command processing (no more loop()-polled handleSerial())

`WisBlockLoRaAT::enableBackgroundRx()` (call once, after `begin()`) is the
AT-layer equivalent of `WisBlockLoRaWAN::enableBackgroundTask()`: instead of
`handleSerial()` being polled from `loop()`, incoming USB CDC data is read
and dispatched directly from the platform's own USB RX callback -
TinyUSB's `tud_cdc_rx_cb()` on RAK4631, the native USB CDC RX event on
RAK3312. Once enabled, `loop()` no longer needs to call `handleSerial()` at
all (calling it anyway is a harmless no-op).

Two things worth knowing:

- **Non-AT input is forwarded, not swallowed.** Register
  `onUnhandledData(callback)` to receive any line that doesn't start with
  `"AT"` - useful if your application wants its own serial protocol on the
  same port. A line that *does* start with `"AT"` but isn't a command this
  library recognizes still gets the normal `ERROR: unknown command` reply,
  on the assumption it was meant for this parser and is just
  malformed/unsupported.
- **Thread safety.** The USB RX callback runs on a different FreeRTOS task
  than `WisBlockLbmTask`'s background event task (see "Low power" above).
  Since AT commands like `AT+SEND` call into the same LBM engine state that
  task also touches, `WisBlockLoRaAT` wraps each dispatched command in
  `WisBlockLoRaWAN::lockLbm()`/`unlockLbm()` - a mutex shared with the
  background task - so the two can't corrupt LBM's internal state by
  touching it concurrently. This is automatic; you don't need to do
  anything for it, but it's worth knowing it's there if you're debugging
  unexpected latency on an AT command that happens to land while the
  background task is mid-cycle.

**Platform availability**: RAK4631 and RAK3312 only (matching the request
that motivated this feature) - RAK11310 doesn't have an equivalent USB RX
hook wired up, `enableBackgroundRx()` returns `false` there and the library
falls back to `loop()`-polled `handleSerial()`. Two caveats specific to
this feature (not shared with `enableBackgroundTask()`):

- Only one `WisBlockLoRaAT` instance can use background RX mode at a time.
- It installs the `tud_cdc_rx_cb()` weak-symbol hook (RAK4631) or an
  `ARDUINO_HW_CDC_EVENTS` handler via `Serial.onEvent()` (RAK3312) - this
  **cannot coexist** with your own sketch defining either of those itself.
  The RAK3312 implementation assumes the `HWCDC`-based native USB CDC event
  API; if your installed esp32-arduino core version exposes this
  differently (`USBCDC` vs `HWCDC` has varied across core releases), see
  the `TODO` comment in `WisBlockLoRaAT.cpp`'s `usbEventCallback()`.

## LoRa P2P: radio never slept (fixed - was causing elevated baseline current)

`LoRaP2PEngine.cpp` never called `sx126x_set_sleep()` anywhere - after
every TX, RX, or CAD operation the radio was left in STANDBY, not SLEEP.
With the TCXO active, STANDBY draws several mA continuously; SLEEP draws
roughly 1.5uA. This is a completely different bug from the missing PA
config above (that one affected TX *power*; this one affects *idle*
current), found while investigating a report of 1-3mA baseline current in
both P2P and LoRaWAN modes.

Fixed by adding `LoRaP2PEngine::sleep()` (`sx126x_set_sleep(WARM_START)` -
warm-start retains the radio's configuration, so the next
`send()`/`startReceive()`/`startCad()` doesn't need to reconfigure
anything, just wake and go) and exposing it as
`WisBlockLoRaWAN::sleepRadio()`. This needed no changes to
`wisblock_radio_hal_*.cpp`'s sleep-state tracking at all - it's already
keyed off the raw SetSleep opcode byte (`0x84`) rather than tied to any
specific call site, so it picked up this new caller transparently (see
"Radio HAL: sleep-state-aware wake handling" below).

**This is opt-in, not automatic** - deliberately, since automatically
sleeping after every single radio operation risks breaking legitimate
"TX then immediately listen for a reply" patterns where the application
wants the radio to stay awake between them. Call `sleepRadio()` whenever
your application knows it has no immediate P2P radio activity coming up -
see `examples/LowPowerLoRaP2P/`'s `onRxDone()` for the pattern (sleep once
the RX window concludes, whether a packet arrived or it timed out).
No-op in LoRaWAN mode, where `radio_planner` already handles this
automatically between scheduled tasks (see
`smtc_modem_core/radio_planner/src/radio_planner.c`'s `ral_set_sleep()`
calls).

**On the reported periodic ~250ms current peak specifically**: this
remains unresolved. I searched exhaustively - every timing constant in
this library's own source, the vendored LBM tree's relevant paths, and the
example sketches - and found nothing that schedules anything at a ~250ms
interval. Since it was reported in both P2P and LoRaWAN modes, and P2P
mode doesn't touch LBM's engine at all, the cause (if still present after
the radio-sleep fix above) is likely external to this library's own code -
worth checking, roughly in order of how easy each is to test:

1. **USB/TinyUSB activity** - if measuring with USB still connected,
   TinyUSB's device task services the USB bus continuously regardless of
   application activity. Try measuring with USB fully disconnected
   (battery/external supply only) and see if the peak disappears.
2. **FreeRTOS tickless idle configuration** - `WisBlockLbmTask`'s event
   task blocks on a bounded `xSemaphoreTake()`, which only achieves real
   low-power idle between wakes if `configUSE_TICKLESS_IDLE` is enabled in
   your specific core's `FreeRTOSConfig.h`. If it isn't, the CPU wakes on
   every RTOS tick regardless of task readiness - check your installed
   Adafruit nRF52 / ESP32 core version's FreeRTOS configuration.
3. If neither explains it, the next concrete diagnostic step would be
   correlating the current peak against `DIO1` toggling (logic analyzer or
   scope on that pin) to confirm whether it's radio-related at all, versus
   an MCU-only artifact.

## LoRa P2P: missing PA configuration (fixed - was causing severely reduced TX power)

`LoRaP2PEngine.cpp`'s `applyRadioParams()` called `sx126x_set_tx_params()`
to set the requested power level, but never called `sx126x_set_pa_cfg()` at
all. Per the SX126x datasheet, `SetTxParams` alone does not select which
power amplifier is active - without an explicit `SetPaConfig` call, the
chip runs on its power-on-reset PA default (the low-power LP PA path, not
the SX1262's HP PA capable of +22dBm), silently capping actual radiated
power far below whatever dBm value was requested.

This was specific to P2P mode: LoRaWAN mode's radio config path
(`wisblock_ral_sx126x_bsp.c`'s `ral_sx126x_bsp_get_tx_cfg()`) already
configured the PA correctly, since it's a required part of LBM's own
`ral_sx126x_set_tx_cfg()` sequence. P2P mode talks to the raw
`sx126x_driver` directly rather than through that path, and the equivalent
call was simply missing when `LoRaP2PEngine.cpp` was first written.

Symptom: LoRa P2P packets arriving dramatically weaker than expected -
identified by comparing RSSI against a reference implementation
(beegee-tokyo/SX126x-Arduino) transmitting at the same requested power with
identical hardware/antennas (~-101dBm received vs. ~-51dBm for the
reference, a ~50dB gap). Fixed by adding the same `sx126x_cfg_tx_clamp()` +
`sx126x_set_pa_cfg()` sequence (HP PA, `device_sel=0x00`, `hp_max=0x07`,
`pa_duty_cycle=0x04`) already verified correct for these boards in
`wisblock_ral_sx126x_bsp.c`, plus power clamping to the HP PA's valid
-9..+22dBm range.

## Power consumption: three fixes for elevated current draw / constant wake

Field testing on RAK4631 turned up three separate causes of higher-than-expected
power draw, on top of the LoRa P2P radio-sleep and PA-config fixes documented
below. All three are now fixed, in all three board HALs
(`wisblock_radio_hal_rak4631/rak3312/rak11310.cpp`), not just RAK4631's:

1. **DIO1 left floating (`pinMode(LORA_DIO1, INPUT)`, no pull) - background
   task woke constantly.** A floating input drifts across the digital
   threshold on its own (noise coupling, capacitive pickup from neighboring
   traces) and fires spurious `RISING` edges on an interrupt that's supposed
   to mean "the radio has something to report." Every spurious edge called
   `WisBlockLbmTask::notifyFromISR()`, yanking the background event task
   (see "Low power: background task mode" above) out of tickless idle for
   no reason - the task would wake, find nothing to do, and go back to
   sleep, over and over, defeating the entire point of that idle behavior.
   **Fixed** in `WisBlockLbmPort::init()` (`wisblock_lbm_port.cpp`) by
   changing to `pinMode(LORA_DIO1, INPUT_PULLDOWN)`, which holds the line
   deterministically at the SX1262's own idle/no-IRQ level until the radio
   itself genuinely drives it high. Confirmed supported on all three cores
   this library targets (Adafruit nRF52, ESP32 Arduino, arduino-pico).

2. **RF-switch/antenna-power rail (`LORA_ANT_PWR`) never switched off.**
   Each board HAL's `init()` drove this GPIO high once and never touched it
   again - so the RF switch / antenna front-end supply it feeds stayed
   powered for the entire time the SX1262 itself was in SLEEP mode, which
   quietly ate a large part of the current budget SLEEP mode is supposed to
   buy you. **Fixed** by tying `LORA_ANT_PWR` directly to the same
   Awake/Asleep `RadioMode` tracking each HAL already maintains for BUSY's
   sleep-mode behavior (see "Radio HAL: sleep-state-aware wake handling"
   below): the SetSleep opcode (`0x84`) branch in `sx126x_hal_write()` now
   drives it low right after marking the radio asleep, and
   `checkDeviceReady()`'s wake path drives it high again (with a
   `kAntPwrSettleUs` settle delay before trusting the bus) before issuing
   the NSS wake pulse. Because this hooks the same opcode-level tracking
   that's already keyed off every caller regardless of work mode, it
   applies automatically to LBM's own automatic sleeps in LoRaWAN mode and
   to explicit `sleepRadio()` calls in P2P mode - no call-site changes
   needed anywhere else in the library. Also exposed as
   `WisBlockRadioHal::setAntennaPower(bool)` for the rare case you need to
   force it off independent of that flow (e.g. right before an MCU-level
   `sleep()` where you know the radio will be fully reinitialized on wake).
   **TODO**: the `kAntPwrSettleUs` settle delay (1 ms default) is a
   conservative placeholder, not measured against real hardware - tighten
   it with a scope on `LORA_ANT_PWR` vs. the first successful post-wake SPI
   transaction if you need faster wake latency.

3. **`WisBlockLoRaWAN::sleep()` was an unimplemented stub.** The MCU-level
   low-power entry point never actually parked the CPU - `loop()` just spun
   at full speed regardless of what this function was asked to do,
   independent of the two bugs above. **Fixed** with a real per-platform
   implementation (`WisBlockLoRaWAN.cpp`): `waitForEvent()` (Adafruit
   nRF52 core's own low-power WFE primitive) on RAK4631,
   `esp_light_sleep_start()` with GPIO + timer wakeup sources on RAK3312,
   and a `__wfi()`-based wait loop on RAK11310. All three wake on the
   SX1262 DIO1 IRQ or `maxDurationMs` elapsing, whichever comes first - see
   the doc comment on `sleep()` in `WisBlockLoRaWAN.h` and the comments at
   each platform branch in the `.cpp` for specifics and caveats (notably:
   RAK11310 can only reach `__wfi()`-depth savings, not true dormant mode,
   without adding a `pico-extras`-enabled core build - arduino-pico doesn't
   expose dormant mode out of the box). This is unrelated to, and doesn't
   replace, `enableBackgroundTask()`'s tickless-idle-based savings - see
   that method's own doc comment for when you'd reach for one versus the
   other. `sleep()` also doesn't touch the radio itself; pair it with
   `sleepRadio()` in P2P mode if you want the radio asleep too.

## LoRaWAN engine started unconditionally, even in P2P-only builds (fixed)

Follow-up field testing (P2P mode, `sleepRadio()` from the fix above already
applied) still showed an elevated idle baseline - improved, but nowhere near
the SX1262's real SLEEP current. The giveaway was in the boot log: even
though the test sketch only ever calls `setWorkMode(WISBLOCK_MODE_LORA_P2P)`
and P2P APIs, LBM's full LoRaWAN engine initialization ran anyway -
including relay end-device configuration (`ADD_RELAY_RX`/`ADD_RELAY_TX` are
on in this library's example `main.h`) - and logged its own `END DEVICE
RELAY CONFIGURATION` / `Activation: ED_CONTROLED` banner before the sketch's
own `setup()` had gotten past `lora.begin()`.

**Root cause:** `WisBlockLoRaWAN::begin()` called `lorawan.begin()`
unconditionally, for every application, before the sketch ever gets a
chance to call `setWorkMode()` at all. `lorawan.begin()` doesn't just call
`smtc_modem_init()` - it also runs `LoRaWANRelay::configureED()` /
`setRelayMode()`, which arms the LoRaWAN Relay end-device's WOR
(Wake-on-Radio) listening configuration against the physical SX1262. A
P2P-only sketch never wanted any of that, but got it anyway - and
`p2p.sleep()`'s plain SX126x `SetSleep` command has no way to know about or
cancel a WOR schedule LBM's radio planner still considers its own. The two
engines were quietly contending for the same radio the whole time; that
contention, not another HAL bug, is what accounted for the residual
elevated idle current after the `sleepRadio()` fix.

**Fixed** by making LoRaWAN engine startup lazy: `begin()` no longer touches
`lorawan` at all. A new private `ensureLoRaWANEngineStarted()` runs
`lorawan.begin()` exactly once, the first time it's actually needed -
`setWorkMode(WISBLOCK_MODE_LORAWAN)`, any LoRaWAN-specific setter
(`setOTAAKeys()`, `setRegion()`, `setDeviceClass()`, ...), `join()`,
`sendLoRaWAN()`, or the relay/link-check/device-time helpers. A P2P-only
sketch that calls `setWorkMode(WISBLOCK_MODE_LORA_P2P)` and never touches a
LoRaWAN API never starts the LoRaWAN engine at all - no `smtc_modem_init()`,
no relay configuration, nothing left to contend with the P2P engine for the
radio. `applyLoRaWANSettings()` (the shared path `restoreConfig()` /
`factoryReset()` also use for a blanket config resync) is additionally
gated on `config.workMode == WISBLOCK_MODE_LORAWAN`, so loading a
persisted config while running in P2P mode can't re-trigger the same
problem through that side door. Ordinary LoRaWAN sketches see no change in
behavior - `setWorkMode(WISBLOCK_MODE_LORAWAN)` (called right after
`begin()` in every LoRaWAN example this library ships) starts the engine at
essentially the same point `begin()` used to.

## Radio silently re-woken by its own idle IRQ poll (fixed)

One more round of field testing (P2P mode, both fixes above applied,
board/USB power-supply causes ruled out by direct comparison against a
known-good ~50µA P2P sleep figure from another library on the *same*
hardware) still showed idle current roughly 20x higher than that reference
- consistently ~1080µA, with a hard floor that never dipped any lower
anywhere in a two-minute capture, even briefly. That "never dips, ever" detail
was the key clue: a radio that's actually reaching SLEEP and getting
legitimately woken now and then would show up as brief low dips between
wake events. A flat, unmoving floor instead means something was undoing the
sleep essentially immediately, every time.

**Root cause:** `LoRaP2PEngine::handleEvents()` called
`sx126x_get_irq_status()` unconditionally on every invocation - including
the routine "is anything actually pending?" poll that runs every time the
background task wakes for *any* reason. That status read is itself an SPI
transaction, and `checkDeviceReady()` (`wisblock_radio_hal_*.cpp`) treats
*every* SPI transaction as a wake request - antenna power back on, NSS wake
sequence - regardless of why it was issued. So the moment anything gave the
background task's event semaphore during an otherwise-idle stretch (a real
DIO1 edge, or even just a stray one), the very next poll - which only
wanted to check "anything pending?" - silently woke a deliberately-slept
radio back into STANDBY as a side effect, and nothing ever put it back to
sleep afterward, because only an explicit `sleepRadio()` call does that. The
radio was spending almost the entire idle window awake again, immediately
after every sleep.

**Fixed** by adding `WisBlockRadioHal::isAsleep()` (tracks the same
Awake/Asleep state the antenna-power fix above already maintains) and
checking it at the top of `LoRaP2PEngine::handleEvents()`: if the radio is
already known to be asleep, skip the IRQ-status read entirely and return
immediately. This costs nothing when correct - a radio that's genuinely
asleep can't have generated a new IRQ anyway, since real P2P events only
ever arrive on DIO1 while actively listening, transmitting, or running CAD,
none of which happen while asleep - and it eliminates the SPI transaction
that was undoing `sleepRadio()`'s effect as a side effect of routine
polling.

## LoRaWAN RX1/RX2 window missed (fixed)

First real-hardware LoRaWAN join test after the P2P power fixes above: the
device sent a real, correctly-formed Join Request (confirmed on the network
server), then timed out on *both* RX1 and RX2 without receiving the Join
Accept the network had sent - `onJoinFailed()` fired, and a retry hit LBM's
own "device is already joined" guard (a separate, pre-existing point of
confusion explained below), leaving the application stuck thinking it was
never joined.

This one took several rounds of instrumented logging to actually pin down,
worth summarizing because the intermediate theories were each disproven by
data rather than assumed away:

- First suspected the antenna-power settle delay wasn't being budgeted into
  `radio_planner`'s wake-ahead timing at all. Fixed that (folded it into
  `smtc_modem_hal_get_radio_tcxo_startup_delay_ms()`) - same failure,
  unchanged.
- Re-tested with `WISBLOCK_RADIO_HAL_KEEP_ANT_PWR_ALWAYS_ON` to take
  antenna power out of the picture entirely - same failure, proving it was
  never the cause.
- Added tracing of the raw `GetIrqStatus` bytes LBM reads: `02 00` =
  `0x0200` = `IRQ_TIMEOUT`, firing for real, ~65-100ms into windows LBM had
  scheduled to last several seconds.
- Added tracing of the actual `SetRx` timeout parameter sent to the chip:
  a genuine 3000ms, not a short/corrupted value - ruling out a parameter
  bug.
- Added tracing of `SetLoRaSymbNumTimeout` (the SX126x's separate, much
  shorter "give up if not even a preamble shows up within N symbols"
  check that normal LoRaWAN stacks arm ahead of the full RX window as a
  power-saving measure): armed with ~6-8 symbols, a perfectly ordinary
  value - not a misconfiguration either.

That last result was the key one: a correctly-configured, standard
preamble quick-check was firing because the receiver genuinely wasn't
detecting anything - not because the check itself was wrong.

**Root cause:** `smtc_modem_hal_get_radio_tcxo_startup_delay_ms()`
(`wisblock_lbm_port.cpp`) told LBM's `radio_planner` this board's TCXO
needs only 5ms to start up and stabilize. It doesn't - `LoRaP2PEngine.cpp`'s
own `sx126x_set_dio3_as_tcxo_ctrl()` call configures 50ms for the *exact
same physical TCXO* on the same board, a value P2P mode's TX/RX have been
working reliably against the whole time. 5ms was simply the wrong number
for this hardware. `radio_planner` uses this figure to decide how far ahead
of a scheduled window to issue the radio command so the chip's internal
TCXO/PLL ramp-up finishes exactly as the window is meant to open -
under-reporting it doesn't delay when the command is *issued* (it was
issued right on `radio_planner`'s own logged schedule every time) but it
does delay when the radio is actually *ready to receive*, by the
difference between 5ms and however long this TCXO genuinely takes. That
was enough to consistently miss the first several symbols of a downlink
that itself arrived exactly on time, which is exactly what a short,
correctly-armed preamble-detection timeout firing with nothing detected
looks like.

**Fixed** by using the same 50ms figure P2P mode already proved correct:
`50 + antennaPowerSettleMs()` instead of `5 + antennaPowerSettleMs()`.

**Correction:** matching P2P's 50ms exactly turned out to be one step too
far. `smtc_relay_tx_init()` hard-panics ("TCXO delay not compatible with
relay mode") if this value is `>= DELAY_WOR_TO_WORACK_MS` - also 50ms, a
LoRaWAN Relay *protocol* timing budget defined in `wake_on_radio_def.h`,
not a hardware limit. This library's `main.h` has `ADD_RELAY_TX` on, so
that init runs unconditionally at `lorawan.begin()`, before a join is ever
attempted - P2P mode never touches relay code at all, which is why 50ms
was safe there but crashed the LoRaWAN path immediately. Settled on a
deliberately conservative `40 + antennaPowerSettleMs()`, clamped to stay
under the 50ms ceiling regardless of what `antennaPowerSettleMs()` reports
in the future - closer to P2P's proven 50ms than the original wrong 5ms,
without tripping the relay assert. This is still not a measured value; if
RX1/RX2 are marginal after this, it's the next thing to tune with a scope,
and disabling `ADD_RELAY_TX` in `main.h` (if you don't need the relay
feature) removes the 50ms ceiling entirely.

For anyone chasing a similar radio-timing issue on this library: the
`WISBLOCK_RADIO_HAL_DEBUG` build flag (see `wisblock_radio_hal_*.cpp`)
traces sleep/wake transitions, `SetRx`/`SetLoRaSymbNumTimeout` parameters,
and raw `GetIrqStatus` reads with millisecond timestamps - cross-reference
against `MODEM_HAL_DBG_TRACE`'s own output to see exactly what the radio
was told to do versus what it actually reported.

While tracing this, also found and fixed a related but separate,
currently-latent bug: `LoRaWANEngine::applySettings()` only ever pushed
device class/ADR/relay settings to LBM - region and OTAA credentials were
pushed exactly once, from `begin()`, and never again. That was harmless as
long as `begin()` ran *after* `setRegion()`/`setOTAAKeys()` had already
populated the settings it read, which used to be guaranteed back when
`WisBlockLoRaWAN::begin()` started the LoRaWAN engine eagerly. Lazy engine
startup (see the "LoRaWAN engine started unconditionally" fix above) means
`begin()` now legitimately runs as early as `setWorkMode(WISBLOCK_MODE_LORAWAN)`
- before the application has called `setOTAAKeys()`/`setRegion()` at all,
which is the order every example this library ships actually uses. On a
device with a previously-saved config already on flash this goes
unnoticed (the reloaded values already match what the sketch sets anyway,
which is exactly why it didn't explain *this* particular failure - the
Join Request in the log used the correct DevEUI/JoinEUI). On a genuinely
first boot, or right after `factoryReset()`, it would have pushed a blank
region and all-zero keys with no later chance to correct them. `begin()`
now delegates to `applySettings()`, and `applySettings()` pushes region and
OTAA credentials every time, closing that gap regardless of call order.

## First send after join lost (fixed, in the example sketches)

With the join fix above confirmed working on real hardware, the next real
log capture showed something subtler: the periodic-uplink timer firing,
`sendLoRaWAN()` returning `true`, LBM logging `add send task` - and then
nothing. No `Tx LoRa`, no `TX DONE`, no `onLoRaWANTxFinished()` callback.
The *next* scheduled send, 30 seconds later, went out fine with `fcnt_up=0`
- confirming the first one never actually consumed a frame counter, i.e.
never really existed as far as the network was concerned.

The log's own timestamps gave it away: `[Loop] Wakeup Cause 0000000000000001`
(the `STATUS` bit) printed twice at the *identical* millisecond, with a
`sendLoRaWAN()` call in between - meaning the periodic timer's callback
fired twice almost simultaneously for what should have been one 30-second
tick. Two `sendLoRaWAN()` calls that close together race for LBM's single
pending-uplink slot; the second silently replaces the first, and both
calls return `true` since queuing itself succeeds each time - `sendLoRaWAN()`
was never lying, it just doesn't (and can't) promise eventual transmission,
only successful queuing. That contract is now spelled out on its
declaration in `WisBlockLoRaWAN.h`.

**Root cause:** both `examples/LowPowerLoRaWAN/LowPowerLoRaWAN.ino` and
`examples/LowPowerLoRaP2P/LowPowerLoRaP2P.ino` called
`xTimerChangePeriod()` immediately followed by `xTimerStart()` on a timer
that had just been created via `xTimerCreate()` - and was therefore still
dormant. `xTimerChangePeriod()`'s own documented behavior is to *start* a
dormant timer as a side effect of changing its period; the change itself
was a no-op regardless, since the timer was already created with exactly
that period. So every timer start point in both examples was actually two
redundant, closely-spaced start requests racing each other, capable of
producing two expiry notifications close together instead of one.

**Fixed** by deleting the redundant `xTimerChangePeriod()` calls - a single
`xTimerStart()` (or `xTimerStartFromISR()`) on the already-correctly-
configured dormant timer is the entire fix, in both examples.

**Follow-up, after that fix was confirmed on real hardware:** the exact
same symptom - a queued send that never transmits, silently replaced by
the next one, wasting a frame counter - kept recurring even with the timer
fix applied and no duplicate `Wakeup Cause` lines left in the log. This
time the cause was more fundamental than a sketch bug: LBM was reporting
it accurately the whole time. `SMTC_MODEM_EVENT_TXDONE` fires with
`status == SMTC_MODEM_EVENT_TXDONE_NOT_SENT` whenever a queued uplink gets
superseded before being dispatched - exactly the "TX FAILED, airtime 0 ms"
lines visible in the log, right after a *new* `add send task` landed while
the *previous* uplink's full Class A cycle (TX + RX1 + RX2, sometimes
stretched by post-join MAC command negotiation - one capture showed a
26-second gap between a queued send and its actual transmission) was still
in progress. `LoRaWANEngine::send()` had no notion of "an uplink is
already in flight" and would happily call `smtc_modem_request_uplink()`
again regardless, and LBM - which only ever holds one pending uplink -
just discarded whichever one was still waiting. Nothing was mis-reporting
anything; the library just had no guard against causing the collision in
the first place.

**Fixed** with a new `uplinkPending` flag on `LoRaWANEngine`: set the
moment `send()` successfully queues a request, cleared unconditionally the
moment `SMTC_MODEM_EVENT_TXDONE` fires (success or not). `send()` now
refuses - returns `false` immediately, no LBM call made, no frame counter
spent - if a previous send is still in flight, instead of silently queuing
a replacement. A fixed-interval periodic sender (like both example
sketches) will now sometimes see `sendLoRaWAN()` return `false` if its
interval is shorter than a real-world Class A cycle - that's the correct,
actionable signal to skip that cycle and wait for the next one, rather
than the previous silent frame-counter waste.

## Queued send not dispatched promptly (fixed)

Even with the collision guard above confirmed working (a refused second
send, no wasted frame counter that time), the underlying complaint turned
out to be real and separate: a log capture showed `add send task`
succeeding, then over 30 seconds of complete silence - no radio activity
at all - before the actual `Tx LoRa` finally appeared, with no new queue
request in between. The original uplink had simply been sitting there
untouched the entire time.

**Root cause:** `smtc_modem_request_uplink()` (called from
`LoRaWANEngine::send()`) only enqueues a request into LBM's own internal
task queue - it doesn't itself cause `smtc_modem_run_engine()` to run
again, and critically, `sendLoRaWAN()`/`join()` are normally called from
the *application's own task* (e.g. `loop()`), not from
`WisBlockLbmTask`'s background event task. If that background task was
already asleep waiting out its own previously-computed deadline (bounded
by `kMaxWaitMs`, up to a full minute) when the request was queued, nothing
told it "wake up now, there's new work" - it just sat there until its old
deadline eventually elapsed on its own, which is exactly the kind of
delay the log captured.

**Fixed** with a new `WisBlockLbmTask::notify()` - the non-ISR counterpart
to the existing `notifyFromISR()` - called from `WisBlockLoRaWAN::join()`
and `sendLoRaWAN()` immediately after successfully queuing something with
LBM, so the background task wakes and calls `smtc_modem_run_engine()`
again promptly instead of waiting out a stale deadline.

## P2P idle current: warm-start vs. cold-start SLEEP (tested, not the primary cause)

Back to the P2P power investigation, with the LoRaWAN join/send bugs now
resolved. Recap of where this stood: antenna-power cutting and the
`isAsleep()` HAL fix both landed real, measurable improvements, but a
direct A/B test with `WISBLOCK_RADIO_HAL_KEEP_ANT_PWR_ALWAYS_ON` proved
antenna power was no longer the dominant factor - idle current stayed
essentially flat (~1080-1100uA) whether the antenna rail was cut or not,
across four separate captures (V3 through V6). A reference implementation
(SX126x-Arduino) on the *same* physical hardware reaches ~50uA with RX
disabled - roughly 20x lower - so something real was still costing ~1mA
that a working implementation on this exact board doesn't pay.

The one remaining variable never tested: `LoRaP2PEngine::sleep()` used
`SX126X_SLEEP_CFG_WARM_START` - retains the radio's configuration across
sleep so the next operation can wake and go immediately, at the cost of
needing an internal regulator to stay active for that retention through
the whole sleep. `SX126X_SLEEP_CFG_COLD_START` drops that regulator
entirely, at the cost of losing all configuration - meaning every wake now
needs a full reconfigure (frequency, modulation/packet params, PA config,
TX params, DIO2/DIO3 control) before the radio can do anything.

**Changed** `sleep()` to cold-start, and added
`LoRaP2PEngine::reconfigureAfterColdSleep()` - re-running everything
`begin()` does except the hardware reset pin toggle (not needed;
`checkDeviceReady()`'s wake sequence already restores SPI/BUSY) - called
unconditionally at the top of `send()`/`startReceive()`/`startCad()`, a
no-op unless a cold sleep actually happened since the last wake.

**Result: real, but not the ~1mA floor itself.** Captures V7 through V9
(after further HAL-level and FreeRTOS-level fixes, documented below) kept
landing at the same ~700-1100uA floor with cold-start already active,
meaning warm-start's retention regulator was never the dominant remaining
cost - it's a legitimate improvement to keep (lower current during actual
sleep, worth having regardless), just not the answer to where the rest of
the ~1mA was going. That turned out to be the SPI peripheral itself - see
the next section.

## Persistent idle current floor (fixed - SPI peripheral was never disabled)

After every other lever - antenna power, radio SLEEP-command correctness,
`LoRaP2PEngine::handleEvents()` not re-waking a sleeping radio to poll
routine IRQ status, and the Adafruit nRF52 core's idle-hook/tickless-idle
interaction - was tested and confirmed to genuinely change behavior, one
last stubborn floor remained: idle current sat at ~700-1100µA and hadn't
moved regardless of which of the above was tried, identically in both P2P
and LoRaWAN mode despite those two using completely different sleep-
triggering code paths. That convergence was itself informative - whatever
was left wasn't specific to either radio engine.

**Root cause, confirmed:** `SPI.begin()` is called once in `init()` and
nothing ever called `SPI.end()` afterward - `beginTransaction()`/
`endTransaction()` only arbitrate bus access for a single transaction,
they don't power the underlying SPI peripheral down between transactions.
A real report of exactly this pattern - a UART peripheral on the same
nRF52 chip family drawing meaningful current simply from being left
*enabled*, independent of whether it's actively transmitting - was the
basis for testing whether the same applies to SPIM here. It did: a real
2-minute P2P capture with the fix in place shows idle current at ~50µA
(63.8% of all post-boot samples under 50µA, minimum recorded 3.12µA) -
matching the ~50µA reference figure from an independent library on the
same hardware that originally proved this level was achievable at all.

**Fixed** with `SPI.end()` in the SetSleep branch of each
`wisblock_radio_hal_<board>.cpp`, and `SPI.begin()` (with pins re-applied)
in `checkDeviceReady()`'s wake path - gated behind
`WISBLOCK_RADIO_HAL_KEEP_SPI_ALWAYS_ON` if you ever need to A/B test
against the old always-enabled behavior.

The ~15s RX window itself still draws ~9.3mA while actively listening -
expected, since the SX1262's receiver has to be genuinely active to
detect anything, and not something further sleep/peripheral management
can reduce. If that number itself needs to come down, the next levers are
RX window duration and whether boosted LNA gain is actually needed for
your link budget - a separate, non-idle-current question.

## P2P: RX boosted gain toggle and hardware RX duty-cycling

Follow-up to the idle-current fix above, once the ~15s RX window's own
~9mA became the next visible cost worth looking at: two SX1262 features
this library wasn't exposing yet.

**RX boosted gain** (`sx126x_cfg_rx_boosted()`) trades RX current for
sensitivity - roughly 4-5mA extra for a few dB, matching the gap between
this library's ~9mA RX current and a comparison library's ~4-5mA on the
same hardware with boost off. Nothing previously configured this register
explicitly either way, so the SX1262 was running on its power-on default,
which is boosted-on. Now exposed as `WisBlockLoRaWAN::setP2PRxBoostedGain(bool)`
/ `AT+RXBOOST=<0/1>`, defaulting to the previous (boosted-on) behavior so
existing applications see no change unless they opt out. Applied in
`LoRaP2PEngine::applyRadioParams()`, which - per the driver's own doc
comment on `sx126x_cfg_rx_boosted()` ("not kept in retention memory -
shall be enabled each time the chip leaves sleep mode") - already runs on
every path that matters for that: `begin()`, `applySettings()`, and
`reconfigureAfterColdSleep()` (called before every CAD/RX/TX after this
engine's cold-start sleep wipes the chip's config - see the section
above).

**RX duty-cycling** (`sx126x_set_rx_duty_cycle()`) is a different
mechanism from anything this library exposed before: the chip alternates
RX/sleep phases entirely on its own, autonomously, without the MCU waking
it for each cycle - genuinely lower average current than an MCU-driven
"wake, listen, sleep, repeat" loop for the same effective duty cycle,
since the chip handles the alternation itself instead of paying
`checkDeviceReady()`'s wake cost (antenna power settle, SPI, BUSY wait)
on every cycle. If a preamble is detected during an RX phase, the chip
extends reception to receive the full packet regardless of the configured
RX phase duration, then reports RX_DONE/CRC_ERROR/etc. through the exact
same IRQ routing and event dispatch as `startReceive()` - this only
changes radio behavior *between* packets, not how a received packet is
handled. New API: `WisBlockLoRaWAN::startP2PReceiveDutyCycle(rxTimeMs, sleepTimeMs)`
/ `AT+PRECVDC=<rxTimeMs>:<sleepTimeMs>`. Deliberately *not* routed through
`checkDeviceReady()`'s sleep-state tracking (the chip's autonomous sleep
phases are invisible to and untouched by that bookkeeping) - `LORA_ANT_PWR`
correctly stays powered for the whole sequence, since the chip needs it
during every RX phase and our software has no visibility into their
timing to toggle it in between.

**Choosing rxTimeMs/sleepTimeMs isn't arbitrary** - they're constrained by
the transmitting side's actual over-the-air preamble duration: their sum
must stay under it (with margin), or a packet's entire preamble can pass
while this radio is asleep and never get caught. Rather than have every
application reimplement that formula (and risk it drifting out of sync
with whatever bandwidth/SF/preamble is actually configured), added
`LoRaP2PEngine::computeRxDutyCycleTiming()` /
`WisBlockLoRaWAN::computeP2PRxDutyCycleTiming()`, which reads the current
settings via the new `getSettings()`/`getP2PSettings()` getters and
computes safe values directly - or use `AT+PRECVDC=AUTO` to compute and
start in one step. Returns `false` if the configured preamble is too
short to fit any usable window at all, in which case increase the
transmitter's preamble length first.

**Correction, confirmed on real hardware:** the first version of this
assumed the transmitting node's preamble length matched this radio's own
`settings.preambleLength`, and fell back to that when nothing else was
available. That assumption doesn't hold in general - a receiver's own
configured preamble length has no bearing on what it can detect (only the
*transmitter's* actual over-the-air preamble length does), and two
devices can complete a link perfectly normally with completely different
preamble settings on each side, confirmed with a working link between a
sender at 200 symbols and a receiver still configured at the default 8.
Both `computeRxDutyCycleTiming()` and `computeP2PRxDutyCycleTiming()` now
have a second overload taking an explicit `txPreambleLengthSymbols`
parameter - prefer it whenever the transmitter's real value is known,
which is effectively always. `AT+PRECVDC=AUTO:<txPreambleLengthSymbols>`
is the AT-command equivalent; plain `AT+PRECVDC=AUTO` (no explicit value)
remains as a fallback that reads this radio's own configured preamble
length, for cases where nothing better is available - understand that
it's a weaker stand-in, not the primary way to use this.

## Radio HAL: sleep-state-aware wake handling

`wisblock_radio_hal_*.cpp`'s `sx126x_hal_write()`/`read()`/`wakeup()` track
an explicit `RadioMode` (Awake/Asleep) state, modeled directly on Semtech's
own reference `sx126x_hal.c` (in `lbm_applications/2_porting_nrf_52840/` of
the upstream SWL2001 repo). This matters because **BUSY reads HIGH
throughout SX126x sleep mode** - it does not behave like a normal
"processing a command" busy signal while asleep, and only clears once you
pull NSS low to initiate wake, which is a different sequence from a normal
transaction's busy-wait.

This library never explicitly sleeps the radio itself, but LBM's own
`radio_planner` does, autonomously, between scheduled tasks
(`ral_set_sleep()` in `radio_planner.c`) - so the HAL has to track this
regardless of what higher-level code does. Getting this wrong looks exactly
like a hung/dead radio: `BUSY` never clears, every subsequent SPI
transaction times out, and LBM panics with something like
`ralf_setup_lora(...) == RAL_STATUS_OK` failing during the very first
LoRaWAN join attempt - while LoRa P2P mode (which never sleeps the radio)
works completely fine, making the failure look P2P-vs-LoRaWAN-specific when
it's actually about sleep/wake state tracking that P2P mode never exercises.

## Building with PlatformIO

If you're using PlatformIO rather than Arduino IDE: this library ships a
`library.json` + `extra_script.py` that explicitly add every nested
`src/lbm/...` subdirectory to the compiler's include path. This is
necessary because PlatformIO's Library Dependency Finder does not reliably
discover deeply-nested header directories the way Arduino IDE's recursive
`src/` scanning does - without it, you'll see `fatal error: some_header.h:
No such file or directory` for headers that genuinely exist on disk, just
not on the include search path PlatformIO computed.

If you still hit this after updating, try adding to your `platformio.ini`:

```ini
[env:your_env]
lib_ldf_mode = deep+
```

...and do a clean rebuild (`pio run -t clean`), since PlatformIO caches LDF
results per environment.

If you re-vendor a different LBM version and the directory layout changes,
regenerate the include list in `extra_script.py` with (run from the library
root):

```bash
find src/lbm -name "*.h" -exec dirname {} \; | sort -u
```

## Directory layout

```
WisBlockLoRaWAN/
├── library.properties
├── src/
│   ├── WisBlockLoRaWAN.h/.cpp        // Top-level facade class (the public API)
│   ├── WisBlockLoRaWANTypes.h        // Enums / structs shared by API + AT layer
│   ├── WisBlockLoRaWANConfig.h/.cpp  // Persisted settings, load/save/defaults
│   ├── WisBlockLoRaFlash.h/.cpp      // Per-MCU flash/NVS backend (#if defined per board)
│   ├── WisBlockLoRaBoards.h          // Pin maps for RAK4631 / RAK3312 / RAK11310
│   ├── WisBlockLoRaAT.h/.cpp         // AT command parser/dispatcher
│   ├── LoRaWANEngine.h/.cpp          // Wraps smtc_modem_api: join/send/class/ADR
│   ├── LoRaWANRelay.h/.cpp           // Relay TX/RX config wrapper
│   ├── LoRaP2PEngine.h/.cpp          // Direct radio P2P TX/RX/CAD
│   ├── wisblock_lbm_port.h/.cpp      // smtc_modem_hal_* implementations
│   ├── wisblock_lbm_task.h/.cpp      // optional FreeRTOS background task (see "Low power" above)
│   ├── wisblock_ral_sx126x_bsp.c     // ral_sx126x_bsp_* implementations (LoRaWAN-mode radio config)
│   ├── wisblock_radio_hal.h/.cpp     // SPI/GPIO glue to SX1262 (TODO)
│   └── lbm/                          // <- vendor Semtech LoRaBasicsModem here
├── examples/
│   ├── BasicLoRaWAN/BasicLoRaWAN.ino
│   ├── BasicLoRaP2P/BasicLoRaP2P.ino
│   └── ATCommandInterface/ATCommandInterface.ino
└── README.md
```

## AT command set (implemented in `WisBlockLoRaAT.cpp`)

| Command                          | Description                                      |
|-----------------------------------|---------------------------------------------------|
| `AT+MODE=<0/1>`                  | 0 = LoRaWAN, 1 = LoRa P2P                          |
| `AT+MODE=?`                      | Query current mode                                 |
| `AT+DEVEUI=<hex8>`               | Set Device EUI                                     |
| `AT+APPEUI=<hex8>` / `AT+JOINEUI`| Set Join EUI                                       |
| `AT+APPKEY=<hex16>`              | Set App/Network key (OTAA)                         |
| `AT+DEVADDR=<hex4>`              | Set Device Address (ABP)                           |
| `AT+NWKSKEY=<hex16>`             | Set Network Session Key (ABP)                      |
| `AT+APPSKEY=<hex16>`             | Set App Session Key (ABP)                          |
| `AT+REGION=<0..13>`              | EU868, US915, AU915, AS923, KR920, IN865, RU864... |
| `AT+DR=<0..15>`                  | Data rate index                                    |
| `AT+CLASS=<A/B/C>`               | Device class                                       |
| `AT+JOINMODE=<0/1>`              | 0 = OTAA, 1 = ABP                                  |
| `AT+JOIN`                        | Start join procedure                               |
| `AT+ADR=<0/1>`                   | ADR on/off                                         |
| `AT+TXP=<0..15>`                 | TX power index                                     |
| `AT+RELAY=<0/1/2>`               | 0 = off, 1 = relay TX (end-device), 2 = relay RX (serving) |
| `AT+RELAYED=<activation>:<smartLevel>:<backoff>:<missedWorAckToNoSync>:<2ndChEnable>:<2ndChFreqHz>:<2ndChAckFreqHz>:<2ndChDr>` | Configure relay TX (end-device role); persisted, re-applied on AT+RELAY=1 |
| `AT+RELAYSRV=<cadPeriod>:<freqHz>:<ackFreqHz>:<dr>:<errorPpm>:<cadToRxSymb>` | Configure relay RX (serving role); persisted, re-applied on AT+RELAY=2. Requires LBM built with `ADD_RELAY_RX` |
| `AT+RELAYDEV=<idx>:<devAddr8hex>:<rootWorSKey32hex>:<unlimited0/1>:<bucketFactor>:<reloadRate>` | Register a trusted end-device (0-15) with the serving relay - **required** before it forwards anything for that device; not persisted |
| `AT+RELAYDEVDEL=<idx>`            | Remove a trusted end-device from the serving relay's list |
| `AT+SEND=<port>:<hex payload>`   | Send LoRaWAN uplink                                |
| `AT+CFM=<0/1>`                   | Confirmed/unconfirmed uplinks                      |
| `AT+LINKCHECK`                   | Request a link check                               |
| `AT+LINKCHECK=?`                 | Query the most recently answered link check's margin (dB) and gateway count, without sending a new request |
| `AT+TIMEREQ`                     | Request network time (DeviceTimeReq)               |
| `AT+P2P=<freq>:<sf>:<bw>:<cr>:<preamble>:<txpower>` | Set LoRa P2P radio params        |
| `AT+CAD=<0/1>`                   | Enable/disable CAD before P2P TX                   |
| `AT+RXBOOST=<0/1>`               | Enable/disable RX boosted gain (extra ~4-5mA RX current for a few dB sensitivity) |
| `AT+PSEND=<hex payload>`         | Send a LoRa P2P packet                             |
| `AT+PRECV=<0/timeout_ms>`        | Put radio into RX (0 = continuous)                 |
| `AT+PRECVDC=<rxTimeMs>:<sleepTimeMs>` | Put radio into SX1262 hardware RX duty-cycling (chip alternates RX/sleep on its own) |
| `AT+PRECVDC=AUTO`                | Same, computed automatically from the currently configured bandwidth/SF/preamble length |
| `AT+PRECVDC=AUTO:<txPreambleLengthSymbols>` | Same, computed against a given transmitter preamble length instead of this radio's own - prefer this form |
| `AT+LOWPOWER=<0/1>`              | Enable/disable low power (DIO1 wake) mode           |
| `AT+SAVE`                        | Persist current config to flash                    |
| `AT+RESTORE`                     | Reload config from flash                           |
| `AT+FACTORY`                     | Reset config to factory defaults                   |
| `AT+STATUS`                      | Dump current config + join/link status             |

Every setter here maps 1:1 to a public C++ API call, so the AT layer is just
a thin serializer over `WisBlockLoRaWAN` — you never have two sources of
truth for a setting.

## C++ API sketch

```cpp
#include <WisBlockLoRaWAN.h>

WisBlockLoRaWAN lora;

void setup() {
  lora.begin();                       // loads saved config, inits radio
  lora.setWorkMode(WISBLOCK_MODE_LORAWAN);

  lora.setOTAAKeys(devEui, joinEui, appKey);
  lora.setRegion(WISBLOCK_REGION_EU868);
  lora.setDeviceClass(WISBLOCK_CLASS_A);
  lora.setADR(true);
  lora.setTxPower(0);

  lora.onJoinSuccess(onJoined);
  lora.onJoinFailed(onJoinFail);
  lora.onTxFinished(onTxDone);
  lora.onRxFinished(onRxDone);
  lora.onTimeRequestAnswer(onTimeAns);
  lora.onLinkCheckAnswer(onLinkCheck);

  lora.join();
}

void loop() {
  lora.handleEvents();   // pump the LBM state machine — call every loop()
}
```

See `src/WisBlockLoRaWAN.h` for the full API and callback signatures, and the
`examples/` folder for both LoRaWAN and LoRa P2P end-to-end sketches.
