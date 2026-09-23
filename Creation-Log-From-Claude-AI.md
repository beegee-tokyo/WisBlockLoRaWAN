# WisBlockLoRaWAN

Arduino library skeleton for **LoRaWAN (Class A/B/C)** and **LoRa P2P** on
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

## RUI3 AT command compatibility

Renamed four commands and changed one command's behavior to match
[RUI3's AT command manual](https://docs.rakwireless.com/product-categories/software-apis-and-libraries/rui3/at-command-manual),
so existing RUI3 tooling/scripts work against this library unmodified for
the commands both support. This is a breaking change from this library's
own prior naming (`AT+MODE`, `AT+REGION`, `AT+JOINMODE`, `AT+JOIN=?` no
longer exist as command names) - deliberate, not an oversight, since
keeping both names with *different value conventions* for the same
setting would be a worse trap than a clean break: a script still using the
old `AT+MODE=1` for P2P would now silently mean LoRaWAN instead if both
names coexisted, because the values are inverted between this library's
original convention and RUI3's.

- **`AT+MODE` → `AT+NWM`**: values are inverted, not just renamed - this
  library's original `AT+MODE` was `0 = LoRaWAN, 1 = P2P`; RUI3's `AT+NWM`
  is `0 = P2P_LORA, 1 = LoRaWAN, 2 = P2P_FSK`. `2` (P2P_FSK) is rejected
  with an error - this library has no FSK P2P mode implemented at all, not
  just under this command.
- **`AT+REGION` → `AT+BAND`**: RUI3's region numbering doesn't match this
  library's own `WisBlockRegion` enum values or order at all - built a
  translation table (`bandIndexToRegion()`/`regionToBandIndex()` in
  `WisBlockLoRaAT.cpp`) rather than just renaming the command. Two RUI3
  band indices have no equivalent here and are rejected with an error:
  `0` (EU433) and `12` (LA915) - this vendored LBM's `main.h` doesn't
  define `REGION_EU_433` or `REGION_LA_915`, so those region tables were
  never compiled in; picking a fallback region instead of erroring would
  have been worse. Two `WisBlockRegion` values likewise have no RUI3 band
  index at all (`WISBLOCK_REGION_CN470_RP_1_0`, `WISBLOCK_REGION_WW2G4`) -
  library-specific regions beyond RUI3's set, simply not reachable via
  `AT+BAND` (`AT+BAND=?` errors if the currently active region is one of
  these, rather than printing a made-up index).
- **`AT+JOINMODE` → `AT+NJM`**: values are inverted here too - this
  library's original was `0 = OTAA, 1 = ABP`; RUI3's `AT+NJM` is
  `0 = ABP, 1 = OTAA`.
- **`AT+JOIN=?` → `AT+NJS=?`**: this library's `AT+JOIN=?` used to print
  the raw `WisBlockJoinState` enum (`IDLE`/`IN_PROGRESS`/`SUCCEEDED`/
  `FAILED`, i.e. 0-3). RUI3's `AT+NJS=?` is a plain joined/not-joined
  boolean - now backed by `isJoined()`, not a cast of the richer enum.
- **`AT+LINKCHECK`**: not just a value change - RUI3's version is a
  persistent *mode* (`0` disabled, `1` request once, `2` request
  automatically on every uplink from now on), not the one-shot
  "trigger now" action plus "poll the last result" getter this library had
  before. Modes `1`/`2` needed genuine new behavior, not a rename: added
  `LoRaWANEngine::setLinkCheckMode()`, checked inside `send()` (the single
  choke point every uplink goes through, regardless of whether it was
  triggered via the AT layer or a direct `sendLoRaWAN()` call) - mode `1`
  piggybacks a link check request onto the very next uplink and then
  reverts itself to `0`; mode `2` does the same on every uplink
  indefinitely, until explicitly turned off. `AT+LINKCHECK=?` now reports
  the mode, not the last check's result - the result itself still arrives
  asynchronously via `onLinkCheckAnswer()` either way (RUI3 doesn't have a
  poll-style result getter either - it's event-driven there too).
  `getLinkCheckResult()` is still available at the C++ layer for
  applications that want to poll rather than use the callback; it's just
  no longer surfaced under the `AT+LINKCHECK` name.

Everything else keeps its existing name - `AT+DEVEUI`, `AT+APPEUI`/
`AT+JOINEUI`, `AT+APPKEY`, `AT+DEVADDR`, `AT+NWKSKEY`, `AT+APPSKEY`,
`AT+DR`, `AT+CLASS`, `AT+ADR`, `AT+TXP`, `AT+CFM`, `AT+SEND`, `AT+TIMEREQ`
already matched RUI3's naming with no changes needed. RUI3 commands this
library has no equivalent functionality for at all (BLE config, USB mode,
sensor/interface commands, etc.) are simply absent, not stubbed - request
any specific ones if you need them.

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

**Second correction, from real hardware testing at BW125/SF7/marginSymbols=5,
transmitter preamble 100 symbols:** the formula's own textbook assumptions
weren't sufficient either. Two further empirical adjustments, confirmed by
testing every combination against actual packet reception:

- `rxMs` was a fixed "~2 symbols" regardless of `marginSymbols` - not
  reliable enough in practice. Changed to scale directly with
  `marginSymbols` (`rxMs = tSymMs * marginSymbols`), so a caller who
  increases the margin for reliability gets a correspondingly larger RX
  window, not just a larger subtraction from the sleep budget.
- Even with that fix, rounding `rxTimeMs` to the nearest millisecond
  (`+ 0.5`) still missed packets. Needed a further `1.5x` multiplier
  (`rxTimeMs = (uint32_t)(rxMs * 1.5)`) to reliably catch everything in
  testing.

Both are baked into `computeRxDutyCycleTiming()`'s current implementation
and the default `marginSymbols` was raised from 2 to 5 to match. Treat
this as the current best-known-working configuration from one round of
hardware validation, not a fully-characterized formula - if you tune
further, these are the two lines to start from
(`LoRaP2PEngine.cpp`, `computeRxDutyCycleTiming()`).

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

Commands are case-insensitive (`AT+MODE=1` and `at+mode=1` are equivalent) -
the parser uppercases the whole line before parsing. Any non-AT-prefixed
data on the same serial stream (see `unhandledDataCb`) is passed through to
the application byte-for-byte, untouched by this.

Every setter that persists a value also has a `=?` getter to read it back,
except one-shot actions with nothing to read back (`AT+SEND`, `AT+PSEND`,
`AT+PRECV`, `AT+PRECVDC`, `AT+RELAYDEVDEL`) and `AT+RELAYDEV`, whose stored
values aren't kept anywhere readable on this side (see its own row below).

Command names and value conventions follow
[RUI3's AT command manual](https://docs.rakwireless.com/product-categories/software-apis-and-libraries/rui3/at-command-manual)
wherever this library implements the equivalent functionality, so existing
RUI3 tooling/scripts for `AT+NWM`/`AT+BAND`/`AT+NJM`/`AT+NJS`/`AT+LINKCHECK`
work unmodified against this library too. RUI3 commands this library
doesn't implement anything equivalent to are simply absent - not aliased,
not stubbed - see the "RUI3 AT command compatibility" note further below
for exactly what changed, why, and where the two diverge (`AT+BAND`'s
region numbering doesn't fully overlap what this LBM vendoring supports;
`AT+LINKCHECK` gained real automatic-trigger-on-uplink behavior, not just
a rename).

| Command                          | Description                                      |
|-----------------------------------|---------------------------------------------------|
| `AT+NWM=<0/1/2>` / `AT+NWM=?`    | 0 = P2P_LORA, 1 = LoRaWAN, 2 = P2P_FSK (not supported - this library has no FSK P2P mode) |
| `AT+DEVEUI=<hex8>` / `AT+DEVEUI=?` | Device EUI                                       |
| `AT+APPEUI=<hex8>` / `AT+JOINEUI=<hex8>` / `AT+APPEUI=?` / `AT+JOINEUI=?` | Join EUI |
| `AT+APPKEY=<hex16>` / `AT+APPKEY=?` | App/Network key (OTAA). **Getter returns `SET`/`UNSET` only, never the key itself** - see the security note below |
| `AT+DEVADDR=<hex4>` / `AT+DEVADDR=?` | Device Address (ABP)                          |
| `AT+NWKSKEY=<hex16>` / `AT+NWKSKEY=?` | Network Session Key (ABP). **Getter returns `SET`/`UNSET` only** |
| `AT+APPSKEY=<hex16>` / `AT+APPSKEY=?` | App Session Key (ABP). **Getter returns `SET`/`UNSET` only** |
| `AT+BAND=<0..12>` / `AT+BAND=?`  | RUI3 numbering: 0 EU433 (unsupported), 1 CN470, 2 RU864, 3 IN865, 4 EU868, 5 US915, 6 AU915, 7 KR920, 8 AS923-1, 9 AS923-2, 10 AS923-3, 11 AS923-4, 12 LA915 (unsupported) |
| `AT+DR=<0..15>` / `AT+DR=?`      | Data rate index                                    |
| `AT+CLASS=<A/B/C>` / `AT+CLASS=?` | Device class                                      |
| `AT+NJM=<0/1>` / `AT+NJM=?`      | 0 = ABP, 1 = OTAA                                  |
| `AT+JOIN`                        | Start join procedure                               |
| `AT+NJS=?`                       | 0 = not joined, 1 = joined                         |
| `AT+ADR=<0/1>` / `AT+ADR=?`      | ADR on/off                                         |
| `AT+TXP=<0..15>` / `AT+TXP=?`    | TX power index                                     |
| `AT+RELAY=<0/1/2>` / `AT+RELAY=?` | 0 = off, 1 = relay TX (end-device), 2 = relay RX (serving) |
| `AT+RELAYED=<activation>:<smartLevel>:<backoff>:<missedWorAckToNoSync>:<2ndChEnable>:<2ndChFreqHz>:<2ndChAckFreqHz>:<2ndChDr>` / `AT+RELAYED=?` | Relay TX (end-device role) config; persisted, re-applied on AT+RELAY=1 |
| `AT+RELAYSRV=<cadPeriod>:<freqHz>:<ackFreqHz>:<dr>:<errorPpm>:<cadToRxSymb>` / `AT+RELAYSRV=?` | Relay RX (serving role) config; persisted, re-applied on AT+RELAY=2. Requires LBM built with `ADD_RELAY_RX` |
| `AT+RELAYDEV=<idx>:<devAddr8hex>:<rootWorSKey32hex>:<unlimited0/1>:<bucketFactor>:<reloadRate>` | Register a trusted end-device (0-15) with the serving relay - **required** before it forwards anything for that device; not persisted |
| `AT+RELAYDEV=?`                  | Returns an error - no local copy of the registered device list is kept to read back (and it contains a session key that shouldn't be echoed in plaintext regardless) |
| `AT+RELAYDEVDEL=<idx>`            | Remove a trusted end-device from the serving relay's list |
| `AT+SEND=<port>:<hex payload>`   | Send LoRaWAN uplink                                |
| `AT+CFM=<0/1>` / `AT+CFM=?`      | Confirmed/unconfirmed uplinks                      |
| `AT+LINKCHECK=<0/1/2>` / `AT+LINKCHECK=?` | 0 = disabled, 1 = request once (on the next uplink), 2 = request automatically on every uplink |
| `AT+TIMEREQ`                     | Request network time (DeviceTimeReq)               |
| `AT+P2P=<freq>:<sf>:<bw>:<cr>:<preamble>:<txpower>` / `AT+P2P=?` | LoRa P2P radio params      |
| `AT+CAD=<0/1>` / `AT+CAD=?`      | Enable/disable CAD before P2P TX                   |
| `AT+RXBOOST=<0/1>` / `AT+RXBOOST=?` | Enable/disable RX boosted gain (extra ~4-5mA RX current for a few dB sensitivity) |
| `AT+PSEND=<hex payload>`         | Send a LoRa P2P packet                             |
| `AT+PRECV=<0/timeout_ms>`        | Put radio into RX (0 = continuous)                 |
| `AT+PRECVDC=<rxTimeMs>:<sleepTimeMs>` | Put radio into SX1262 hardware RX duty-cycling (chip alternates RX/sleep on its own) |
| `AT+PRECVDC=AUTO`                | Same, computed automatically from the currently configured bandwidth/SF/preamble length |
| `AT+PRECVDC=AUTO:<txPreambleLengthSymbols>` | Same, computed against a given transmitter preamble length instead of this radio's own - prefer this form |
| `AT+LOWPOWER=<0/1>` / `AT+LOWPOWER=?` | Enable/disable low power (DIO1 wake) mode      |
| `AT+SAVE`                        | Persist current config to flash                    |
| `AT+RESTORE`                     | Reload config from flash                           |
| `AT+FACTORY`                     | Reset config to factory defaults                   |
| `AT+STATUS`                      | Dump current config + join/link status             |

**Why key material getters return `SET`/`UNSET` instead of the actual
key:** an AT interface that echoes secret key material back over serial -
often USB, sometimes a UART with no physical security at all - is exactly
the kind of thing a real product's AT command set normally guards against.
`AT+DEVEUI=?`/`AT+APPEUI=?`/`AT+DEVADDR=?` return their values in full
since none of those are secret; `AT+APPKEY=?`/`AT+NWKSKEY=?`/`AT+APPSKEY=?`
deliberately don't. If your application genuinely needs plaintext readback
(e.g. a provisioning tool that already trusts this serial link), each of
those three handlers in `WisBlockLoRaAT.cpp` is a few lines and clearly
marked - change them to call the same `printHex()` helper the non-secret
getters use, but that's a deliberate opt-in, not the default.

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

## ESP32/RAK3312: noisy "Preferences.cpp getBytesLength() NOT_FOUND" errors on first boot (fixed)

First test on RAK3312 hardware after this library's GitHub publication
turned up something alarming-looking in the boot log: roughly twenty
`[E][Preferences.cpp:503] getBytesLength(): nvs_get_blob len fail:
wb_lbm_crash_flag NOT_FOUND` lines during `lora.begin()`, plus more later
at join time. Worth being precise about what this was and wasn't:

**Not a bug in LBM, and not actually a functional failure.** LBM's crash-
status HAL callback (`smtc_modem_hal_crashlog_get_status()`) genuinely
does get called many times during a single `smtc_modem_init()` - once per
internal service that checks for a stored crash on its own bring-up
(class B, class C, relay TX, relay RX, ALC sync, and others) - and that's
expected LBM behavior, not something to reduce the call count of. Every
key this library ever reads (`wb_cfg`, `wb_lbm_crash`, `wb_lbm_crash_flag`,
`wb_lbm_0`..`wb_lbm_5`) is legitimately absent on a genuinely fresh board,
before the very first `saveConfig()`/crash/context write - a miss here was
never a real error, and `WisBlockLoRaFlash::read()`'s return value
(`false` on a miss) was already functionally correct throughout.

**The actual issue:** `WisBlockLoRaFlash::read()`'s ESP32 backend called
`Preferences::getBytes()` directly. Internally, that calls
`getBytesLength()`, which calls `nvs_get_blob()` to determine the stored
size - and the ESP32 Arduino core's own `Preferences.cpp` unconditionally
logs via `log_e()` whenever that call returns `NOT_FOUND`, regardless of
whether the caller treats a miss as an expected, ordinary condition (which
this one always does). With LBM calling the crash-status callback ~20
times per `smtc_modem_init()`, a single fresh-board boot could produce
exactly the wall of red `[E]` lines the log showed - real log noise, but
not an actual malfunction anywhere in the read/write path.

**Fixed** by checking `Preferences::isKey(key)` first and returning `false`
immediately on a miss, before ever calling `getBytes()`. Verified directly
against the ESP32 Arduino core's own `Preferences.cpp` source: `isKey()`
calls `getType()`, which checks the same underlying `nvs_get_blob()`
condition but does *not* call `log_e()` on failure - so this is the
official, intended way to check for a key's existence quietly. Identical
functional behavior (`read()` still returns `false` on a genuine miss),
just without conscripting the ESP-IDF logger into announcing every single
one of them as if something had gone wrong.

## `setADR(false)` silently not taking effect - ADR bit stayed on, DR kept drifting via real network commands (fixed)

RAK3312 LoRaWAN Class A field test: `AT+DR=3` / `setADR(false)` at startup,
but the very first application uplink went out at DR4, not DR3 - and the
network's own log showed `"adr": true` on every single frame, with the
network later issuing genuine `LinkADRReq` MAC commands that the device
correctly obeyed, shifting it to DR5. None of this should happen with ADR
off - and a follow-up test nailed the exact mechanism: manually calling
`AT+DR=` or `AT+ADR=0` again after join produced LBM's own trace line,
verbatim: `ERROR: ADR with a bad DataRate value`.

**Root cause, traced directly into LBM's `smtc_modem.c`:**
`smtc_modem_adr_set_profile(..., SMTC_MODEM_ADR_PROFILE_CUSTOM, ...)` -
what `setADR(false)`/`setDataRate()` map to - validates the requested DR
against `mask_dr_allowed`, the union of DR ranges every *currently
enabled* uplink channel supports. Right after a fresh join, only the
region's default join channels are enabled, and their DR range can be
narrower than what the network's post-join `NewChannelReq` MAC commands
later add (visible in the device's own log, right there as `Cmd
new_channel_parser` lines a few frames after join). If the requested DR
isn't covered yet, this call fails outright
(`SMTC_MODEM_RC_INVALID`) and - critically - LBM leaves the ADR profile
exactly as it was *before* the call: still `NETWORK_CONTROLLED`, its own
default at init. `LoRaWANEngine::applyAdrProfile()` discarded that return
code entirely - a bare `void` - so this failure was completely invisible
even outside debug builds. Every symptom traces back to this one silent
failure: the ADR bit stayed on because the profile never actually left
`NETWORK_CONTROLLED`; the network's own ADR engine then legitimately saw
that bit and ran its own algorithm; DR4 was LBM's own network-controlled
selection, not the requested DR3; and the device correctly obeyed the
resulting `LinkADRReq` because it genuinely was still in that mode the
whole time.

**Fixed** two ways:
1. `setADR()`/`setDataRate()` now return `bool` (`false` doesn't mean
   rejected - the requested value is still stored in `config.lorawan` and
   will apply as soon as it's achievable - it means "not active on the
   radio yet"). `AT+DR=`/`AT+ADR=` print an explicit `PENDING` note in that
   case instead of silently claiming success.
2. Added an automatic retry: `LoRaWANEngine` tracks `adrProfileApplied`,
   and if a `CUSTOM` profile push failed, retries it after every
   subsequent `TXDONE` event - the most likely moment a channel-widening
   downlink has just been processed on a prior RX window, without needing
   to parse which specific MAC command arrived. Retrying while ADR is on
   is a harmless no-op, and this stops firing entirely once a retry
   succeeds. In the common case (join, then a `NewChannelReq` arrives a
   few frames later, as in the field test that surfaced this), ADR-off
   should now take effect on its own within the first few uplinks with no
   application action needed - but until it does, `getConfig().lorawan.adrEnabled`
   reflects what was *requested*, not necessarily what's *active*; use the
   new return value if an application needs to know the difference in the
   moment.

## Correction: the real bug was `buildSingleDrDistribution()` itself, not channel-mask timing

The fix above was real (the return-code check and retry infrastructure
are both correct and worth keeping), but it didn't actually solve the
problem - a follow-up field test showed `setADR(false)`/`setDataRate(3)`
failing **every single time**, including at fcnt=11, long after all six
of AS923's extra channels had been added via `NewChannelReq` with
`DrMin=0, DrMax=7`. That ruled out "channel mask hasn't widened yet" as
the explanation - if that were it, later attempts should have started
succeeding. They never did, which meant the real bug had to be something
that stayed broken regardless of channel state.

Re-tracing `smtc_modem_custom_dr_distribution_to_tab()` in
`smtc_modem.c` character by character (rather than trusting the earlier
read) found it: `dr_custom_distribution_data`
(`SMTC_MODEM_CUSTOM_ADR_DATA_LENGTH` = 16 bytes) is **not** a one-hot
table indexed by DR, the way its shape invites you to assume. It's a flat
list of 16 literal DR *values*, one per retry-attempt slot - each entry
gets validated and counted directly against the channel mask as a DR
value in its own right, not as an index into anything.
`buildSingleDrDistribution()` was doing exactly the one-hot thing:
`out[dataRate] = 1`, leaving the other 15 of 16 slots at value `0`. For
`dataRate = 3`, that array said "15 of 16 attempts should use DR0, 1
attempt should use DR1" - not "always use DR3" at all. AS923 enforces a
dwell-time floor of DR2 (`MIN_TX_DR_LIMIT_AS_923` in
`region_as_923_defs.h`), which excludes DR0 and DR1 specifically - so
*every* slot failed validation, on *every* call, regardless of the
requested DR or how wide the channel mask ever got. This also explains
why the failure was present from the very first call in `setup()`,
before join - the default join channels support DR3 just fine (confirmed
directly in `region_as_923_config()`'s channel setup), so channel
availability was never actually the constraint.

**Fixed** by filling all 16 slots with the literal requested DR value,
which is what correctly tells LBM "always use this DR" - `smtc_real_get_next_tx_dr()`
in `smtc_real.c` counts occurrences per DR value across the slots that
survive validation to build its actual weighted-random selection table,
so a uniform array of one value produces exactly the "pin to this DR"
behavior the function's name always promised. The return-code check,
`PENDING` AT response, and automatic per-uplink retry from the fix above
all stay in place - genuinely useful defensive infrastructure for a
region/DR combination that legitimately does need to wait on channel
widening - they just weren't what was wrong here.

## `WisBlockTxResult::airtimeMs` always reported 0 for LoRaWAN uplinks (fixed)

Reported directly: `result.airtimeMs` in `onLoRaWANTxFinished()` has been
0 for every uplink since the TXDONE handler was first written -
`SMTC_MODEM_EVENT_TXDONE`'s own event data (`smtc_modem_api.h`) carries
only a status enum, no airtime figure, and nothing was ever computing one
to fill the field with.

**Fixed** by computing it with the standard LoRa airtime formula (Semtech
AN1200.13 - the same one `LoRaP2PEngine::computeAirtimeMs()` already uses
for P2P) fed by two things captured in `send()` right after an uplink is
accepted: `lorawan_api_next_dr_get(kStackId)` for the DR that specific
uplink will actually use (captured per-send rather than once, since ADR/
`LinkADRReq` can legitimately change it between one uplink and the next),
and an estimated PHY payload length (application length + the standard
13-byte MHDR+FHDR+FPort+MIC overhead, assuming no MAC commands happen to
be piggybacked in FOpts on that specific frame - not knowable from this
layer, so this is an estimate, not exact).

**Verified against this project's own hardware trace, not just derived
from the datasheet formula**: a real device log captured earlier in this
project showed an actual SF8/BW125 uplink with a 22-byte PHY frame and a
measured `toa = 103ms`. Feeding those same SF/BW/length values into the
formula predicts 102.9ms - a match, not a coincidence, and good
confirmation the formula and its parameters (preamble=8, CR=4/5, explicit
header, CRC on - all fixed by the LoRaWAN Regional Parameters spec) are
implemented correctly for this DR family.

The DR-to-SF/BW mapping (`drToSfBw()`) is implemented and confirmed for
EU868/AS923(-1..4)/RU864/IN865/KR920/CN470/CN470_RP_1_0's standard DR
tables, and for US915/AU915's 125kHz sub-band (DR0-DR4 - the range a
device's own uplinks normally use). Deliberately left unimplemented
(`airtimeMs` stays 0 rather than reporting a number that isn't backed by
anything): FSK data rates (not a LoRa airtime computation at all), and
US915/AU915's DR5-7 (RFU for uplink) and DR8-13 (500kHz channels) - less
commonly hit by a device's own uplinks, and without a hardware trace to
verify those specific entries the way the EU-like table above was
verified, reporting a number for them would be a guess dressed up as a
fact.

---

## 2026-09-14 - LoRaWAN Relay functionality removed (Claude AI)

At the requester's instruction, all LoRaWAN Relay support (both the
TX/end-device role and the RX/serving role, TS011 / RP002-1.0.4) has been
completely removed from this library. Everything described earlier in
this log under "Relay" reflects the *previous* state of the project and
is kept here for history, not as a description of the current code.

### Why this was more than deleting a couple of files

Relay wasn't an optional add-on bolted on at one layer - it was wired
into the LoRaWAN stack at four different levels:

1. **This library's own public API and AT command set** - `LoRaWANRelay`
   (a thin wrapper class), relay-specific fields in
   `WisBlockLoRaWANSettings`, `LoRaWANEngine`/`WisBlockLoRaWAN` setters,
   and five AT commands (`AT+RELAY`, `AT+RELAYED`, `AT+RELAYSRV`,
   `AT+RELAYDEV`, `AT+RELAYDEVDEL`).
2. **Vendored Semtech LoRa Basics Modem (LBM) relay modules** - two whole
   source directories (`lr1mac/src/relay/{common,relay_rx,relay_tx}` and
   `modem_services/relay_service/`) plus `smtc_modem_relay_api.h`.
3. **LBM's core MAC/crypto/scheduling code**, which had relay-specific
   branches spliced in at points that have nothing to do with relay on
   the surface: RX-window timing and frequency selection
   (`lr1_stack_mac_layer.c`), the MAC-command dispatcher's default case,
   session-key derivation (`soft_se.c`, `smtc_modem_crypto.c`), radio
   scheduler hook IDs (`radio_planner_hook_id_defs.h`), the public event
   enum (`smtc_modem_api.h`), duty-cycle accounting (`modem_core.c`), and
   - the single largest piece - a relay TX state machine woven through
   `modem_tx_protocol_manager.c`'s transmission scheduling logic (roughly
   twenty separate `#if defined( ADD_RELAY_TX )` blocks across ~1450
   lines).
4. **The PlatformIO build script** (`extra_script.py`) actually defined
   `ADD_RELAY_TX` and `ADD_RELAY_RX` unconditionally, so - contrary to
   what this log's own earlier "Honest scope statement" section implied
   about relay being an easily-disabled extra - relay code was in fact
   being compiled into every build of this library, and
   `wisblock_lbm_port.cpp`'s TCXO startup timing had a whole comment
   block (and an active 49ms clamp, `kMaxForRelayMs`) built around
   working around one of relay's own hard-coded protocol constraints.

### What was deleted outright

- `src/LoRaWANRelay.cpp` / `.h`
- `src/wisblock_relay_rx_bridge.c` / `.h`
- `src/lbm/smtc_modem_core/lr1mac/src/relay/` (all of `common/`,
  `relay_rx/`, `relay_tx/`)
- `src/lbm/smtc_modem_core/modem_services/relay_service/`
- `src/lbm/smtc_modem_api/smtc_modem_relay_api.h`

### What was surgically edited (relay code/comments removed, everything
### else in the file left untouched)

- `src/lbm/smtc_modem_api/smtc_modem_api.h` - removed the four
  `SMTC_MODEM_EVENT_RELAY_*` event enum values and their `relay_tx`/
  `relay_rx` union members from `smtc_modem_event_t`.
- `src/lbm/smtc_modem_core/radio_planner/src/radio_planner_hook_id_defs.h`
  - removed the `RP_HOOK_ID_RELAY_*` hook IDs and collapsed the
  now-unconditional `ADD_CLASS_C` numbering.
- `src/lbm/smtc_modem_core/smtc_modem_crypto/smtc_modem_crypto.c` / `.h`
  - removed `smtc_modem_crypto_derive_relay_session_keys()`.
- `src/lbm/smtc_modem_core/smtc_modem_crypto/smtc_secure_element/`
  `smtc_secure_element.h` - removed the three `SMTC_SE_RELAY_*` key-slot
  enum values and `smtc_secure_element_derive_relay_session_keys()`'s
  declaration.
- `src/lbm/smtc_modem_core/smtc_modem_crypto/soft_secure_element/soft_se.c`
  - removed the three relay key-slot table entries and the
  `smtc_secure_element_derive_relay_session_keys()` implementation
  (WOR root/integrity/encryption session-key derivation).
- `src/lbm/smtc_modem_core/smtc_modem.c` - removed relay event mapping
  and the entire public `smtc_modem_relay_tx_*` API implementation block
  (enable/disable/config get-set, activation mode, sync status).
- `src/lbm/smtc_modem_core/lorawan_packages/lorawan_certification/`
  `lorawan_certification.c` / `.h` - the certification test protocol's
  `LORAWAN_CERTIFICATION_RELAY_MODE_CTRL_REQ` opcode (0x53) is a fixed
  part of the TS011 certification protocol's command-ID space and was
  kept so the dispatcher still recognizes the message; it now
  unconditionally replies "not implemented" (its behavior already
  defaulted to, since `ADD_RELAY_TX` was never defined in *this specific
  file's* build path). The relay-specific config-parsing branch, the
  `smtc_modem_relay_api.h` include, and the unused
  `lorawan_certification_relay_tx_enabled_t` enum were removed.
- `src/lbm/smtc_modem_core/lr1mac/src/lr1mac_defs.h` - removed the
  relay-only `LWPSTATE_RXR`, `RXR` (rx window type), and
  `RECEIVE_ON_RXR` enum values.
- `src/lbm/smtc_modem_core/lr1mac/src/lr1_stack_mac_layer.c` - the
  largest single-file cleanup after the TX protocol manager: removed the
  relay includes, the `RXR` debug-name entry, the `FPORT_RELAY`
  encryption-key branch, the `RXR` case in both RX-window-frequency
  functions, the relay crystal-error adjustment in RX timing parameter
  calculation, the FPORT_RELAY MAC-decode-key branch, and the relay
  MAC-command-parser fallback in the default case of the MAC command
  dispatcher (now a plain "unknown mac command" trace, matching the
  behavior every other LBM build without relay already had).
- `src/lbm/smtc_modem_core/lr1mac/src/lr1mac_core.c` - removed the relay
  include and the entire `#else` (relay-enabled) branch of the
  `LWPSTATE_RXR`/`RECEIVE_ON_RXR` state-machine handling, keeping only
  the `#if !defined( ADD_RELAY_TX )` branch's body unconditionally.
- `src/lbm/smtc_modem_core/lorawan_manager/lorawan_join_management.c` -
  reworded one comment that referenced relay WOR duty-cycle exhaustion.
- `src/lbm/smtc_modem_core/lorawan_manager/lorawan_send_management.c` -
  removed the `RECEIVE_ON_RXR` branch from an RX-window check.
- `src/lbm/smtc_modem_core/modem_utilities/modem_core.c` - removed the
  relay include and the relay duty-cycle contribution to
  `smtc_duty_cycle_get_next_free_time_ms()`'s result.
- `src/lbm/smtc_modem_core/modem_utilities/modem_services_config.h` -
  removed the two relay service includes and their entries in the
  services-init table.
- `src/lbm/smtc_modem_core/modem_supervisor/modem_tx_protocol_manager.c`
  - the big one. Programmatically stripped all ~19 top-level
  `#if defined( ADD_RELAY_TX )` blocks (keeping the `#else` branch's body
  where one existed, deleting the block outright where it didn't), then
  removed the now-dead `MAX_TRIAL_RELAY` macro and
  `current_tpm_cpt_relay_max_trial` counter (write-only once the state
  machine reading them was gone), then rewrote the handful of Doxygen
  comment blocks that described the two-case (LoRaWAN vs. WOR/relay)
  transmission sequencing down to the single LoRaWAN-only case that's
  now the only one that exists.
- `src/lbm/smtc_modem_core/modem_supervisor/modem_tx_protocol_manager.h`
  - reworded one comment.
- `src/lbm/smtc_modem_core/CMakeLists.txt` - removed the `LBM_RELAY_RX`/
  `LBM_RELAY_TX` conditional blocks (these referenced source files that
  no longer exist; this file is for non-Arduino/CMake consumers of the
  vendored LBM tree and isn't used by the Arduino or PlatformIO builds
  of this library, but was cleaned up for consistency).

### This library's own code

- `src/WisBlockLoRaWANTypes.h` - removed `WisBlockRelayMode`,
  `WisBlockRelayEDConfig`, `WisBlockRelayServingConfig`,
  `WisBlockRelayTrustedDevice`, and the three relay fields from
  `WisBlockLoRaWANSettings`.
- `src/LoRaWANEngine.h` / `.cpp` - removed the `LoRaWANRelay.h` include
  and the five relay methods (`setRelayMode`, `configureRelayED`,
  `configureRelayServing`, `add/removeRelayTrustedDevice`); `begin()`/
  `applySettings()` no longer touch relay config at all.
- `src/WisBlockLoRaWAN.h` / `.cpp` - removed the same five methods at
  this layer, and rewrote `ensureLoRaWANEngineStarted()`'s doc comment
  (previously explained a real P2P/relay radio-contention bug this
  lazy-init design fixed; kept the P2P-contention explanation since
  eager `smtc_modem_init()` is still wasteful for P2P-only sketches, but
  removed the relay-specific mechanism since it no longer exists).
- `src/WisBlockLoRaAT.cpp` - removed the `RELAY=` line from `AT+CFG?`'s
  status dump, and all five `AT+RELAY*` command handlers
  (~185 lines total, including hex-key parsing for the trusted-device
  list and the config get/set pairs for both relay roles).
- `src/wisblock_lbm_port.cpp` - `maxSleepDuration()`'s TCXO startup-time
  comment explained a real hardware bug fix (5ms was too optimistic for
  this board's TCXO) but the actual constraint it was clamping against
  (`kMaxForRelayMs`, keeping the value under relay's
  `DELAY_WOR_TO_WORACK_MS` protocol timing budget so
  `smtc_relay_tx_init()` wouldn't panic) no longer applies to code that
  doesn't exist. Removed the clamp and the relay-specific half of the
  comment; kept the hardware-bug explanation and the 40ms base value,
  since that part was never about relay.
- `examples/RX-Duty-LoRaP2P/main.h` - removed the two commented-out
  `#define ADD_RELAY_TX/RX` lines.

### Build metadata and docs

- `library.properties`, `library.json`, `CHANGELOG.md` (description
  line) - dropped "Relay" from the one-line project description.
- `extra_script.py` - removed the `ADD_RELAY_TX`/`ADD_RELAY_RX`
  `CPPDEFINES` entries (these were the ones actually compiling relay
  code into every PlatformIO build of this library) and the four relay
  include-directory entries, and updated the file's own top-of-file
  scope comment to say relay was deliberately removed rather than
  vendored.
- `README.md` - removed "Relay (not yet tested)" from the feature list
  and deleted the six `AT+RELAY*` rows from the AT command reference
  table.

### What's intentionally still there

`src/lbm/smtc_modem_core/lorawan_packages/lorawan_certification/`
`lorawan_certification.{c,h}` still contain the `0x53`
(`LORAWAN_CERTIFICATION_RELAY_MODE_CTRL_REQ`) opcode. This is a fixed
command ID in the TS011 certification test protocol itself (the network
certification test tool can send this opcode to any device under test,
relay-capable or not); removing the enum value entirely would either
break the dispatcher's `switch` numbering or require it to silently
ignore a valid certification-protocol message instead of correctly
replying "not implemented," which is what a compliant non-relay device
is supposed to do. Two vendored upstream `CHANGELOG.md` files
(`smtc_modem_api/CHANGELOG.md`, `smtc_modem_hal/CHANGELOG.md`) also
still mention relay - these are Semtech's own historical release notes
for past LBM versions and document what upstream actually shipped, not
this library's current functionality, so they were left as-is.

### Verification

A recursive case-insensitive search for "relay" across the entire
repository (`grep -rli -i relay .`, excluding `.git`) now returns only:
this log file itself, the two vendored upstream `CHANGELOG.md` files
noted above, `extra_script.py`'s scope comment explaining the removal,
and the two intentionally-kept certification opcode references. No
source file references a deleted symbol, type, header, or macro
(`LoRaWANRelay`, `WisBlockRelay*`, `smtc_relay_tx_*`,
`smtc_modem_relay_*`, `relay_tx_api.h`, `relay_rx_api.h`,
`ADD_RELAY_TX`/`ADD_RELAY_RX`) anywhere in the tree.

**Not done**: this removal was performed by editing vendored C source
directly rather than by building and running the toolchain (no compiler
was available in this environment), so it has not been verified with an
actual `arduino-cli`/PlatformIO compile against real RAK4631/RAK3312/
RAK11310 targets. The `#if`/`#endif` block extraction in
`modem_tx_protocol_manager.c` in particular was done programmatically
across ~19 blocks; a real build (or at minimum a `gcc -fsyntax-only`
pass with the right include paths and macro defines from
`extra_script.py`) is the recommended next step before flashing this to
hardware.

---

## 2026-09-14 - Class C bug fix: device class silently never switched away from Class A

### The bug, as reported

A Class C device log showed RX1 and RX2 opening with a fixed timeout and
closing again after every single uplink, with no continuous receive
window ever appearing between transmissions - i.e. pure Class A
behavior, even though the application had configured Class C.

### Root cause

Traced into the vendored LBM source. `smtc_modem_set_class()`
(`src/lbm/smtc_modem_core/smtc_modem.c`) checks
`SMTC_MODEM_STATUS_JOINED` first and returns `SMTC_MODEM_RC_FAIL`
outright - leaving the class unchanged - if the device isn't joined yet:

```c
smtc_modem_status_mask_t status_mask = modem_get_status( stack_id );
if( ( status_mask & SMTC_MODEM_STATUS_JOINED ) != SMTC_MODEM_STATUS_JOINED )
{
    return SMTC_MODEM_RC_FAIL;
}
```

`LoRaWANEngine::setDeviceClass()` was called exactly once: from
`applySettings()`, which runs from `begin()` - i.e. before `join()` is
ever called. So the very first (and, before this fix, *only*) attempt to
switch to Class C was guaranteed to fail on every boot, and
`setDeviceClass()` didn't check the return code at all - the failure was
completely silent. The device was left on Class A permanently, matching
the log exactly.

This is the identical "must be joined first" gate that
`smtc_modem_adr_set_profile()` has (already documented in this file
under the ADR/DR-distribution fix), which is why the same log also shows
`[LoRaWAN] Failed to disable ADR` / `[LoRaWAN] Failed to set DR3` right
after init - same root cause, different symptom. ADR happens to recover
on its own because `handleEvents()`'s `TXDONE` case already retries
`applyAdrProfile()` once per uplink until it succeeds; there was no
equivalent retry anywhere for device class, so it never recovered.

### Fix

`LoRaWANEngine::setDeviceClass()` now checks `smtc_modem_set_class()`'s
return code and records success/failure in a new `classProfileApplied`
flag (mirroring `adrProfileApplied`'s existing pattern), and its return
type changed from `void` to `bool` so callers can see whether the class
actually took effect - `WisBlockLoRaWAN::setDeviceClass()` was updated to
match and forward the result. The flag is retried at every point the
device can newly become joined:

- `begin()` resets `classProfileApplied = false` (a fresh
  `smtc_modem_init()` always comes up in Class A regardless of what's
  about to be requested).
- `SMTC_MODEM_EVENT_JOINED` in `handleEvents()` - the actual moment an
  OTAA device becomes joined, and therefore the first point the pending
  class request can possibly succeed. This is the fix that matters for
  the log's OTAA join flow.
- The synchronous ABP-success path in `join()` - ABP never raises
  `SMTC_MODEM_EVENT_JOINED`, so this path needed its own retry rather
  than relying on the event handler.
- A `TXDONE`-time fallback retry in `handleEvents()`, mirroring the
  existing ADR retry, as a belt-and-suspenders catch-all in case
  `classProfileApplied` is somehow still false by the time an uplink
  completes (e.g. `RETURN_BUSY_IF_TEST_MODE`).

All four retries call `setDeviceClass()` unconditionally guarded by
`if (!classProfileApplied)`, so once the class switch actually lands
none of the later checks do anything.

### Files changed

`src/LoRaWANEngine.h` (new `classProfileApplied` member,
`setDeviceClass()` signature `void` -> `bool`), `src/LoRaWANEngine.cpp`
(the fix itself, in `begin()`, `setDeviceClass()`, `join()`'s ABP branch,
and both `SMTC_MODEM_EVENT_JOINED`/`TXDONE` cases in `handleEvents()`),
`src/WisBlockLoRaWAN.h` / `.cpp` (matching `bool` return, forwarded
through unchanged otherwise).

### Verification

Checked every existing call site of `setDeviceClass()`
(`WisBlockLoRaAT.cpp`'s `AT+CLASS=` handler, both example sketches) -
all call it as a bare statement and discard the return value already,
so the `void` -> `bool` signature change doesn't break anything. Ran a
comment/string-aware brace and paren balance check across all four
edited files (0 imbalance in each) in place of a full Arduino toolchain
build, which isn't available in this environment.

**Not done**: not verified against real hardware or a real network
server - same caveat as the rest of this log. If Class C still doesn't
behave correctly after this fix, the next place to look is
`src/lbm/smtc_modem_core/lr1mac/src/lr1mac_class_c/lr1mac_class_c.c`
itself (LBM's actual continuous-RX scheduling for Class C) rather than
this library's wrapper, since this fix only addresses *requesting* the
class switch actually reaching LBM - not LBM's own Class C RX scheduling
logic, which was not modified.

---

## 2026-09-14 - Sub-band pre-selection (AT+MASK / setChannelMask), RUI3-compatible

### The problem

US915, AU915, and CN470/CN470_RP_1_0 define far more uplink/downlink channels
than a typical 8-channel gateway actually listens on (72 channels/8 sub-bands
for US915/AU915, 64 or 96 channels/8 or 12 sub-bands for CN470/CN470_RP_1_0).
Without knowing in advance which sub-band the gateway is on, a joining device
has to cycle through every sub-band's channels across repeated join attempts
before it happens to transmit on one the gateway is actually listening to -
wasting join requests, airtime, and time-to-first-join, all avoidable if the
application already knows (or can be told) which sub-band to use.

RUI3 solves this with `AT+MASK` / `api.lorawan.mask.set()`: a 16-bit mask,
one bit per sub-band, applied *before* the join request so it only ever
transmits on the selected sub-band's channels. This adds the same mechanism
here, matching RUI3's command name, mask encoding, and API shape exactly (per
the RUI3 AT Command Manual's `AT+MASK` section and the RUI3 LoRaWAN API's
`api.lorawan.mask`/`RAKLorawan::mask` - RUI3's own doc page doesn't render the
`mask` API section directly, but its signature - `bool set(uint16_t* mask)` -
and encoding are confirmed against RAKwireless's own `RUI3-Best-Practice`
repo and RAK4630 quick-start guide, both of which show
`uint16_t maskBuff = 0x0001; api.lorawan.mask.set(&maskBuff);`).

### Why this took real engineering, not just an AT command passthrough

The vendored LBM stack has no existing public API for "restrict channels to
this sub-band." The closest primitive, `smtc_real_set_channel_enabled()`
(per-channel on/off), explicitly does **not** support US915/AU915/CN470/
CN470_RP_1_0 - its own source says so ("// Not supported") for exactly these
regions, because they use LoRaWAN's `LinkADRReq`-style `ChMaskCntl`/`ChMask`
mechanism instead of simple per-channel toggles.

The actual usable primitive is `smtc_real_build_channel_mask( real, ch_mask_cntl,
ch_mask )`, which is the same function the network's own `LinkADRReq` handling
calls into - this just runs it locally, before joining, instead of waiting
for the network to do it after joining:

- **US915/AU915**: `ChMaskCntl=5` ("bank of channels" mode) is a direct match
  for what's needed - bit N of the 16-bit `ChMask` enables/disables the whole
  8-channel sub-band N+1 (its 8 125kHz channels *and* its one 500kHz wide
  channel) as a single unit. This lines up bit-for-bit with RUI3's own
  `AT+MASK` encoding for these regions, so no translation is needed beyond
  masking to 8 bits. `mask == 0` (RUI3's "ALL" convention) uses `ChMaskCntl=6`
  instead, which turns every 125kHz channel on and takes the wide-channel
  mask as its `ChMask` parameter (set to `0x00FF` = all 8 wide channels on).
- **CN470 / CN470_RP_1_0**: no such shortcut exists - each `ChMaskCntl`
  value (0-3 for the 64-channel CN470, 0-5 for the 96-channel
  CN470_RP_1_0) covers a plain 16-channel (2 sub-band) window with a
  literal 16-bit `ChMask`. Selecting one sub-band means building the right
  `ChMask` for whichever 16-channel block contains it (0x00FF for the lower
  half, 0xFF00 for the upper half) and calling `smtc_real_build_channel_mask()`
  once per block - including the blocks that should end up all-disabled,
  since each call only touches its own block and previous state could have
  left other blocks enabled.

### What was added

- `src/lbm/smtc_modem_core/lorawan_api/lorawan_api.c` / `.h` (mid-level API
  layer, the same one `LoRaWANEngine.cpp` already reaches into for
  `lorawan_api_next_dr_get()`, since `smtc_modem_api.h` has no equivalent) -
  new `lorawan_api_set_channel_mask( stack_id, mask )` and
  `lorawan_api_get_channel_mask( stack_id )`, plus the two region-specific
  static helpers described above. Region is read via the existing
  `lr1mac_core_get_region()`; the mask actually applied is remembered in a
  small per-stack static array since `smtc_real` itself only tracks
  per-channel enabled bits, not "which mask produced this state." Regions
  outside the four listed are a documented no-op, matching RUI3's own
  `AT+MASK` scoping ("only for US915, AU915, LA915, CN470" - LA915 isn't a
  distinct region in this vendored LBM v4.9.0, so it doesn't apply here).
- `src/WisBlockLoRaWANTypes.h` - new persisted `channelMask` field (`uint16_t`,
  default 0 = no restriction) on `WisBlockLoRaWANSettings`.
- `src/LoRaWANEngine.h` / `.cpp` - new `setChannelMask()`/`getChannelMask()`.
  Unlike `setDeviceClass()`/`setADR()`, this does **not** require the device
  to already be joined (it only affects which channels this device itself
  considers when transmitting) - it's pushed to LBM from `applySettings()`
  immediately after `smtc_modem_set_region()`, every time settings are
  (re)applied, specifically so it's in effect *before* the first join
  attempt rather than needing a post-join retry like the Class C fix above.
- `src/WisBlockLoRaWAN.h` / `.cpp` - matching `setChannelMask()`/
  `getChannelMask()` at this layer, following the same
  `config.lorawan.X` / `ensureLoRaWANEngineStarted()` / `lorawan.X()` pattern
  as every other setter here.
- `src/WisBlockLoRaAT.cpp` - new `AT+MASK` command, matching RUI3's exactly:
  `AT+MASK=?` / `AT+MASK=<4 hex digits>`, same `AT_PARAM_ERROR` behavior on a
  malformed value. Also added a `MASK=` line to the `AT+CFG?` status dump
  alongside the existing `REGION=`/`CLASS=`/`ADR=` lines.
- `README.md` - new `AT+MASK` row in the AT command reference table.

### Usage

```cpp
loraWan.setRegion(WISBLOCK_REGION_US915);
loraWan.setChannelMask(0x0001); // sub-band 1 (channels 0-7 + 64) - e.g. The Things Network US915
loraWan.join();
```

or via AT command, before `AT+JOIN`:

```
AT+BAND=5
AT+MASK=0001
AT+JOIN
```

### Verification

`lorawan_api.c` (containing the new mask logic) was checked with
`gcc -fsyntax-only` using the project's real build flags/include paths and
all four region defines (`REGION_US_915`, `REGION_AU_915`, `REGION_CN_470`,
`REGION_CN_470_RP_1_0`) - zero errors. `LoRaWANEngine.cpp/h`,
`WisBlockLoRaWAN.cpp/h`, and `WisBlockLoRaAT.cpp` were checked with a
comment/string-aware brace-and-paren balance script (Arduino-target files
can't be fed through a plain `gcc` syntax check the way the vendored LBM C
files can, since they need the Arduino core and this board's HAL headers,
neither of which exist in this environment).

**Not done**: no access to real US915/AU915/CN470 hardware or gateways in
this environment, so the actual join-time channel selection has not been
verified against a live network server. The CN470 (non-RP_1_0) 26MHz
channel-plan variant is a known, called-out limitation (see the code
comment in `lorawan_api_set_channel_mask_cn_470()`) - sub-band selection has
no effect in that specific plan, since that region's own
`region_cn_470_build_channel_mask()` always enables every channel for the
16-channel block covering channels 48-63 regardless of the mask given, when
a 26MHz plan is active. The far more common 20MHz A/B plan, and
CN470_RP_1_0, are both unaffected by this and work as described above.

---

## 2026-09-14 - Sub-band mask bug fix: first join attempt ignored the configured sub-band

### The bug, as reported

With `lora.setChannelMask(0x0002)` (sub-band 2) configured before joining on
US915, a device log showed the *first* join attempt transmitting on a
sub-band 1 frequency (902.9 MHz) anyway. It failed, retried, and the second
attempt happened to land on a sub-band 2 frequency (904.9 MHz) and
succeeded - but only the second attempt honored the requested mask.

### Root cause

Traced into exactly how LBM's join channel selection actually decides which
channel to transmit on, since that turned out to be a different code path
than the one `smtc_real_build_channel_mask()` (added in the previous
sub-band mask feature) writes to.

`smtc_real_build_channel_mask()` only stages the requested mask into a
scratch buffer (`unwrapped_channel_mask`). It is **not** what channel
selection reads. `region_us_915_get_join_next_channel()` (and its AU915/
CN470/CN470_RP_1_0 equivalents) read a separate, region-owned
`channel_index_enabled` array - and that array is only ever updated by a
second, distinct function: `smtc_real_set_channel_mask()`, which copies the
staged buffer into it. In normal operation, `smtc_real_set_channel_mask()`
is called by LBM itself while processing an incoming network `LinkADRReq` -
strictly a post-join event. Nothing in the join path itself ever calls it.

The previous implementation called `smtc_real_build_channel_mask()` and
stopped there, so the staged sub-band selection never actually reached
`channel_index_enabled`. That array stayed at its region-init default (every
channel enabled) for the entire pre-join period, so
`region_us_915_get_join_next_channel()`'s random pick among "active"
channels was effectively unrestricted - every join attempt (not just the
first) was choosing uniformly at random across all 8 US915 sub-bands,
completely independent of whatever mask had been requested. A join that
happens to land on the requested sub-band in that state - like the log's
second attempt - is coincidence (roughly a 1-in-8 chance per attempt for
US915/AU915), not the mask taking effect.

### Fix

Both `lorawan_api_set_channel_mask_us_au_915()` and
`lorawan_api_set_channel_mask_cn_470()` (in
`src/lbm/smtc_modem_core/lorawan_api/lorawan_api.c`) now call
`smtc_real_set_channel_mask( lr1_mac_obj[stack_id].real )` immediately after
staging the mask via `smtc_real_build_channel_mask()`, committing it into
`channel_index_enabled` right away instead of waiting for a post-join
`LinkADRReq` that, before joining, is never going to come.

For US915/AU915 specifically, this commit function
(`region_us_915_channel_mask_set_after_join()` /
`region_au_915`'s equivalent - the "_after_join" in the name refers to when
LBM itself normally calls it, not any restriction on when it's safe to call)
also copies the mask into `snapshot_channel_tx_mask`, the array used to
round-robin channels within a sub-band across retries. Separately,
`lr1mac_core_join_status_clear()` resets that same snapshot back to
"everything available" at the start of every join sequence - but that reset
only affects which *already-enabled* channels get retried in what order
within a sub-band; it does not re-enable a sub-band our mask has switched
off. `region_us_915_get_join_next_channel()` ANDs the snapshot against
`channel_index_enabled` before considering a channel a candidate, so a
disabled sub-band stays disabled (zero candidates in that block, causing the
selection loop to move on to the next block) regardless of what state the
snapshot itself resets to. The commit to `channel_index_enabled` is what
matters, and it's not touched by that reset - so the fix holds from the very
first join attempt onward, not just after a retry happens to clear the
snapshot too.

### Files changed

`src/lbm/smtc_modem_core/lorawan_api/lorawan_api.c` only - both region
helper functions gained one `smtc_real_set_channel_mask()` call each, plus
an expanded doc comment on the function group explaining the staging/commit
split so a future edit doesn't drop this call again by "simplifying" back to
just the build step.

### Verification

Re-checked with `gcc -fsyntax-only` using the project's real build flags and
all four region defines - zero errors. Re-derived the fix by reading the
actual channel-selection code path end to end
(`region_us_915_get_join_next_channel()` and its US915/AU915/CN470/
CN470_RP_1_0 counterparts, plus `smtc_real_set_channel_mask()`'s dispatch to
each region's own commit function) rather than assuming the first
implementation's `smtc_real_build_channel_mask()` call was sufficient, which
is exactly the assumption that was wrong the first time.

**Not done**: as with the mask feature itself, not verified against real
US915/AU915/CN470 hardware or a live gateway in this environment - the fix
is derived from a precise reading of the vendored LBM channel-selection
source, not from a repeat hardware test. If sub-band selection still
misbehaves after this, the next thing to check would be whether
`applySettings()`'s ordering (region set -> mask set, both before any join
call) is actually what's running in the failing case, since a mask set
*after* a join attempt has already started obviously can't affect that
attempt.

---

## 2026-09-14 - Sub-band mask bug fix #2: mask reset before *every* join attempt, not just before joining

### The bug, as reported (with the previous fix already applied)

Testing the previous fix (`smtc_real_set_channel_mask()` added to
`lorawan_api_set_channel_mask()`) against real hardware: `setChannelMask(0x0002)`
called well before `join()`, following the documented order (region, then
mask, then class/ADR/DR) - and the first join attempt was *still* transmitted
on a sub-band 1 frequency (902.9 MHz). Second attempt landed on sub-band 2
(904.9 MHz) and succeeded - the exact same symptom as before, meaning the
previous fix, while correct as far as it went, did not actually solve the
reported problem.

### Root cause - deeper than the first fix reached

The first fix correctly identified that `smtc_real_build_channel_mask()`
alone doesn't commit into `channel_index_enabled` (the array channel
selection reads) and added the missing `smtc_real_set_channel_mask()` commit
call - but that commit happens once, at `setChannelMask()`/`applySettings()`
time, before `join()` is ever called. It didn't yet account for what happens
*inside* the join sequence itself, between that commit and the actual
channel pick for each transmitted join request.

Traced `lr1mac_core_update_join_channel()` - called fresh before *every*
join attempt (the first one and every automatic retry, since LoRaWAN's OTAA
join procedure is its own internal retry loop inside LBM, not something this
library's `join()` call re-triggers each time - hence the log's "device is
already join" warning on the logged retry, which was LBM's own internal
join task firing again, not a second `join()` call from the application
actually doing anything). Its very first line is:

```c
lr1_stack_mac_region_config( lr1_mac_obj );
```

which calls `smtc_real_config()` -> `region_us_915_config()` (or the AU915/
CN470/CN470_RP_1_0 equivalent), and *that* function unconditionally marks
every channel in the region's hardcoded default plan as enabled -
`SMTC_PUT_BIT8( channel_index_enabled, i, CHANNEL_ENABLED )` for every
single channel index, no exceptions. This isn't a bug in LBM - by design, a
join request is supposed to be able to use any default channel unless
something has restricted it - but it means whatever `channel_index_enabled`
state the first fix committed gets silently thrown away and reset back to
"every channel enabled" immediately before every single join channel
selection, first attempt included. Committing the mask earlier (whether from
`setChannelMask()` directly, or from `applySettings()` before `join()` is
ever called) cannot fix this, because the problem isn't *when* the mask gets
committed once - it's that something resets it again, unconditionally, on a
per-attempt basis, after that.

### Fix

`lr1mac_core_update_join_channel()` (`src/lbm/smtc_modem_core/lr1mac/src/lr1mac_core.c`)
now calls `smtc_real_set_channel_mask( lr1_mac_obj->real )` immediately
after `lr1_stack_mac_region_config()`, on every call - i.e. before every
single join channel selection, first attempt and every retry alike.
`smtc_real_set_channel_mask()` re-copies whatever mask was last staged via
`smtc_real_build_channel_mask()` (in `unwrapped_channel_mask`, a completely
separate buffer that `region_xxx_config()` never touches and therefore
survives its reset untouched) back over the default `region_config()` just
set - undoing the unwanted reset immediately, on every attempt, rather than
only once before the first one.

This call is unconditional (not gated to only the four sub-band-capable
regions) and confirmed safe for every other region too: the staged buffer
each region reads from is always initialized to a sane "all channels
enabled" default by that region's own `region_xxx_init()` (e.g. EU868's
`region_eu_868_init()` explicitly does
`memset( &unwrapped_channel_mask[0], 0xFF, BANK_MAX_EU868 )`), and that init
always runs at least once - inside `smtc_modem_set_region()` - before
`lr1mac_core_update_join_channel()` can ever be reached. So for a region
with no custom mask ever requested, this call re-applies the same
"everything enabled" state `region_config()` just set - a harmless no-op -
and for the four regions this library's `setChannelMask()` actually
supports, it re-applies the real restriction instead.

### Files changed

`src/lbm/smtc_modem_core/lr1mac/src/lr1mac_core.c` only - one function,
`lr1mac_core_update_join_channel()`, gained one line plus an explanatory
comment.

### Verification

`gcc -fsyntax-only` against the project's real build flags and all four
region defines - zero errors. Traced the actual call graph from
`smtc_modem_join_network()` down through `lorawan_join_add_task()` to
`lr1mac_core_update_join_channel()` to confirm this function genuinely runs
before every join attempt (not just the first), matching both this log's
symptom and the previous, still-unresolved report's symptom exactly -
including why the "successful" second attempt in both logs landed on the
requested sub-band by coincidence rather than by the mask actually working
(1-in-8 odds for a US915 sub-band on a single random attempt), and separately
confirmed via each region's own `_init()` function that the "safe for every
region, not just the four we handle" assumption behind making this call
unconditional actually holds.

**Not done**: still not verified against real hardware in this environment.
Given that the previous fix looked complete by the same kind of source-level
reasoning and turned out not to fully resolve the issue, this one should be
treated as "traced and reasoned through to the actual point where the reset
happens, per-attempt, not just per-mask-set" rather than as a guaranteed fix
until it's been tried again against a real US915 (or AU915/CN470) gateway.
If sub-band selection is *still* not effective after this, the next place to
look would be whether something else calls `lr1_stack_mac_region_config()`
through a path other than `lr1mac_core_update_join_channel()` on the way to
an actual join transmission that wasn't accounted for here.

---

## 2026-09-15 - Class A bug fix: FPending downlinks never drained faster than the app's own send interval

### The bug, as reported

With multiple downlinks queued on the LNS, only the first was ever
delivered. The downlink log confirmed `f_pending = true` on it (the network
correctly signaling more were waiting), but the device never sent a prompt
follow-up uplink to fetch them - it just kept receiving one additional
downlink per its own regular, application-scheduled uplink (confirmed
against the device log: downlinks `0x1` through `0x7` arrived one per normal
`[LOOP] Send` cycle, roughly a minute apart, not back-to-back).

### Root cause

For Class A, a downlink can only ever arrive in RX1/RX2, which only open in
response to an uplink the device itself sends. When the network has more
than one downlink queued, it sets the FPending bit on the one it does send,
signaling "there's more - send another uplink soon so I can give you the
rest" (LoRaWAN 1.0.4 section 5.1). Nothing about this is automatic on the
network side; the device has to act on it.

`smtc_modem_get_downlink_data()`'s output struct
(`smtc_modem_dl_metadata_t`) already carries this bit
(`meta.fpending_bit`), and LBM's MAC layer already parses it out of the
downlink's `FCtrl` byte correctly - but `LoRaWANEngine::handleEvents()`'s
`SMTC_MODEM_EVENT_DOWNDATA` case only ever read `meta.fport`, `meta.rssi`,
and `meta.snr` from that struct. `fpending_bit` was retrieved and then
silently discarded, every single time. With no code path ever acting on it,
the device had no way to know it should ask for more - so it never did,
and the "next" downlink only ever showed up whenever the application's own,
completely unrelated periodic send happened to open another RX window.

### Fix

`SMTC_MODEM_EVENT_DOWNDATA` handling in `LoRaWANEngine.cpp` now reads
`meta.fpending_bit` into the new `WisBlockRxResult::fpending` field (so the
application can see it too, if it wants to), and - when
`WisBlockLoRaWANSettings::fetchPendingDownlinks` is true (the new default)
and the device is Class A - automatically issues an empty, unconfirmed
uplink via `smtc_modem_request_uplink()` right away, rather than waiting for
whatever the application's own send cadence happens to be. This drains a
queue of N pending downlinks at roughly one extra uplink+RX-window cycle
apart, instead of one per full application send interval.

Two real LBM API constraints had to be handled to make the automatic uplink
call itself succeed, both found by reading `smtc_modem_request_uplink()`/
`smtc_modem_send_tx()`'s actual validation rather than assuming:

- `smtc_modem_request_uplink()` rejects a NULL payload pointer outright
  (`RETURN_INVALID_IF_NULL`), even when the requested length is 0 - so the
  empty uplink uses a real (just unused) 1-byte static buffer with
  `payload_length` explicitly 0, not `nullptr`.
- `smtc_modem_send_tx()` forbids FPort 0 for application uplinks (it's
  reserved for MAC-only frames) - so if the pending-flagged downlink itself
  had no application payload (arrived on FPort 0, a MAC-only downlink),
  the follow-up empty uplink falls back to FPort 1 instead of using
  `meta.fport` directly and failing.

Scoped to Class A only: Class B/C devices already have a standing receive
window (ping slots / continuous RXC) and don't need to ask for the next
downlink with an extra uplink - the network can just send it. Made
opt-out-able (`fetchPendingDownlinks = false`, `AT+FPENDING=0`) rather than
unconditional, since this does spend extra airtime/duty-cycle budget per
pending downlink, which not every application may want to spend
automatically - `WisBlockRxResult::fpending` is still reported either way,
so an application that opts out can still see the bit and act on it itself.
Guarded by `uplinkPending` as a safety net against ever double-queuing,
though in normal operation this event fires after the TXDONE that already
cleared it, for the same RX cycle.

### Files changed

`src/WisBlockLoRaWANTypes.h` (new `WisBlockLoRaWANSettings::fetchPendingDownlinks`,
default true; new `WisBlockRxResult::fpending`), `src/LoRaWANEngine.h`/`.cpp`
(the fix itself, in the `SMTC_MODEM_EVENT_DOWNDATA` case, plus
`setFetchPendingDownlinks()`/`getFetchPendingDownlinks()`),
`src/WisBlockLoRaWAN.h` (matching setter/getter, same
`config.lorawan.X`/`ensureLoRaWANEngineStarted()`/`lorawan.X()` pattern as
every other setting here), `src/WisBlockLoRaAT.cpp` (new library-specific
`AT+FPENDING` command - no RUI3 equivalent to mirror here), `README.md`
(new AT command table row).

### Verification

Checked `smtc_modem_request_uplink()`'s and `smtc_modem_send_tx()`'s actual
validation logic in the vendored LBM source (not assumed) to confirm the
NULL-payload and FPort-0 handling above are real constraints and not
speculative caution. Comment/string-aware brace-and-paren balance check
across all five edited files - zero imbalance in each.

**Not done**: not verified against a real device/LNS pair with multiple
downlinks queued in this environment - the fix is derived from reading
LBM's downlink metadata plumbing and uplink validation end to end, matching
the reported symptom and the attached LNS log's confirmed `f_pending=true`,
but hasn't been re-tested against hardware here.

---

## 2026-09-16 - Regression fix: switching to AS923 after a sub-band mask was set elsewhere sent join on freq:0

### The bug, as reported

After testing on AU915 with a sub-band mask configured (per the previous
two fixes), switching the region to AS923-3 via `setRegion()` appeared to
succeed, but the join request was transmitted on `freq:0` and nothing
followed - no RX1/RX2, no further log output at all.

### Root cause - a real regression from this library's own previous fix

The second sub-band-mask fix (`lr1mac_core_update_join_channel()` calling
`smtc_real_set_channel_mask()` unconditionally, for every region, before
every join channel selection) was built on the assumption that doing so is
always a safe no-op when no custom mask is active, because the staged
buffer it copies from would already match the "all channels enabled"
default that `lr1_stack_mac_region_config()` (called immediately before it,
in the same function) had just set.

That assumption holds for US915/AU915/CN470/CN470_RP_1_0 - fixed channel
plans where every one of the 64-96 channel slots always has a real, valid,
spec-hardcoded frequency, so "all channels enabled" is always a
legitimately usable state. It does **not** hold for AS923 (and, by the same
code pattern, EU868/IN865/KR920/RU864): `region_as_923_config()` correctly
sets `channel_index_enabled` to just the region's real default boot
channels (typically 2), but *separately* blanket-sets a second buffer,
`unwrapped_channel_mask`, to "every possible channel slot enabled"
regardless of whether each slot actually has a frequency assigned yet -
most of AS923's 16 possible channel slots don't, until a join-accept's
CFList (if the network sends one) populates them. The unconditional
`smtc_real_set_channel_mask()` call copied that naively-all-enabled
`unwrapped_channel_mask` straight over the correctly-computed
`channel_index_enabled`, re-enabling channel slots with a frequency of 0.
`region_as_923_get_next_channel()` picks randomly among "enabled" channels
without separately checking the frequency is non-zero, so it could - and,
per the reported log, did - pick one of those bogus zero-frequency slots
and hand back `freq:0` for the actual transmission.

This didn't show up during the AS923 testing that produced earlier fixes
because it only manifests when `unwrapped_channel_mask`'s "all enabled"
default has previously been legitimately relied on for a *different*,
fixed-plan region (i.e., a sub-band mask, or even just the "no restriction"
default, was ever committed for US915/AU915/CN470 in the current session) -
switching to AS923 afterward carries that same unconditional re-commit
behavior into a region where it's actively wrong, not just unnecessary.

### Fix

`lr1mac_core_update_join_channel()`'s `smtc_real_set_channel_mask()` call is
now scoped to exactly the four regions this library's sub-band mask feature
actually manages (`SMTC_REAL_REGION_US_915`, `_AU_915`, `_CN_470`,
`_CN_470_RP_1_0`), via a region-type switch using the same
`lr1mac_core_get_region()` already used elsewhere in this file. Every other
region (AS923, EU868, IN865, KR920, RU864, ...) now falls through a `default`
case that does nothing, leaving `region_xxx_config()`'s own frequency-aware
channel setup completely untouched - exactly as it worked before either
sub-band-mask fix was introduced.

### Files changed

`src/lbm/smtc_modem_core/lr1mac/src/lr1mac_core.c` only -
`lr1mac_core_update_join_channel()`'s single unconditional
`smtc_real_set_channel_mask()` call replaced with a region-gated switch
statement around the same call.

### Verification

`gcc -fsyntax-only` against the project's real build flags, run three times
with different subsets of region `#define`s active (all regions; only
AS923+EU868; only a single fixed-plan region at a time) to confirm the
`#if`-guarded case labels compile correctly in every combination, not just
the "all regions" case this environment's normal flag set exercises.
Comment/string-aware brace-and-paren balance check - zero imbalance.
Re-derived the fix by reading `region_as_923_config()` and
`region_as_923_get_next_channel()` in full to confirm the exact mechanism
(two separately-maintained buffers, one frequency-aware and one not) rather
than assuming a single shared root cause with the fixed-plan regions.

**Note, not acted on**: `settings.channelMask` itself is a persisted value
that isn't automatically cleared when `setRegion()` switches to a different
region - so a mask set for AU915 will still be sitting in
`WisBlockLoRaWANSettings::channelMask` (and get harmlessly no-op'd by
`lorawan_api_set_channel_mask()`, per its existing region check) after a
later switch to, say, US915 again. This didn't need to change to fix the
reported bug and doing so wasn't requested, but is worth knowing: switching
between two *mask-capable* regions in the same session carries the
previous region's mask value over as a starting point, unless
`setChannelMask()` is called again explicitly after the region change.

**Not done**: not re-verified against real AS923/AU915 hardware in this
environment - the fix is derived from reading both regions' actual channel-
management source end to end and matches the reported symptom precisely,
but hasn't been re-tested on a device.

---

## 2026-09-16 - FPending auto-fetch fix #2: it was starving the application's own scheduled sends

### The bug, as reported

With 7 downlinks enqueued on the LNS: the first uplink after join delivered
2 downlinks, the next uplink logged an application-level error
("[LOOP] Start sending failed") and delivered only 1, and this pattern
repeated until the queue was empty - at which point a single downlink
worked cleanly again with no error.

### Root cause - a real side effect of the previous FPending fix, not a separate bug

Traced against the attached log line by line. The FPending auto-fetch
(previous fix: automatically send an empty uplink when a downlink's
FPending bit is set, so a Class A device drains a queued backlog promptly
instead of one per the application's own send interval) was firing
correctly and delivering downlinks quickly, exactly as designed - the "2
packets on one uplink" the report describes is actually two separate,
closely-spaced uplinks (the real one, then the auto-fetch immediately
after), which is the intended behavior.

The actual problem: `LoRaWANEngine::send()`'s existing `uplinkPending` guard
(added by an earlier fix specifically to stop two uplinks from racing each
other - see its own doc comment) can't tell the difference between "a real
application uplink is in flight" and "this library's own internal, empty,
no-payload auto-fetch uplink is in flight." AS923 (like several regions)
has real duty-cycle/LBT constraints, so a queued uplink can sit for tens of
seconds before actually transmitting - confirmed in the log by a 66-second
gap between an auto-fetch being queued ("INFO: add send task") and it
actually going out. Every time the application's own regularly-scheduled
`send()` call happened to land inside that window, it was rejected outright
by the existing guard, indistinguishable from a genuine double-send race,
even though the application did nothing wrong.

### Fix

Added a way for `send()` to tell the two cases apart and, in the harmless
one, defer rather than refuse:

- A new `autoFetchUplinkPending` flag (`LoRaWANEngine.h`) records whether
  the uplink currently occupying LBM's single slot is this library's own
  auto-fetch rather than a real application send.
- A new single-slot `deferredSend` buffer holds at most one application
  `send()` request that arrives while `autoFetchUplinkPending` is true.
  `send()` now returns `true` for this case (the request genuinely will be
  transmitted, just not immediately) instead of `false`. A second `send()`
  arriving before the first deferred one has gone out still falls through
  to the original, correct refusal - this remains a single-slot mechanism,
  not a general uplink queue.
- `SMTC_MODEM_EVENT_TXDONE` handling now checks, right after clearing
  `uplinkPending`, whether the just-completed uplink was the auto-fetch and
  a deferred send is waiting - if so, it's dispatched immediately, since the
  slot that TXDONE just freed is exactly what it needed.
- The actual `smtc_modem_request_uplink()` call plus its
  `uplinkPending`/`lastTxDr`/`lastTxPhyPayloadLen` bookkeeping was factored
  out of `send()` into a new private `dispatchUplink()`, shared by `send()`
  itself, the auto-fetch's own call, and the deferred-send replay - so all
  three paths stay in sync rather than three copies of the same bookkeeping
  drifting apart over time.
- One ordering bug caught and fixed while writing this: the TXDONE handler
  computes `WisBlockTxResult::airtimeMs` from `lastTxDr`/`lastTxPhyPayloadLen`,
  which `dispatchUplink()` overwrites - so that computation now happens
  *before* the deferred-send replay's `dispatchUplink()` call, not after, or
  the reported airtime would have described the just-replayed uplink instead
  of the auto-fetch uplink this particular TXDONE actually completed.

### Files changed

`src/LoRaWANEngine.h` (new `autoFetchUplinkPending`/`deferredSend` members,
new private `dispatchUplink()` declaration, updated `send()` doc comment),
`src/LoRaWANEngine.cpp` (`send()` refactored to use `dispatchUplink()` and
handle the deferred case; the auto-fetch call site now goes through
`dispatchUplink()` too and sets `autoFetchUplinkPending`;
`SMTC_MODEM_EVENT_TXDONE` handling gained the replay logic, with the
airtime-ordering fix above).

### Verification

Comment/string-aware brace-and-paren balance check on both files - zero
imbalance. Traced the full event ordering against the attached log
(auto-fetch queued via "add send task", application's own "[LOOP] Send"
landing inside the ~66-second duty-cycle/LBT delay before that auto-fetch
actually transmitted, "[LOOP] Start sending failed" immediately after) to
confirm this is exactly the collision window the fix closes, and confirmed
the pattern stops recurring once the downlink queue empties (no more
auto-fetch uplinks competing for the slot), matching the report's own
observation.

**Known minor limitation, not addressed**: a `send()` call that gets
deferred bypasses `setLinkCheckMode()`'s AT+LINKCHECK piggyback logic (which
normally runs between the `uplinkPending` check and the actual dispatch) -
if link-check mode 1 (one-shot) is active and a send happens to land in the
deferred path, that particular LinkCheckReq piggyback is skipped rather
than carried over to the replay. Low severity (link check mode is opt-in
and the request would simply be retried on the next mode-1-triggering send),
but worth knowing.

**Not done**: not re-verified against real AS923 hardware with a multi-
downlink LNS queue in this environment - the fix is derived from a full
trace of the attached log against the actual `uplinkPending`/`send()`/
TXDONE code path, but hasn't been re-tested on a device.

---

## 2026-09-16 - FPending auto-fetch fix #3: the previous fix only handled one collision deep

### The bug, as reported (with fix #2 already applied)

Now only one downlink was fetched per uplink instead of draining faster,
and a single "[LOOP] Start sending failed" still occurred.

### Root cause - fix #2's deferral was scoped one level too narrowly

Traced the new log's exact event ordering, matching timestamps and payload
lengths (a 0-byte auto-fetch plus the fixed ~13-byte LoRaWAN overhead
produces a very recognizable `len 13 bytes` in the trace, distinct from the
real ~23-byte application payloads) against the code path:

1. A real application send delivers downlink #1 (FPending set) and the
   auto-fetch is queued right behind it, as designed.
2. Because this region enforces a real duty-cycle/LBT gap between
   transmissions (confirmed elsewhere in this project's testing), that
   auto-fetch sits queued for tens of seconds before actually going out.
   The application's *own* next regularly-scheduled send lands during that
   window - exactly the case fix #2 was built for - and gets correctly
   deferred rather than refused.
3. When the auto-fetch's TXDONE arrives, the deferred application send is
   replayed - but replaying it only *queues* it with LBM; LBM's own duty-
   cycle timer still delays *its* actual transmission by a similar amount.
4. Meanwhile, downlink #1's own FPending drain attempt is skipped (the slot
   is occupied by the just-replayed send), so no second auto-fetch gets
   queued for it - matching the "only one downlink per uplink" symptom.
5. The application's *next* send after that lands during the replay's own
   duty-cycle delay - but fix #2 only ever deferred when
   `autoFetchUplinkPending` was specifically true. By this point the thing
   occupying the slot was the *replayed application send*, not the
   auto-fetch, so the old, narrower check refused it outright - producing
   the one remaining "[LOOP] Start sending failed".

In short: fix #2 correctly solved the first collision but only that one:
whatever uplink is in flight - auto-fetch, real send, or a replay of an
earlier deferral - can itself be delayed long enough that the *next* thing
in line collides too, and the "is this specifically the auto-fetch"
distinction fix #2 relied on stops matching reality after the first hop.

### Fix

`send()`'s collision guard no longer checks *why* the uplink slot is
occupied - it now defers unconditionally (returns `true`) whenever a slot
is available in the single-entry `deferredSend` buffer, regardless of
whether the in-flight uplink is the auto-fetch, a real application send, or
an earlier deferred send being replayed. A second `send()` arriving while
one is *already* deferred still gets the original, unconditional refusal -
this remains bounded to one level of deferral, not an unbounded queue.
`SMTC_MODEM_EVENT_TXDONE` handling was simplified to match: it now checks
`deferredSend.valid` unconditionally (previously gated on
`autoFetchUplinkPending`) and replays whatever's waiting, regardless of
what the just-completed uplink was.

`autoFetchUplinkPending` is no longer used to gate deferral at all, but is
kept for one remaining, narrower purpose: `onTxFinished()` now skips the
callback specifically for the auto-fetch's own TX completions (an
application never asked for those empty uplinks and has no reason to hear
about them) while still firing normally for a real send that merely had to
wait its turn through one or more deferrals - a small, genuine improvement
made while already revisiting this code, not a response to a separately
reported symptom.

The auto-fetch's own firing condition (`!uplinkPending`, in the
`SMTC_MODEM_EVENT_DOWNDATA` handler) is unchanged: it still just skips
firing if the slot is busy, rather than using the deferred slot itself.
Real application data always takes priority over this library's own empty
keep-alive traffic for that one slot - worst case, a particular downlink's
prompt fetch is skipped and it waits for the application's own next
regularly-scheduled send instead, which is the original, pre-feature
behavior for that one cycle rather than a regression.

### Files changed

`src/LoRaWANEngine.h` (updated `send()`/`autoFetchUplinkPending`/
`deferredSend` doc comments to describe the unconditional-deferral design),
`src/LoRaWANEngine.cpp` (`send()`'s guard condition widened from
`autoFetchUplinkPending && !deferredSend.valid` to just `!deferredSend.valid`;
`SMTC_MODEM_EVENT_TXDONE`'s replay condition widened to match; added the
`!wasAutoFetch` guard on the `txFinishedCb` call).

### Verification

Comment/string-aware brace-and-paren balance check on both files - zero
imbalance. Re-traced the full event sequence in the newly attached log
against the corrected code path to confirm each collision point (both the
one fix #2 already handled and the new one it didn't) now resolves via
deferral rather than refusal.

**Not done**: not re-verified against real AS923 hardware with a multi-
downlink LNS queue in this environment. Given that fix #2 also looked
complete by the same kind of log-tracing and turned out to only address
the first layer of the collision, this one should likewise be treated as
"reasoned through against the actual reported sequence, generalized rather
than special-cased" rather than a guaranteed final fix until confirmed
against hardware again - particularly worth checking whether an
application whose own send interval is close to this region's minimum
uplink-to-uplink duty-cycle gap can still, in principle, produce a chain of
deferrals long enough to eventually hit the single-slot bound and see one
refusal, since only one level of deferral is held by design.

---

## 2026-09-17 - Two more real bugs found from testing with fetchPendingDownlinks both on and off

Two attached logs, one with the auto-fetch feature on and one off, both
still showing only 1-2 downlinks drained per uplink instead of the full
queue, plus a direct report that manually working around the disabled
auto-fetch ("send an empty uplink myself when fpending is set") wasn't
possible through this library's public API at all. Both turned out to be
real, distinct bugs - not the same collision issue fixed three times
already.

### Bug A: the auto-fetch's own event-ordering assumption was wrong

Every previous fix assumed `SMTC_MODEM_EVENT_TXDONE` for the uplink that
solicited a downlink is always delivered before `SMTC_MODEM_EVENT_DOWNDATA`
for that downlink, within the same `handleEvents()` drain - so by the time
the auto-fetch's `!uplinkPending` check ran, `uplinkPending` would already
be correctly cleared. Comparing the two attached logs line by line disproved
that: in the FPENDING-ON log, the very first downlink's auto-fetch never
fired at all (no "add send task" trace anywhere near it, and no `TX OK` for
that cycle either), while a later downlink's auto-fetch fired correctly -
the *same code path*, behaving two different ways in the same run. That's
only possible if event ordering between TXDONE and DOWNDATA for a given
cycle isn't guaranteed by LBM, and it clearly isn't: whichever one happens
to be delivered first determines whether `uplinkPending` is already false
by the time the auto-fetch's gate runs.

Unlike a real application `send()` (which already had `deferredSend` to
fall back on), a fetch attempt that lost this race was previously just
**dropped silently** - not deferred, not retried, gone. That downlink's
FPending flag would never get acted on again until the application's own
next regularly-scheduled send happened to come along, which is exactly the
"only fetches one, then stops" symptom in both logs.

**Fix**: the auto-fetch now falls back to a remembered request
(`pendingDownlinkFetchRequested` + the port to use) when it can't dispatch
immediately, exactly mirroring `deferredSend`'s existing pattern. It's
serviced from `SMTC_MODEM_EVENT_TXDONE` - checked *after* `deferredSend`, so
real application data still wins the slot if both are waiting - which is
correct regardless of which order TXDONE and DOWNDATA arrived in for the
original cycle, since TXDONE is the one unambiguous signal that the slot is
actually free.

### Bug B: no way to manually send an empty uplink at all

Reported directly: with the auto-fetch disabled (`fetchPendingDownlinks =
false`), trying to replicate it manually - send an empty uplink to pull the
next downlink - wasn't possible through `send()`, which rejected `length ==
0` outright with no way around it. Also reported: FPort 0 specifically
threw an error.

**Fix**: `send()` no longer rejects `length == 0` - a 0-length uplink is a
legitimate LoRaWAN operation (it's exactly what this library's own
auto-fetch has been sending internally all along), and a `nullptr` `data`
pointer is now accepted specifically when `length == 0` (substituted
internally with a real, unused buffer, since `smtc_modem_request_uplink()`
itself still rejects a literal NULL even at length 0) so callers don't need
to keep a dummy buffer of their own around just to send nothing. `AT+SEND=
1:` (empty hex after the colon) now works for the same reason -
`parseHex()` already handled a zero-length request correctly; only `send()`
itself was refusing it.

FPort 0 specifically **still fails**, and correctly so - that's LoRaWAN's
own reserved-for-MAC-only-frames rule, enforced by
`smtc_modem_send_tx()` itself, not a restriction this library was imposing.
Documented directly in `send()`'s doc comment: use any other valid FPort
(e.g. whatever the application normally sends on) for a manual empty
"fetch the next pending downlink" uplink instead of FPort 0.

### Files changed

`src/LoRaWANEngine.h` (new `pendingDownlinkFetchRequested`/
`pendingDownlinkFetchPort` members), `src/LoRaWANEngine.cpp` (`send()`'s
length/data guard relaxed; the `SMTC_MODEM_EVENT_DOWNDATA` auto-fetch now
falls back to the remembered-request path instead of dropping the fetch;
`SMTC_MODEM_EVENT_TXDONE` services that remembered request, after
`deferredSend`, when nothing real is waiting there), `README.md` (AT+SEND
row updated to mention empty-payload support).

### Verification

Comment/string-aware brace-and-paren balance check across all three edited
files - zero imbalance. Re-traced both attached logs' full event sequences
against the corrected code paths: the FPENDING-ON log's first downlink
(previously silently dropped) now resolves via the TXDONE-serviced fallback
regardless of arrival order; confirmed `AT+SEND=<port>:` (empty hex) reaches
`send()` with `length == 0` via `parseHex()`'s already-correct zero-length
handling.

**Not done**: not re-verified against real AS923 hardware with a multi-
downlink LNS queue in this environment. This is the fourth fix attempt at
this same underlying feature; given the previous two also looked complete
by the same kind of log-tracing and each turned out to have a further gap,
this one should be tested with particular attention to whether the full
4-packet queue now drains in one pass, not assumed fixed from source review
alone.

---

## 2026-09-17 - The actual root cause of the entire FPending-drain saga: stale background-task scheduling

### The bug, as reported

Two fresh logs - AS923-3 on ChirpStack, AU915 on The Things Network -
still showed exactly one downlink drained per uplink, no better (arguably
worse-looking) than before four rounds of fixes to the queueing/collision
logic. "Getting worse."

### Root cause - not a queueing bug at all

Every fix so far (fixes #1 through #4 above) focused on making sure a
follow-up uplink actually gets *queued* with LBM when it should be - and
each one was solving a real problem in that area. But re-reading these two
new logs against the actual dispatch code exposed something none of them
touched: the auto-fetch/deferred uplink *was* being queued correctly (the
`INFO: add send task` trace confirms it, right when expected) - it just
then sat there, un-acted-on, for **~50-60 seconds** before LBM actually
transmitted it. That delay is suspiciously uniform across two different
regions and two different network servers, which doesn't fit a duty-cycle
or LBT explanation (AU915 in particular has no such restriction).

The actual cause: `LoRaWANEngine::handleEvents()` calls
`smtc_modem_run_engine()` once, at the very top, and returns that value
(`sleep_time_ms`) unconditionally at the end - "how long until I must be
called again," per LBM's own documented contract. But everything this
function does in between - including every `dispatchUplink()` call the
FPending auto-fetch, a deferred send replay, or a deferred fetch replay can
make - happens *after* that value was already computed. Queuing a brand
new uplink during event processing doesn't retroactively update it; the
stale, pre-queueing value is what gets returned regardless.

In loop()-polled mode this is harmless, because the very next `loop()`
iteration calls `handleEvents()` (and therefore `smtc_modem_run_engine()`)
again almost immediately regardless of what was returned. But every log in
this entire investigation has shown `[LoRaWAN] Background task active` at
startup - meaning `WisBlockLbmTask`'s FreeRTOS event task is in play, and
*that* task uses this exact return value as how long to sleep before
calling `smtc_modem_run_engine()` again (see `eventTask()` in
`wisblock_lbm_task.cpp`). Whatever LBM's own idle/maintenance interval
happened to be at the top of this call - unrelated to anything this
function itself just queued - is what the background task kept sleeping
for, every single time, no matter how correctly the queueing logic itself
had been fixed. This is why fixes #1 through #4 each looked complete by
source inspection and log-tracing, and each one turned out not to actually
fix the reported symptom: they were all correctly fixing real bugs in
*getting an uplink queued*, but none of them touched *how soon LBM would
actually be told to act on it*, which turned out to be the actual bottleneck
the whole time.

### Fix

`handleEvents()` now calls `smtc_modem_run_engine()` a second time, after
its event-draining loop has finished (and therefore after any
`dispatchUplink()` calls that loop's own handling made), and returns *that*
value instead of the stale one from the top of the function. This is
exactly the "must be called again within its last reported budget"
contract `smtc_modem_run_engine()` already documents - just invoked
proactively, right after this function's own actions could have changed
what that budget should be, rather than only reactively on the next
external wake-up.

### Files changed

`src/LoRaWANEngine.cpp` only - one additional `smtc_modem_run_engine()`
call at the end of `handleEvents()`, replacing the stale value it returns.

### Verification

Comment/string-aware brace-and-paren balance check - zero imbalance. Traced
`nextWaitMs = registeredEventHandler();` in `wisblock_lbm_task.cpp` to
confirm `handleEvents()`'s return value is used directly as the FreeRTOS
task's sleep duration, and confirmed every log across this entire
investigation has shown background task mode active, explaining why the
same ~50-60s delay pattern was consistent across every fix attempt and
every region/LNS combination tested.

**Why this one is different from the previous four**: fixes #1-4 were
verified by tracing whether an uplink *got queued* at the right moment,
which was always true after each of those fixes - the queueing logic was
never actually broken in the way each fix assumed. This fix instead traces
what happens *after* queueing succeeds, which is the piece none of the
previous diagnosis looked at. That said, given the track record on this
feature, this should still be verified against real hardware rather than
assumed correct from source review alone - if the queue still doesn't drain
promptly after this, the next thing to check would be whether
`enableBackgroundTask()`'s DIO1-IRQ-driven wake path (the other way the
event task can wake up before its timeout, per `eventTask()`) is also
correctly triggered by a freshly-queued uplink, independent of this
timeout-based path.

---

## 2026-09-17 - RUI3-compatible AT+JOIN parameters: auto-join, reattempt interval, max attempts

### Confirmation

The background-task-scheduling fix from the previous entry was confirmed
working against real hardware: `as923-3-fpending.txt` (AS923-3, presumably
against ChirpStack given the region label, though the log header says
AU915 - region printed as "AU915" but device/channel plan matches AS923's
918 MHz range, likely a copy/paste label mismatch in the log's own header
comment, not a functional issue) and `AU915-fpending.txt` both show a full
downlink queue (7-8 packets) draining in rapid back-to-back uplinks once
`fetchPendingDownlinks` is on, exactly as intended. This closes out the
FPending-drain investigation.

### This feature

RUI3's `AT+JOIN=w:x:y:z` / `api.lorawan.join` support four things this
library's own join handling didn't distinguish before: joining right now
(unchanged), auto-joining on power-up instead of waiting for an explicit
command, a configurable interval between retry attempts, and a cap on how
many times to retry before giving up.

### Design decisions and why

**Auto-join** (`autoJoin`, `AT+JOIN`'s `x` parameter): checked in
`WisBlockLoRaWAN::ensureLoRaWANEngineStarted()` - the one unambiguous point
where the LoRaWAN engine actually transitions from not-started to started,
regardless of which public setter happened to trigger it first
(`setWorkMode`, `setRegion`, `setOTAAKeys`, an explicit `join()`, ...).
Hooking in there avoids duplicating the check across every one of those
call sites.

**Reattempt interval and max attempts** (`AT+JOIN`'s `y`/`z`) needed more
thought than a simple stored value, because LBM's own join task
(`lorawan_join_management.c`) already retries automatically, forever, on
every failure - using its own region-appropriate, spec-compliant backoff
timing (confirmed by reading `lorawan_join_internal_add_task()`: every
`SMTC_MODEM_EVENT_JOINFAIL` reschedules itself via
`lorawan_api_next_join_time_second_get()` unless certification/bypass mode
is active). That backoff exists for real regulatory reasons and isn't
something this library should shorten or bypass wholesale - LBM does
expose `smtc_modem_set_join_duty_cycle_backoff_bypass()`, but its own doc
comment ties it explicitly to certification mode, not general application
use, so it was deliberately not used here.

A longer, fixed interval - exactly what "extend the time between attempts
to save battery" (the user's own stated motivation) asks for - is a
legitimate choice to make *instead of* LBM's own schedule, though, so:
when both settings are left at their defaults (8s interval matching RUI3's
own default, 0 = unlimited attempts), this library's behavior is
unchanged - LBM's automatic retry runs exactly as it always has. As soon as
either is set to something else, this library cancels LBM's own
auto-scheduled next attempt (`smtc_modem_leave_network()` - its own doc
comment: "...or cancels an ongoing join process") on every
`SMTC_MODEM_EVENT_JOINFAIL` and drives retries itself instead, via a
`millis()`-based deadline checked in `handleEvents()`.

Max attempts also uses `smtc_modem_leave_network()` to actually stop
retrying once the limit is hit, and reports a new terminal state,
`WISBLOCK_JOIN_GAVE_UP`, from `joinState()` - deliberately *not* by
changing `joinFailedCb()`'s firing behavior (which still fires on every
individual failed attempt, as it always has) to avoid silently changing
behavior for any existing application already relying on that callback.
An application that cares about the distinction checks `joinState()` from
inside its existing callback.

**The background-task scheduling lesson from the previous fix applied
directly here too**: the custom retry timer in `handleEvents()` clamps its
own returned sleep duration to the remaining wait time whenever a retry is
pending, for exactly the same reason the FPending auto-fetch needed it -
otherwise a background-task-mode application could sleep straight past its
own scheduled retry deadline.

### Files changed

`src/WisBlockLoRaWANTypes.h` (new `autoJoin`/`joinReattemptIntervalS`/
`maxJoinAttempts` settings; new `WISBLOCK_JOIN_GAVE_UP` state),
`src/LoRaWANEngine.h`/`.cpp` (the mechanism itself: `join()` resets the
attempt counter, `stopJoin()` is new, `SMTC_MODEM_EVENT_JOINFAIL` now
counts attempts and only overrides LBM's own scheduling when asked to,
`handleEvents()` services and clamps around the custom retry deadline;
needed a new `<Arduino.h>` include for `millis()`, not previously used in
this file), `src/WisBlockLoRaWAN.h`/`.cpp` (matching setters/getters,
`stopJoin()`, the `ensureLoRaWANEngineStarted()` auto-join hook),
`src/WisBlockLoRaAT.cpp` (`AT+JOIN=w:x:y:z` / `AT+JOIN=?`, alongside the
existing bare `AT+JOIN`), `README.md`.

### Verification

Comment/string-aware brace-and-paren balance check across all six edited
files - zero imbalance. Confirmed via `lorawan_join_management.c` and
`smtc_modem_leave_network()`'s implementation (`modem_supervisor_abort_tasks_in_range()`
+ `lorawan_api_join_status_clear()`) that it correctly cancels a pending
auto-scheduled join task rather than just resetting join *state* without
touching the scheduled task.

**Not done**: not tested against real hardware in this environment - in
particular, the custom reattempt-interval and max-attempts paths (the
default-value path exercises the same code as before, which the FPending
investigation already validated live, but the *non-default* path -
`smtc_modem_leave_network()` mid-retry-cycle plus the `millis()`-driven
re-join - has not been.

---

## 2026-09-18 - Library versioning, AT+VER, and an AT+ALIAS setter

Working from a new upload of the library with the user's own additional
RUI3-compatible AT commands already merged in.

### This change

1. A real, single-source-of-truth version number for this library, distinct
   from RUI3's own firmware version (`AT+VER` mirrors RUI3's *format*, not
   a claim to *be* RUI3 firmware - this library is AT-compatible with RUI3,
   not a redistribution of it).
2. `AT+VER=?` now builds its response from that version instead of a
   hardcoded `"1.0.0"` literal, so bumping a release means changing three
   numbers in one place rather than hunting down every place the string
   was typed out.
3. `AT+ALIAS` gained a real setter - it was previously a hardcoded,
   read-only board-name string with no way to actually change it, despite
   RUI3's own `AT+ALIAS` being fully get/set (a persisted, user-chosen
   16-character device label).

### Design decisions

**Version location**: put the three numeric macros
(`WISBLOCK_LORAWAN_VERSION_MAJOR/MINOR/PATCH`) and a derived
`WISBLOCK_LORAWAN_VERSION_STRING` (built via the standard stringify-macro
trick, so the string can never drift out of sync with the numbers) in
`WisBlockLoRaWAN_all.h`, as asked. Since `WisBlockLoRaAT.cpp` (where
`AT+VER` lives) didn't previously include that header - only
`WisBlockLoRaAT.h` -> `WisBlockLoRaWAN.h` - it now also includes
`WisBlockLoRaWAN_all.h` directly. That's a normal one-way `.cpp`-includes-
a-convenience-header dependency, not a circular one: `WisBlockLoRaWAN_all.h`
itself only ever includes *headers*, and a `.cpp` including it doesn't feed
back into anything that header depends on.

**AT+ALIAS's length limit**: RUI3's own docs say `AT_PARAM_ERROR` is
returned for a malformed/oversized value, not that it gets silently
truncated - so `WisBlockLoRaWAN::setAlias()` rejects (returns false, no
change made) a NULL pointer or a string over 16 characters, and the AT
handler reports `AT_PARAM_ERROR` for that case, matching RUI3's documented
behavior rather than quietly cutting a long value short.

**The existing hardcoded default text didn't fit RUI3's own 16-character
limit** (`"WISBLOCK_BASICMODEM_RAK4631"` is 28 characters) - rather than
silently shortening text the user might already be relying on for
identification purposes, the persisted `alias` field's buffer was sized to
comfortably hold the existing longer defaults (32 bytes), and the 16-
character limit is enforced only on values coming in through the new
setter/AT command. The factory-default text itself was left completely
unchanged.

**Config version bump**: adding a new field to `WisBlockPersistedConfig`
changes its size and layout. The existing CRC check would likely have
caught this on its own (an old, shorter saved blob won't produce a matching
CRC against the new, larger struct), but `WISBLOCK_CONFIG_VERSION` was
bumped anyway (1 -> 2) to make the incompatibility with configs saved by
an older library version explicit and intentional rather than incidental.
Either way, an old saved config is safely detected as invalid and replaced
with factory defaults (`wisblockConfigLoad()`'s existing fallback path) -
never misinterpreted.

**A small adjacent gap fixed while already touching this code**: `AT+VER`
and the alias factory-default only ever handled `NRF52_SERIES` (RAK4631)
and `ARDUINO_ARCH_ESP32` (RAK3312), despite this library also supporting
RAK11310 (`ARDUINO_ARCH_RP2040`) elsewhere (see `WisBlockLoRaBoards.h`'s
own `WISBLOCK_BOARD_NAME` for that board). Added the missing third branch
to both rather than leaving it as a gap in code being edited for this
exact purpose anyway - flagged here since it wasn't explicitly requested.

### Files changed

`src/WisBlockLoRaWAN_all.h` (version macros), `src/WisBlockLoRaAT.cpp`
(`AT+VER=?` uses the version macro plus the new RP2040 branch; `AT+ALIAS=`
setter added), `src/WisBlockLoRaWAN.h`/`.cpp` (`setAlias()`/`getAlias()`),
`src/WisBlockLoRaWANConfig.h` (new persisted `alias` field, RP2040 default,
`WISBLOCK_CONFIG_VERSION` bump), `README.md` (new AT+VER/AT+ALIAS rows -
neither was previously documented there even though both already existed).

### Verification

Compiled the exact stringify-and-concatenate macro pattern standalone with
`gcc` to confirm `"RUI_comp_" WISBLOCK_LORAWAN_VERSION_STRING "_RAK4631"`
resolves to the same literal string (`"RUI_comp_1.0.0_RAK4631"`) the old
hardcoded version produced, rather than assuming the macro mechanics were
correct. Comment/string-aware brace-and-paren balance check across all five
edited files - zero imbalance. Re-normalized `README.md`'s line endings
after editing it (a `str_replace` on this CRLF file had left a couple of
lines with a bare `\n`), rather than leaving a mixed-line-ending file.

**Not done**: not tested against real hardware or an actual AT-command
session in this environment.

---

## 2026-09-20 - LBT (Listen Before Talk): checked, and exposed via API/AT commands

### The check the user asked for

Traced whether the vendored LBM stack actually implements LBT at all, and
whether this library exposed any control over it.

**LBM itself: fully implemented already, nothing missing at the radio/MAC
layer.** `smtc_modem_lbt_set_state()`/`_get_state()` and
`_set_parameters()`/`_get_parameters()` are all present and complete in
`smtc_modem_api.h`, backed by a real sniff-before-transmit implementation
(`smtc_lbt.c`) hooked into the radio planner. Per `smtc_modem_lbt_set_state()`'s
own doc comment, LBT is "silently enabled if the feature is mandatory in a
region selected with smtc_modem_set_region" - and CSMA
(`smtc_modem_csma_*`) is the equivalent silent default for regions where
LBT isn't mandatory. This matches what's already been visible in this
project's own test logs (`modem_tpm_radio_busy_lbt` traces have appeared
in earlier AS923 sessions) - LBT-class channel-access behavior has been
active in testing already, without this library doing anything to enable
it.

**What was actually missing: this library's own public API/AT command
surface.** There was no way for an application - or an AT-command user -
to see LBT's status, turn it on/off explicitly, or adjust its RSSI
threshold or scan duration. RUI3 exposes exactly this via `AT+LBT` /
`AT+LBTRSSI` / `AT+LBTSCANTIME` ("support Korea, Japan"); this library had
no equivalent at all.

**A finding worth knowing, not itself a bug**: every region in this
vendored LBM tree defines its own "region-appropriate" LBT threshold
constant (`LBT_THRESHOLD_DBM_KR_920`, `..._AS_923`, etc.) - but none of
them are ever actually read by anything else in LBM (confirmed: no caller
anywhere for `smtc_real_get_lbt_threshold_dbm()`, the one function that
would read them), and every single one of those constants is -80 dBm
anyway - identical to `smtc_lbt_init()`'s own generic fallback (two of
them, CN470's variants, are even marked `// TODO value must be checked` in
Semtech's own source). So this has no practical effect today: selecting
KR920 or a Japan-targeting AS923 variant does not itself apply any
region-tuned LBT threshold - whatever the generic default is (or whatever
the new API/AT calls set) is what actually gets used, regardless of
region. Deliberately did not invent a "corrected" per-region default to
apply automatically here - the actual regulatory-correct threshold for a
given deployment is a certification question this library has no business
guessing at silently; exposing the control is the right scope for this
change, and it's now available via the API/AT commands added.

### What was added

`LoRaWANEngine::setLbtEnabled()`/`getLbtEnabled()`,
`setLbtThreshold()`/`getLbtThreshold()`, `setLbtScanTime()`/`getLbtScanTime()`
(matching `WisBlockLoRaWAN` wrappers, following this library's established
pattern of `ensureLoRaWANEngineStarted()` before a state-changing call).
Threshold and scan time share one underlying LBM call
(`smtc_modem_lbt_set_parameters()`, which also takes an RSSI measurement
bandwidth this library doesn't expose - RUI3 doesn't either) - each setter
reads the current values back from LBM first rather than risking
clobbering the other one with a stale cached value. `AT+LBT=0/1` /
`AT+LBT=?`, `AT+LBTRSSI=<dBm>` / `AT+LBTRSSI=?`, and
`AT+LBTSCANTIME=<ms>` / `AT+LBTSCANTIME=?` added to the AT layer, matching
this library's existing convention of implementing the `=?` (get) and
`=value` (set) forms without a bare-`?` short-help response, since no
other command in this AT set implements that form either.

### Files changed

`src/LoRaWANEngine.h`/`.cpp` (the six new methods),
`src/WisBlockLoRaWAN.h` (matching wrappers), `src/WisBlockLoRaAT.cpp`
(three new AT commands), `README.md`.

### Verification

Confirmed via `grep` across the whole vendored LBM tree that
`smtc_real_get_lbt_threshold_dbm()` genuinely has zero callers (not just
skimmed past) before relying on that as the basis for the "region
selection doesn't apply a tuned threshold" finding above, and confirmed
each region's own threshold constant's actual value rather than assuming
they differ from the generic default. Comment/string-aware brace-and-paren
balance check across all four edited files - zero imbalance. Cross-checked
every `smtc_modem_lbt_*` call's parameter types/order directly against
their declarations in `smtc_modem_api.h` rather than assuming the
signatures from memory.

**Not done**: not tested against real hardware or an actual AT-command
session in this environment - in particular, whether the RSSI/scan-time
values reported back by `smtc_modem_lbt_get_parameters()` immediately
after a `set` call reflect that same call (rather than some
internally-adjusted value - `smtc_lbt_set_parameters()`'s own source adds
a fixed `LAP_OF_TIME_TO_GET_A_RSSI_VALID` internally to whatever duration
is requested, though `get_parameters()` appears to subtract it back out
symmetrically) has not been confirmed live.

---

## 2026-09-21 - Class B setup parameters: AT+PGSLOT, AT+BFREQ, AT+BTIME

### Scope

RUI3's "Class B Mode" AT command section covers four commands:
`AT+PGSLOT` (ping slot periodicity - a real setup parameter),
`AT+BFREQ`/`AT+BTIME` (read-only beacon status), and `AT+BGW` (read-only
gateway GPS/NetID/GwID, decoded from the beacon's GwSpecific field).
Implemented the first three; `AT+BGW` was deliberately left out - see
below for why.

### What each one needed

**AT+PGSLOT** maps directly onto an LBM API that already exists and
already uses the identical 0-7 numbering RUI3 does
(`smtc_modem_class_b_set/get_ping_slot_periodicity()`,
`smtc_modem_class_b_ping_slot_periodicity_t`'s enum order runs 1s...128s
matching RUI3's own documented periodicity table exactly). Setting it also
triggers a `PingSlotInfoReq` MAC command
(`SMTC_MODEM_LORAWAN_MAC_REQ_PING_SLOT_INFO`) - without that, the change
would only affect this device's own local scheduling while the network
kept scheduling downlinks for the old periodicity, breaking Class B
reception rather than being a harmless no-op. Persisted like every other
setting in this library (`WisBlockLoRaWANSettings::pingSlotPeriodicity`,
pushed on every `applySettings()` - harmless for Class A/C, and means it's
already correct the moment an application does switch to Class B).

**AT+BFREQ/AT+BTIME** needed more digging: LBM has the right low-level
primitives (`smtc_real_get_beacon_dr()`/`_get_beacon_frequency()`, and the
beacon object's own `beacon_epoch_time` field, updated from the actual
time value embedded in the last received beacon - not this device's local
clock), but nothing in the existing public `smtc_modem_api`/`lorawan_api`
surface exposed them to a caller above `lorawan_api.c`. Added three small
getters there (`lorawan_api_get_beacon_epoch_time/_dr/_frequency`),
following the same pattern used for the sub-band channel mask feature
earlier in this project - reach one layer into the internals only as far
as needed, expose a narrow getter, keep everything else untouched.
`smtc_real_get_beacon_frequency()` needs a reference GPS time (some
regions, e.g. US915/AU915, hop the beacon frequency over time) - the last
received beacon's own embedded time is the correct value to use there.

### AT+BGW - not implemented

The beacon's GwSpecific field (which AT+BGW reports) needs decoding via
`smtc_decode_beacon_gw_specific()`, which requires the beacon's spreading
factor at reception time to compute a correct byte offset into the raw
beacon payload - not something already cleanly surfaced alongside the
stored beacon buffer the way epoch time/DR/frequency were. Getting a
byte-offset or InfoDesc-interpretation (GPS coordinates vs. NetID+GwID)
detail wrong here would produce a plausible-looking but incorrect answer
with no live Class B beacon available in this environment to check it
against - unlike the other three, this one couldn't be verified as
"clearly correct by construction" (a direct, obviously-right pass-through
of an existing, unambiguous LBM value). Flagging this as a known gap
rather than shipping something unverifiable.

### Files changed

`src/lbm/smtc_modem_core/lorawan_api/lorawan_api.h`/`.c` (three new
getters), `src/LoRaWANEngine.h`/`.cpp` (`setPingSlotPeriodicity()`/
`getPingSlotPeriodicity()`, `getBeaconFrequencyAndDr()`, `getBeaconTime()`,
plus the `applySettings()` wiring), `src/WisBlockLoRaWAN.h`/`.cpp`
(matching wrappers, `pingSlotPeriodicity` config sync following the exact
`setChannelMask()` pattern), `src/WisBlockLoRaWANTypes.h` (new persisted
`pingSlotPeriodicity` field), `src/WisBlockLoRaAT.cpp` (`AT+PGSLOT`,
`AT+BFREQ=?`, `AT+BTIME=?`), `README.md`.

### Verification

`gcc -fsyntax-only` on the modified `lorawan_api.c` against the project's
real build flags (all region defines, `ADD_CLASS_B`) - zero errors.
Comment/string-aware brace-and-paren balance check across all six edited
`.h`/`.cpp` files - zero imbalance. Cross-checked
`smtc_modem_class_b_ping_slot_periodicity_t`'s enum values against RUI3's
documented AT+PGSLOT periodicity table entry-by-entry to confirm the
numbering genuinely matches rather than assuming it from the names alone.

**Not done**: not tested against real hardware, and specifically not
against a live Class B beacon (`AT+BFREQ`/`AT+BTIME` will correctly report
0/region-default values until at least one beacon has actually been
received - this hasn't been confirmed against a real network in this
environment). `AT+BGW` remains unimplemented, as above.

## 2026-09-22 - AT command dispatch rewritten as a RUI3-style lookup table; custom AT command API added; cleanup pass

### Scope

Three changes to `src/WisBlockLoRaAT.h`/`.cpp`, all requested together:

1. **Lookup-table dispatch.** `processLine()` used to be a single
   ~900-line `if`/`else if` chain of `strcmp()`/`startsWith()` calls, one
   pair (or more, for get/set) per AT command. Replaced with the same
   shape RUI3's own AT command core uses
   (`cores/nRF5/component/service/mode/cli/atcmd.c`'s `atcmd_info_tbl[]`
   in `RAKWireless/RAK-nRF52-RUI`): a `static const AtCommandEntry
   atCommandTable[]` mapping each command name to a private handler
   method, looked up in a single loop. `processLine()` now does the
   string work exactly once per line - splitting into a bare command name
   plus an `AtOp` (`Run` / `Query` / `Write`, matching the old
   bare-`AT+CMD` / `AT+CMD=?` / `AT+CMD=value` forms) - and hands each
   handler pre-parsed `(AtOp op, const char *value)` instead of making
   every handler independently re-derive its own operation from raw
   string comparisons. One handler method per command (get/set combined,
   e.g. `atNwm()` covers both `AT+NWM=?` and `AT+NWM=1`), 44 methods for
   45 table rows (`+APPEUI`/`+JOINEUI` share `atAppEui()`, same alias the
   old code had). Every handler's actual logic is otherwise a direct,
   line-for-line port of its old `if`/`else if` branch - see "Cleanup"
   below for the handful of places where it isn't.

2. **Custom AT command API**, `WisBlockLoRaAT::addCustomATCommand(cmd,
   usage, handler)` - the same idea as RUI3's `api.system.atMode.add()`
   (`RAKSystem.h`'s `atMode` class), adapted to this library's plain-
   C-string style instead of RUI3's colon-split `stParam`/`argv`. Custom
   commands are registered by base name (e.g. `"LED"`) and dispatched as
   `ATC+LED` / `ATC+LED=?` / `ATC+LED=value` - `"C+"` is RUI3's own prefix
   convention for its custom commands, reused here rather than inventing
   a different one. A handler is a plain function pointer,
   `WisBlockAtStatus (*)(Stream &port, const char *cmd, char *args)`:
   `cmd` is the full command as typed (so one handler function can serve
   several registered names, same as RUI3's `pfHandle(port, cmd, param)`);
   `args` is `nullptr` bare, `"?"` for a query, or the raw text after `=`
   for a set (unsplit - multi-field custom commands parse their own
   colon/comma-separated `args`, the same way this library's own
   `+JOIN=`/`+P2P=`/`+PRECVDC=` handlers already did before this change).
   Returning `WISBLOCK_AT_OK`/`WISBLOCK_AT_ERROR`/`WISBLOCK_AT_PARAM_ERROR`
   maps onto the same `OK`/`AT_ERROR`/`AT_PARAM_ERROR` replies a built-in
   command failure produces. Up to `MAX_CUSTOM_AT_COMMANDS` (16) commands,
   fixed-size table on the `WisBlockLoRaAT` instance (no dynamic
   allocation, consistent with the rest of this library). Matched
   case-insensitively via `strcasecmp()`, same as every built-in command
   (the whole line is already uppercased before any dispatch happens -
   see `processLine()`'s doc comment, unchanged from before this pass).

3. **Cleanup**, found while doing the above and fixed rather than ported
   forward as-is (each is called out with a `CLEANUP:` comment at its
   site in the new code):
   - `AT+CLASS=?` sent **two** `OK\r\n` replies for one query - it called
     `reply()` (which itself already prints a trailing `OK`) and then
     `replyOk()` again right after. Now sends one.
   - `AT+APPKEY=?`/`AT+NWKSKEY=?`/`AT+APPSKEY=?` each had a `SECURITY:`
     comment explicitly stating the key is "deliberately not read back in
     plaintext" - immediately followed by code that printed the raw key
     in hex anyway (the `isSet` boolean it computed was dead code, and
     the masked `"SET"`/`"UNSET"` reply was written but commented out).
     Fixed to actually mask the key as the existing comment says it
     should, restoring the commented-out `"SET"`/`"UNSET"` reply and
     deleting the plaintext `printHex()` call. This is a real change in
     wire behavior for anyone currently relying on plaintext key
     readback; if that's actually wanted, the fix is documented inline
     (revert those two lines to `printHex(port, ...)`).
   - `AT+NWKSKEY=?` echoed its own tag back as `"AT+NWSKEY="` - missing
     the `K` - which didn't match the command's own name. Fixed to
     `"AT+NWKSKEY="`.
   - `AT+TIMEREQ` printed a dangling `"AT+TIMEREQ="` with no value or
     newline before `replyOk()`'s `"OK\r\n"` landed right after it,
     producing one malformed `"AT+TIMEREQ=OK\r\n"` line instead of a
     clean `"OK\r\n"`. The actual result only ever arrives asynchronously
     (there was never a synchronous value to print here) - the dangling
     `printf()` was removed.
   - `AT+HWMODEL=?`/`AT+HWID=?` were missing an `ARDUINO_ARCH_RP2040`
     branch in their platform `#ifdef` chains (present for `AT+VER=?`,
     and this library otherwise supports RAK11310/RP2040 throughout -
     see `WisBlockLoRaBoards.h`'s `WISBLOCK_BOARD_NAME`) - an RP2040
     build printed nothing for either before replying `OK`. Added
     `"rak11310"`/`"rp2040"` branches for parity with the other two
     platforms.
   - `AT+SN=?` had the same missing-RP2040-branch gap, worse here: `id[]`
     was read by `printHex()` without ever being written on that
     platform, so it printed 8 bytes of uninitialized stack memory. Added
     an RP2040 branch using `pico_get_unique_board_id()`
     (`pico/unique_id.h`, part of the arduino-pico core) - the documented
     RP2040 equivalent of the nRF52 FICR reads / ESP32 eFuse MAC read
     already used for the other two platforms. **Not build- or
     hardware-tested** - no RP2040 toolchain available in this
     environment; please verify on real RAK11310 hardware before relying
     on it.
   No other behavioral changes were made; every other handler's logic
   (including some other odd-looking-but-intentional choices, like
   `AT+APPKEY=`'s 16-byte `parseHex()` or `AT+MASK=`'s hex-vs-decimal
   parsing) was ported as-is.

### Files changed

`src/WisBlockLoRaAT.h` (new `AtOp` enum, `AtCommandEntry`/
`CustomAtCommandEntry` structs, `CustomAtHandler` typedef,
`WisBlockAtStatus` enum, `addCustomATCommand()` declaration, one handler
method declaration per built-in command), `src/WisBlockLoRaAT.cpp` (full
rewrite of the dispatch and every handler as described above; helper
functions in the anonymous namespace - `startsWith()`,
`startsWithAtCaseInsensitive()`, `printHex()`, `bandIndexToRegion()`,
`regionToBandIndex()` - and the background-RX/USB-CDC section at the
bottom are unchanged).

### Verification

`g++ -fsyntax-only -std=c++17` against a hand-written stub of
`WisBlockLoRaWAN.h`'s full public API (every method/enum/struct this file
touches, matching the real header's signatures field-for-field) - zero
errors, zero warnings. Confirmed every method declared in the header has
exactly one definition in the `.cpp` and that every `atCommandTable[]`
entry points at a declared handler (scripted cross-check, not just visual
inspection). Comment/string-aware brace-and-paren balance check on both
files - zero imbalance (298/298 braces, 926/926 parens in the `.cpp`).
Manually traced each of the 45 old `if`/`else if` branches against its
new handler method to confirm the ported logic is unchanged apart from
the five cleanup items listed above.

**Not done**: not tested against real hardware or a live AT-command
session in this environment (same limitation as every other entry in
this log). The `AT+SN=?` RP2040 fix in particular is unverified against
actual `pico/unique_id.h` availability/linkage on a real arduino-pico
build - flagged above.

## 2026-09-22 - Revert: AT+APPKEY=?/AT+NWKSKEY=?/AT+APPSKEY=? plaintext readback restored

### Scope

The previous entry above ("AT command dispatch rewritten...") changed
`AT+APPKEY=?`/`AT+NWKSKEY=?`/`AT+APPSKEY=?` to mask the key value
(`"SET"`/`"UNSET"`) instead of printing it in hex, reasoning that the
existing `SECURITY:` comment on each of these ("deliberately not read
back in plaintext") described the intended behavior and the plaintext
`printHex()` call contradicted it.

The maintainer confirmed the plaintext readback is the intentional
behavior - it overrides what the comment says, not the other way around.
Reverted: all three handlers call `printHex()` on the real key again, as
they did before that change. The stale `SECURITY:` wording that prompted
the (incorrect) fix has been replaced with a short note pointing back
here, so a future reader doesn't hit the same false trail.

The unrelated `AT+NWKSKEY=?` tag-typo fix from the same pass (it used to
echo `"AT+NWSKEY="`, missing the `K`) was kept - it has nothing to do
with plaintext-vs-masked and wasn't part of what got flagged.

### Files changed

`src/WisBlockLoRaAT.cpp` (`atAppKey()`, `atNwkSKey()`, `atAppSKey()` -
query branch only; write branches and every other handler untouched).

### Verification

`g++ -fsyntax-only -std=c++17` against the same hand-written
`WisBlockLoRaWAN.h` API stub used to verify the original rewrite - zero
errors. Confirmed no other reference to the removed `isSet` local
remains in the file.
