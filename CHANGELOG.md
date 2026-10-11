# WisBlockLoRaWAN
Unified LoRaWAN (Class A/B/C) and LoRa P2P library for RAKwireless WisBlock boards using Semtech SX1262 + LoRa Basics Modem.

Release Notes

No official release yet, include in PlatformIO using platformio.ini by adding these lines:
```
lib_deps =
   https://github.com/beegee-tokyo/WisBlockLoRaWAN
```
For Arduino IDE, download the repository as ZIP file and add it to Arduino IDE library folder using the "Add from ZIP file" function.

## Documentation

- The file headers of all examples were corrected (three low power / P2P examples carried the header of `BasicLoRaWAN.ino`, the PingPong example had a damaged `@file` name, several had no Doxygen tags at all).
- Every function of the examples and of the library API now has a Doxygen description with `@param` and `@return`. `tools/Doxyfile` generates the documentation: `doxygen tools/Doxyfile`.
- `ATCommandInterface.ino`: the callback parameters were renamed from `r` to `result`, no change in behavior.

## RP2040

- RAK11300/RAK11310 now use the Arduino-Pico core (Arduino IDE) / maxgerhardt platform-raspberrypi (PlatformIO) with FreeRTOS SMP instead of the mbed core; radio on SPI1; AT interface, background task and examples work as on RAK4631/RAK3312. See Creation-Log-From-Claude-AI.md.

## FSK P2P

- New FSK (GFSK) P2P mode: `WISBLOCK_MODE_FSK_P2P` / `AT+NWM=2`, `AT+PBR`, `AT+PFDEV`, FSK `AT+PBW`/`AT+PPL`, example `BasicFskP2P`. Persisted config is now version 5 (older configs are converted). See Creation-Log-From-Claude-AI.md.
