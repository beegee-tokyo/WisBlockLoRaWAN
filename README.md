# WisBlockLoRaWAN
| <center><img src="./assets/rakstar.jpg" alt="RAKstar" width=50%></center>  | <center><img src="./assets/RAK-Whirls.png" alt="RAKWireless" width=50%></center> | <center><img src="./assets/WisBlock.png" alt="WisBlock" width=50%></center> | <center><img src="./assets/Yin_yang-48x48.png" alt="BeeGee" width=50%></center>  |
| -- | -- | -- | -- |    

This library is created to replace the [SX126x-Arduino](https://github.com/beegee-tokyo/SX126x-Arduino) in the future.     
It includes     
- an API for LoRa P2P communication, including P2P CAD and RX-Duty-Cycle implementation.
- an API to use a LoRaWAN modem stack based on Semtechs [SWL2001 Basic Modem](https://github.com/Lora-net/SWL2001/tree/master) implementation.     
It supports Class A, Class C and Class B (not yet tested), LinkCheck, ServerTime and a full implementation of the [TS001-LoRaWAN L2 1.0.4](https://resources.lora-alliance.org/technical-specifications/ts001-1-0-4-lorawan-l2-1-0-4-specification) and [Regional Parameters RP2-1.0.3](https://resources.lora-alliance.org/technical-specifications/rp2-1-0-3-lorawan-regional-parameters) specifications.     
- an AT command interface and a local storage for device setup, e.g. LoRa P2P settings, LoRaWAN credentials, DevNonces, ...     
- same as with the SX126x-Arduino library, it uses an FreeRTOS task in the background to handle LoRa and LoRaWAN events. This allows to create application with minimum power consumption as there is no need to use the loop() to check for events and keep the LoRa and LoRaWAN engines running.     

_**Not supported**_    
- LoRaWAN FUOTA function is not implemented and is not planned at this time due to the complexity of FUOTA and challenge to implement it in an Arduino library.     
- LoRaWAN Relay function is not implemented and it is not planned at this time. 
- LoRa P2P FSK transmission are not supported yet.

# IMPORTANT
- _**This library is still under testing and development. Many parts are not fully tested and challenged in real world applications, use with caution!**_
- This library was created with support of Claude AI, where the AI was doing the simplification and integration of the SWL2001 Basic Modem source codes into an Arduino Library. The requirement definitions for functionality and testing of the functionality on real devices is done by the author of this repository.    
- _**RAK11300 and RAK11310 support is not yet fully implemented. The original Arduino and PlatformIO BSP's for the RP2040 MCU are based on MBED, which is no longer officially maintained and supported, a different approach will be required for these modules.**_     

## Supported hardware

Built-in, compile-time-selected presets: RAK4631 (nRF52840), RAK3312 (ESP32-S3) and RAK11310 (RP2040, reduced support - see above). `WisBlockLoRaWAN::begin()` with no argument picks one of these automatically from the target architecture, same as before.

On nRF52840 and ESP32-S3, any other board with an SX1262 wired up in a common way can be used too, without a new library file: describe its pins and RF-switch/TCXO wiring with a
`WisBlockLoRaHwConfig` (`src/WisBlockLoRaHwConfig.h`) and pass it to `WisBlockLoRaWAN::begin(const WisBlockLoRaHwConfig&)` instead of plain `begin()`. A RAK3401 (RAK3400 WisDuo module + RAK13300/RAK13302 transceiver) preset is included as a worked example - `wisblockLoRaHwConfigRAK3401()` - see `examples/RAK3401_RAK13300/`. This flexible path is
nRF52840/ESP32-S3 only, for the same FreeRTOS-support reason RAK11310 has reduced support above.

## API documentation

All API commands are documented in the [WisBlockLoRaWAN-API](WisBlockLoRaWAN-API.md) document.    

## AT command set

All AT commands are documented in the [WisBlockLoRaWAN-AT-Commands](WisBlockLoRaWAN-AT-Commands.md) document.

The AT interface follows the RUI3 command set. Commands that only exist in this library (`AT+STATUS`, `AT+FPENDING`, `AT+RXBOOST`, `AT+PRECVDC`, `AT+LOWPOWER`, `AT+SAVE`, `AT+RESTORE`, `AT+FACTORY`, `AT+FIRMWAREVER`) and the RUI3 commands that are not implemented are listed there as well.

## Examples
Multiple examples are available in the examples folder of the library.

### BasicLoRaWAN
Simple LoRaWAN application.     
OTAA join, Class A, periodic uplink on port 1, all LoRaWAN callbacks wired.    
Works unmodified on RAK4631 / RAK3312 / RAK11310 once WisBlockLoRaBoards.h has the right pins for your revision and LBM is vendored in (see README).    
The AT command interface runs on the USB serial port. It is polled from loop() with at_serial.handleSerial(), the same way as lora.handleEvents().

### LowPowerLoRaWAN
Low power example for LoRaWAN. Using semaphores to keep the MCU in low power state unless an action is required.
OTAA join, Class A, periodic uplink on port 1, all LoRaWAN callbacks wired.    
Works unmodified on RAK4631 / RAK3312 / RAK11310 once WisBlockLoRaBoards.h has the right pins for your revision and LBM is vendored in (see README).    
This example shows as well how to add custom AT commands to an application.

### RAK3401_RAK13300
Minimal OTAA join + periodic uplink, Class A, on a RAK3400 WisDuo module used as a WisBlock Core module with a RAK13300 (or RAK13302, same wiring).    
SX1262 transceiver module - i.e. "RAK3401" in the sense this library uses the name (see WisBlockLoRaHwConfig.h).     
This is the same OTAA/join/uplink flow as BasicLoRaWAN.ino, the only difference is this single line in setup():    
```lora.begin(wisblockLoRaHwConfigRAK3401());   // instead of plain lora.begin() ```

Everything else - every other API call, the AT command layer if you add it, the examples under examples/ - works exactly the same regardless of which begin() you call, since it's only the radio wiring that differs.      
**Only builds for nRF52840 targets (RAK3400 is an nRF52840 module)**

###  BasicLoRaP2P
Simple LoRa P2P application.    
Alternates between CAD-gated TX and RX every few seconds.    
The AT command interface runs on the USB serial port. It is polled from loop() with at_serial.handleSerial(), the same way as lora.handleEvents().

### LowPowerLoRaP2P
Low power example for LoRa P2P. Using semaphores to keep the MCU in low power state unless an action is required.     
Works unmodified on RAK4631 / RAK3312 / RAK11310 once WisBlockLoRaBoards.h has the right pins for your revision and LBM is vendored in (see README).

### LoRaP2PPingPong
PingPong example. Requires two devices flashed with the same application.     
Depending on timing of power on, one device will act as a master and send PING messages. The other device will act as a slave and is responding with PONG messages.    

### RX-Duty-LoRaP2P
LoRa P2P example that uses the DutyCycle feature to listen to incoming packets.

### ATCommandInterface
Exposes the full AT command set (see README.md) over USB serial.     
Useful for provisioning devices from a host script/terminal without flashing per-device firmware.    
Demonstrates fully background, non-polled operation on RAK4631/RAK3312:
- lora.enableBackgroundTask()      - LoRaWAN/P2P event handling
- atCommands.enableBackgroundRx()  - AT command processing

## Arduino IDE

The library builds in the Arduino IDE / arduino-cli as well as in PlatformIO. Install it into your `libraries` folder (or from the ZIP) and select a WisBlock board (RAK4631, RAK3112/RAK3312). Notes:

- The Arduino IDE only adds the library's `src/` folder to the include path and cannot pass per-library compiler flags. So every `#include` of a vendored LoRa Basics Modem (LBM) header inside this library is a path relative to the including file, and `src/wb_lbm_config.h` supplies the build flags. The library therefore does **not** depend on the include path or on any other installed library, and never uses headers from other libraries that happen to ship files with the same names. PlatformIO keeps using `extra_script.py`; both work with the same sources.
- After re-vendoring LBM, run `python3 tools/make_lbm_includes_relative.py` once to convert the new files.

## Production flow: factory defaults vs. user config

Two complete configs are kept on flash, in separate slots - a "factory" backup and the regular "user" config (the one AT+SAVE/AT+RESTORE/`saveConfig()`/`restoreConfig()` already worked with before AT+FACTORY/ATR existed):

1. Flash firmware. On a genuinely first boot, nothing is saved yet, so the live config is just the compiled-in defaults (region EU868, mode LoRaWAN, join mode OTAA, and the shared default JoinEUI/AppKey already in `WisBlockOTAAKeys` - see `WisBlockLoRaWANTypes.h`).    
2. Set this unit's real, unique `AT+DEVEUI=` (assigned by the LoRa Alliance) and other required settings, e.g. the LoRaWAN region.    
3. `AT+FACTORY` - snapshots the *current live config* (defaults + changes + this unit's real DevEUI) into the factory slot. This is a one-time production step, not something end users run.     
4. The device resets automatically right after saving.    
5. `ATR` - copies the factory slot over both the live config and the user slot, so the factory state (including the real DevEUI from step 2) becomes what AT+SAVE/AT+RESTORE work with from here on too.

After that, an end user is free to change settings and `AT+SAVE` them as usual. If they end up in a broken state, `ATR` at any time restores exactly what was captured in step 3 - including the correct DevEUI, never the generic compiled-in one.

`AT+FACTORY` and `ATR` are easy to confuse with the already-existing `AT+RESTORE`/`restoreConfig()`     
- `AT+RESTORE` reloads the *user* slot (undoing only unsaved, in-RAM changes since the last `AT+SAVE`); 
- `ATR` reaches further back, to the factory slot from step 3, and overwrites the user slot with it too.
