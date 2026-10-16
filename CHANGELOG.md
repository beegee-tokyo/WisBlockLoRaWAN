# WisBlockLoRaWAN
Unified LoRaWAN (Class A/B/C) and LoRa P2P library for RAKwireless WisBlock boards using Semtech SX1262 + LoRa Basics Modem.

Release Notes

Oct 16, 2026 V1.0.1
- Add Class B status callback and status check with AT+CLASS=?. Class B still not tested, waiting for environment.

Oct 13, 2026 V1.0.0
- First release, still testing but confirmed so far:
   - regions US915 AU915, AS923-3, EU868
   - Class A and Class C, Class B not yet tested (I don't have the environment yet)
   - uplinks
   - multicast downlinks
   - AT command
   - multi downlink receive
   - Supports RAK4630/RAK4631, RAK3112/RAK3312, RAK11300/RAK11310 (but limits regarding used platform) and RAK3400/RAk3401 with RAK13300/RAk13302 LoRa transceiver
   - linkcheck request
   - time request
   - device status request

To use library in PlatformIO, use platformio.ini lib_deps      
For latest staging (might be buggy)    
```
lib_deps =
   https://github.com/beegee-tokyo/WisBlockLoRaWAN
```
For latest released
```
lib_deps =
   beegee-tokyo/WisBlockLoRaWAN
```

For Arduino IDE
For latest staging (might be buggy)  
- download the repository as ZIP file and add it to Arduino IDE library folder using the "Add from ZIP file" function.
For latest released
Install or update library with ArduinoIDE library manager

## Documentation

- The file headers of all examples were corrected (three low power / P2P examples carried the header of `BasicLoRaWAN.ino`, the PingPong example had a damaged `@file` name, several had no Doxygen tags at all).
- Every function of the examples and of the library API now has a Doxygen description with `@param` and `@return`. `tools/Doxyfile` generates the documentation: `doxygen tools/Doxyfile`.
- `ATCommandInterface.ino`: the callback parameters were renamed from `r` to `result`, no change in behavior.

## RP2040

- RAK11300/RAK11310 now use the Arduino-Pico core (Arduino IDE) / maxgerhardt platform-raspberrypi (PlatformIO) with FreeRTOS SMP instead of the mbed core; radio on SPI1; AT interface, background task and examples work as on RAK4631/RAK3312. See Creation-Log-From-Claude-AI.md.

## FSK P2P

- New FSK (GFSK) P2P mode: `WISBLOCK_MODE_FSK_P2P` / `AT+NWM=2`, `AT+PBR`, `AT+PFDEV`, FSK `AT+PBW`/`AT+PPL`, example `BasicFskP2P`. Persisted config is now version 5 (older configs are converted). See Creation-Log-From-Claude-AI.md.
