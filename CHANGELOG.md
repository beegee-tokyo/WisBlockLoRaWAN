# WisBlockLoRaWAN
Unified LoRaWAN (Class A/B/C) and LoRa P2P library for RAKwireless WisBlock boards using Semtech SX1262 + LoRa Basics Modem.

Release Notes

No official release yet, include in PlatformIO using platformio.ini by adding these lines:
```
lib_deps =
   https://github.com/beegee-tokyo/WisBlockLoRaWAN
```
For Arduino IDE, download the repository as ZIP file and add it to Arduino IDE library folder using the "Add from ZIP file" function.

## LR2021 (RAK13702) on RAK3401

- New optional support for the Semtech LR2021 (RAK13702 module) on a RAK3401, LoRaWAN and LoRa/FSK P2P. Off by default, enable with `WISBLOCK_ENABLE_LR2021` (`src/wb_lbm_config.h` or the build flags). The existing SX1262 behavior is unchanged: the SPI traffic of the SX1262 LoRa P2P engine is byte-identical with and without the option. Not tested on hardware yet.
- `wisblockLoRaHwConfigRAK3401()` was renamed to `wisblockLoRaHwConfigRAK3401_RAK13300()`; the old name still works. New `wisblockLoRaHwConfigRAK3401_RAK13702()`, `WISBLOCK_LORA_CHIP_LR2021`, and the `lr*` fields of `WisBlockLoRaHwConfig`.
- New example `RAK3401_RAK13702`.
- 2.4 GHz LoRaWAN region `WISBLOCK_REGION_WW2G4` for the LR2021 (compiled in together with `WISBLOCK_ENABLE_LR2021`): `region_ww_2g4.[ch]` and `region_ww_2g4_defs.h` vendored again, new `WisBlockLoRaWAN::setRegion(WisBlockRegion)` and `wisblockRegionUsable()`. It is also band number 13, `WISBLOCK_RUI3_BAND_WW2G4` (a library addition after LA915 = 12, RUI3 has no 2.4 GHz region), for `setRegion()`, `getRegion()` and `AT+BAND=13`. On a board that cannot do 2.4 GHz the region is refused and `join()` does not run, where `WISBLOCK_REGION_WW2G4` used to silently map to EU868.

## Class B state

- New `WisBlockClassBState` (S0 ... S3, the states of RUI3's `AT+CLASS=?`), callback `onClassBStateChanged()` that is called with the new state on every change, and `getClassBState()`. The library did not report anything about the Class B set-up before.
- `AT+CLASS=?` shows the state for Class B: `AT+CLASS=B,S2`. Class A and C are unchanged.

## Documentation

- The file headers of all examples were corrected (three low power / P2P examples carried the header of `BasicLoRaWAN.ino`, the PingPong example had a damaged `@file` name, several had no Doxygen tags at all).
- Every function of the examples and of the library API now has a Doxygen description with `@param` and `@return`. `tools/Doxyfile` generates the documentation: `doxygen tools/Doxyfile`.
- `ATCommandInterface.ino`: the callback parameters were renamed from `r` to `result`, no change in behavior.

## RP2040

- RAK11300/RAK11310 now use the Arduino-Pico core (Arduino IDE) / maxgerhardt platform-raspberrypi (PlatformIO) with FreeRTOS SMP instead of the mbed core; radio on SPI1; AT interface, background task and examples work as on RAK4631/RAK3312. See Creation-Log-From-Claude-AI.md.

## FSK P2P

- New FSK (GFSK) P2P mode: `WISBLOCK_MODE_FSK_P2P` / `AT+NWM=2`, `AT+PBR`, `AT+PFDEV`, FSK `AT+PBW`/`AT+PPL`, example `BasicFskP2P`. Persisted config is now version 5 (older configs are converted). See Creation-Log-From-Claude-AI.md.
