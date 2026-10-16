# WisBlockLoRaWAN
| <center><img src="./assets/rakstar.jpg" alt="RAKstar" width=50%></center>  | <center><img src="./assets/RAK-Whirls.png" alt="RAKWireless" width=50%></center> | <center><img src="./assets/WisBlock.png" alt="WisBlock" width=50%></center> | <center><img src="./assets/Yin_yang-48x48.png" alt="BeeGee" width=50%></center>  |
| -- | -- | -- | -- |    

This library is created to replace the [SX126x-Arduino](https://github.com/beegee-tokyo/SX126x-Arduino) in the future.     
## Supported    
- an API for LoRa P2P and FSK P2P communication, including LoRa P2P CAD and RX-Duty-Cycle implementation.
- an API to use a LoRaWAN modem stack based on Semtechs [SWL2001 Basic Modem](https://github.com/Lora-net/SWL2001/tree/master) implementation.     
It supports Class A, Class C and Class B (not yet tested), LinkCheck, ServerTime and a full implementation of the [TS001-LoRaWAN L2 1.0.4](https://resources.lora-alliance.org/technical-specifications/ts001-1-0-4-lorawan-l2-1-0-4-specification) and [Regional Parameters RP2-1.0.3](https://resources.lora-alliance.org/technical-specifications/rp2-1-0-3-lorawan-regional-parameters) specifications.     
- an AT command interface and a local storage for device setup, e.g. LoRa P2P settings, LoRaWAN credentials, DevNonces, ...     
- same as with the SX126x-Arduino library, it uses an FreeRTOS task in the background to handle LoRa and LoRaWAN events. This allows to create application with minimum power consumption as there is no need to use the loop() to check for events and keep the LoRa and LoRaWAN engines running.     

## Not supported
- LoRaWAN FUOTA function is not implemented and is not planned at this time due to the complexity of FUOTA and challenge to implement it in an Arduino library.     
- LoRaWAN Relay function is not implemented and it is not planned at this time. 

# ⚠️ IMPORTANT
- _**This library is still under testing and development. Many parts are not fully tested and challenged in real world applications, use with caution!**_
- This library was created with support of Claude AI, where the AI was doing the simplification and integration of the SWL2001 Basic Modem source codes into an Arduino Library. The requirement definitions for functionality and testing of the functionality on real devices is done by the author of this repository.    

_**RAK11300 and RAK11310 is only supported using the BSP from Earle F. Philhower, which supports FreeRTOS. The original Arduino and PlatformIO BSP's for the RP2040 MCU are based on MBED, which is no longer officially maintained and supported, therefor it is not supported by this library.**_     
Current workaround for RAK11300/RAK11310
- With Arduino IDE use Earle F. Philhower BSP for RP2040 ==> [arduino-pico](https://github.com/earlephilhower/arduino-pico)
- With PlatformIO use [platform-raspberrypi](https://github.com/maxgerhardt/platform-raspberrypi)
- Enable FreeRTOS support in Arduino BSP tools or for PIO in platformio.ini with _**`build_flags = -D PIO_FRAMEWORK_ARDUINO_ENABLE_FREERTOS`**_

If firmware upload does not work with RAK11300/RAK11310 on Windows, change driver with ZADIG:    
**Install the driver**
   - Put the board in BOOTSEL mode.
   - Start Zadig and enable Options → List All Devices.
   - Select "RP2 Boot (Interface 1)". _**Do not pick the mass-storage interface.**_
   - Install the WinUSB driver.

After that, picotool uploads and the automatic reset into BOOTSEL both work.

## Supported hardware

Built-in, compile-time-selected presets: RAK4631 (nRF52840), RAK3312 (ESP32-S3) and RAK11310 (RP2040, _**only with Earle F. Philhower BSP, MBED BSP is not supported**_).       
`WisBlockLoRaWAN::begin()` with no argument picks one of these automatically from the target architecture, same as before.

On nRF52840, ESP32-S3 and RP2040, any other board with an SX1262 wired up in a common way can be used too, without a new library file: describe its pins and RF-switch/TCXO wiring with a
`WisBlockLoRaHwConfig` (`src/WisBlockLoRaHwConfig.h`) and pass it to `WisBlockLoRaWAN::begin(const WisBlockLoRaHwConfig&)` instead of plain `begin()`. A RAK3401 (RAK3400 WisDuo module + RAK13300/RAK13302 transceiver) preset is included as a worked example - `wisblockLoRaHwConfigRAK3401_RAK13300()` (formerly `wisblockLoRaHwConfigRAK3401()`, still available) - see `examples/RAK3401_RAK13300/`. The wiring of a RAK3401 with a RAK13702 (Semtech LR2021) module is described by `wisblockLoRaHwConfigRAK3401_RAK13702()`; the LR2021 backend is **optional and off by default**, see "LR2021 (RAK13702)" below (`examples/RAK3401_RAK13702/`). This flexible path is
nRF52840/ESP32-S3/RP2040 only. Other MCU types are not supported.

### LR2021 (RAK13702) on a RAK3401

Support for the Semtech LR2021 (RAK13702 module) on a RAK3401 is built in, but compiled in only when you ask for it, so builds for the SX1262 modules do not change and do not grow:

- Enable it by removing the `//` in front of `#define WISBLOCK_ENABLE_LR2021` in `src/wb_lbm_config.h`, or in PlatformIO with `build_flags = -DWISBLOCK_ENABLE_LR2021`. It is only available for the nRF52840.
- Start the library with `lora.begin(wisblockLoRaHwConfigRAK3401_RAK13702());` (SX1262 modules: `wisblockLoRaHwConfigRAK3401_RAK13300()`). The radio is selected at run time from the `chipType` of that config, both LoRaWAN and LoRa/FSK P2P work with it. See `examples/RAK3401_RAK13702/`.
- **Not tested on hardware yet.** Two things of the RAK13702 are assumptions that have to be checked, both are marked at the top of `src/wisblock_ral_lr20xx_bsp.c`: the truth table of the two antenna switches (assumed like DIO2 of an SX1262: high only while transmitting) and the transmit power amplifier settings (`kLfPa[]` is a placeholder, the real output power can differ from the requested one until it is filled in and measured).
- `rxBoostedGainEnabled` has no effect with the LR2021.
- **2.4 GHz.** In P2P mode the frequency alone decides the band: 1.5 GHz and above uses the 2.4 GHz path (HF receiver, HF power amplifier, DIO9 antenna switch), anything below the sub-GHz path (DIO7 switch). The P2P bandwidth choices stop at 500 kHz, the wider 2.4 GHz bandwidths are not offered by the P2P API yet. For LoRaWAN the 2.4 GHz region `WISBLOCK_REGION_WW2G4` (DR0-DR7 = SF12-SF5, 812.5 kHz, +10 dBm EIRP) is part of the build together with the LR2021: select it with `lora.setRegion(WISBLOCK_RUI3_BAND_WW2G4);` (or `setRegion(WISBLOCK_REGION_WW2G4)`) after `begin()`, or `AT+BAND=13`. RUI3 has no 2.4 GHz region, so 13 is a library addition after the last RUI3 band number (12 = LA915). One region is active at a time and it is set before joining, so switching between sub-GHz and 2.4 GHz means selecting the other region and joining again. On a board without an LR2021 `setRegion()` returns false and a restored WW2G4 region makes `join()` refuse instead of joining on another band.

## API documentation

All API commands are documented in the [WisBlockLoRaWAN-API](WisBlockLoRaWAN-API.md) document.    

## AT command set

All AT commands are documented in the [WisBlockLoRaWAN-AT-Commands](WisBlockLoRaWAN-AT-Commands.md) document.

The AT interface follows the RUI3 command set. Commands that only exist in this library (`AT+STATUS`, `AT+FPENDING`, `AT+RXBOOST`, `AT+PRECVDC`, `AT+LOWPOWER`, `AT+SAVE`, `AT+RESTORE`, `AT+FACTORY`, `AT+FIRMWAREVER`) and the RUI3 commands that are not implemented are listed there as well.

## Examples for Arduino IDE
Multiple examples are available in the examples folder of the library.

### BasicLoRaWAN
Simple LoRaWAN application. Everything is handled in _**`loop()`**_ where calls to _**`at_serial.handleSerial();`**_ and _**`lora.handleEvents();`**_ are mandatory.    
OTAA join, Class A, periodic uplink on port 1, all LoRaWAN callbacks wired.    
Works unmodified on RAK4631 / RAK3312 / RAK11310 once WisBlockLoRaBoards.h has the right pins for your revision and LBM is vendored in.    
The AT command interface runs on the USB serial port. It is polled from loop() with at_serial.handleSerial(), the same way as lora.handleEvents().

### LowPowerLoRaWAN
Low power example for LoRaWAN. Using semaphores to keep the MCU in low power state unless an action is required.
OTAA join, Class A, periodic uplink on port 1, all LoRaWAN callbacks wired.    
Works unmodified on RAK4631 / RAK3312 / RAK11310 once WisBlockLoRaBoards.h has the right pins for your revision and LBM is vendored in.    
This example shows as well how to add custom AT commands to an application.

### LoRaWAN-T_H-Sensor
Low power example for temperature and humidity sensing using the WisBlock RAK1901 temperature and humidity sensor.

### RAK3401_RAK13300
Minimal OTAA join + periodic uplink, Class A, on a RAK3400 WisDuo module used as a WisBlock Core module with a RAK13300 (or RAK13302, same wiring).    
SX1262 transceiver module - i.e. "RAK3401" in the sense this library uses the name (see WisBlockLoRaHwConfig.h).     
This is the same OTAA/join/uplink flow as BasicLoRaWAN.ino, the only difference is this single line in setup():    
```lora.begin(wisblockLoRaHwConfigRAK3401_RAK13300());   // instead of plain lora.begin() ```

Everything else - every other API call, the AT command layer if you add it, the examples under examples/ - works exactly the same regardless of which begin() you call, since it's only the radio wiring that differs.      
**Only builds for nRF52840 targets (RAK3400 is an nRF52840 module)**

###  BasicLoRaP2P
Simple LoRa P2P application. Everything is handled in _**`loop()`**_ where calls to _**`at_serial.handleSerial();`**_ and _**`lora.handleEvents();`**_ are mandatory.    
Alternates between CAD-gated TX and RX every few seconds.    
The AT command interface runs on the USB serial port. It is polled from loop() with at_serial.handleSerial(), the same way as lora.handleEvents().

### LowPowerLoRaP2P
Low power example for LoRa P2P. Using semaphores to keep the MCU in low power state unless an action is required.     
Works unmodified on RAK4631 / RAK3312 / RAK11310 once WisBlockLoRaBoards.h has the right pins for your revision and LBM is vendored in.

### LoRaP2PPingPong
PingPong example. Requires two devices flashed with the same application.     
Depending on timing of power on, one device will act as a master and send PING messages. The other device will act as a slave and is responding with PONG messages.    

### RX-Duty-LoRaP2P
LoRa P2P example that uses the DutyCycle feature to listen to incoming packets.

###  BasicFskP2P
Simple FSK P2P application.    
Alternates between CAD-gated TX and RX every few seconds.    
The AT command interface runs on the USB serial port. It is polled from loop() with at_serial.handleSerial(), the same way as lora.handleEvents().

### FSKP2PPingPong
PingPong example using FSK modulation for transmission. Requires two devices flashed with the same application.     
Depending on timing of power on, one device will act as a master and send PING messages. The other device will act as a slave and is responding with PONG messages.    

### FskP2PTest
Simple example using FSK modulation for transmission. Requires two devices flashed with the same application.     
One device has to be set as sender, the other one has to be set as receiver.    

### ATCommandInterface
Exposes the full AT command set (see README.md) over USB serial.     
Useful for provisioning devices from a host script/terminal without flashing per-device firmware.    
Demonstrates fully background, non-polled operation on RAK4631/RAK3312:
- lora.enableBackgroundTask()      - LoRaWAN/P2P event handling
- atCommands.enableBackgroundRx()  - AT command processing

## Examples for PlatformIO
Example projects for PlatformIO are in the folder [examples/PlatformIO](./examples/PlatformIO).      
All examples come with platformio.ini files for RAK4631, RAK3312, RAK11300 and RAK3400+RAK1330x.     
All examples assume that setup of LoRaWAN credentials and LoRa P2P settings are done via AT commands. None of these examples do explicit set any configuration with API calls. This is the usual way to setup devices if more than one is actually built.     
For convinience of using RAKwireless WisBlock Core modules with PlatformIO platforms, an additional folder _**rakwireless**_ is added to the project. In this folder are WisBlock Core specific definitions as some of the platforms do not natively support WisBlock Core modules.    

### BM-LoRa-P2P-Template
This can be used as a simple template for a LoRa P2P application. It provides all required setups and callbacks for LoRa P2P communication and can be easily extended to specific use cases by adding sensor or actuator code in the loop(). This example is based on the low power example code and is event driven.

### BM-LoRa-LoRaWAN-Template
This can be used as a simple template for a LoRaWAN application. It provides all required setups and callbacks for LoRaWAN communication and can be easily extended to specific use cases by adding sensor or actuator code in the loop(). This example is based on the low power example code and is event driven.

### BM-LoRaWAN-T_H
This is a sensor application designed from the BM-LoRaWAN-Template by adding the handlers for a RAK1901 Temperature and Humidity sensor module.

### LoRaP2PPingPong-PIO
This example is converted from the ArduinoIDE based example LoRaP2PPingPong to be build with PlatformIO

## Arduino IDE / PlatformIO usage of the library

The library builds in the Arduino IDE / arduino-cli as well as in PlatformIO. Install it into your `libraries` folder (or from the ZIP) and select a WisBlock board (RAK4631, RAK3112/RAK3312). 

For PlatformIO, simply add the library to the _**`lib_deps1**_ section in platformio.ini.     

```
lib_deps =
   https://github.com/beegee-tokyo/WisBlockLoRaWAN
```

If you change the structure of the src folder of the library (e.g. you want to customize the library for your own needs), please note:

- The Arduino IDE only adds the library's `src/` folder to the include path and cannot pass per-library compiler flags. So every `#include` of a vendored LoRa Basics Modem (LBM) header inside this library is a path relative to the including file, and `src/wb_lbm_config.h` supplies the build flags. The library therefore does **not** depend on the include path or on any other installed library, and never uses headers from other libraries that happen to ship files with the same names. PlatformIO keeps using `extra_script.py`; both work with the same sources.
- After re-vendoring LBM, run `python3 tools/make_lbm_includes_relative.py` once to convert the new files.

## 💡Production flow: factory defaults vs. user config

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
