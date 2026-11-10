# WisBlockLoRaWAN
Unified LoRaWAN (Class A/B/C) and LoRa P2P library for RAKwireless WisBlock boards using Semtech SX1262 + LoRa Basics Modem.

Release Notes

Oct 19, 2026 V1.0.3
- Fix Class B problem on ESP32-S3 RAk3112/RAK3312

Oct 17, 2026 V1.0.2
- Fix Class B timing and add Class B AT commands and API calls to documentation

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
- Install or update library with ArduinoIDE library manager

## Class B state

- New `WisBlockClassBState` (S0 ... S3, the states of RUI3's `AT+CLASS=?`), callback `onClassBStateChanged()` that is called with the new state on every change, and `getClassBState()`. The library did not report anything about the Class B set-up before.
- `AT+CLASS=?` shows the state for Class B: `AT+CLASS=B,S2`. Class A and C are unchanged.
- Fix: the ping slot periodicity (`setPingSlotPeriodicity()`, `AT+PGSLOT`, `WisBlockLoRaWANSettings::pingSlotPeriodicity`) set before the join never reached the stack, the stack refuses it from a device that is not joined and the result was not checked. The device stayed at the stack default of 7 (one ping slot per 128 s beacon period, up to about two minutes of delay for a Class B downlink). The value is now applied right after the join, before Class B is enabled.

## Documentation

- The file headers of all examples were corrected (three low power / P2P examples carried the header of `BasicLoRaWAN.ino`, the PingPong example had a damaged `@file` name, several had no Doxygen tags at all).
- Every function of the examples and of the library API now has a Doxygen description with `@param` and `@return`. `tools/Doxyfile` generates the documentation: `doxygen tools/Doxyfile`.
- `ATCommandInterface.ino`: the callback parameters were renamed from `r` to `result`, no change in behavior.

## RP2040

- RAK11300/RAK11310 now use the Arduino-Pico core (Arduino IDE) / maxgerhardt platform-raspberrypi (PlatformIO) with FreeRTOS SMP instead of the mbed core; radio on SPI1; AT interface, background task and examples work as on RAK4631/RAK3312. See Creation-Log-From-Claude-AI.md.

## LR2021 2.4 GHz P2P

- New example `LoRaP2PPingPong2G4`: the PingPong example for a RAK3401 + RAK13702 (LR2021) at 2.4 GHz. Tested with two RAK3401 + RAK13702 nodes.
- New P2P bandwidths for the 2.4 GHz band: `WISBLOCK_BW_203`, `WISBLOCK_BW_406`, `WISBLOCK_BW_812`, `WISBLOCK_BW_1000` (LR2021 only, AT bandwidth indexes 10 - 13). The SX126x engine does not know them and falls back to 7.81 kHz.

## LR2021 RX sensitivity (RAK13702)

- The RAK13702 preset now uses a 1.6 V TCXO supply (was 3.3 V, inherited from the RAK13300 preset). Override with `-DWISBLOCK_LR2021_TCXO_VOLTAGE=SX126X_TCXO_CTRL_x_xV`.
- Antenna switch polarity of the RAK13702 from the switch logic checked on the board: sub-GHz DIO7 is now high while receiving (low in TX), 2.4 GHz DIO9 stays high while transmitting (low in RX). The two bands are wired the opposite way round. `-DWISBLOCK_LR2021_RFSW_LF_INVERT` / `-DWISBLOCK_LR2021_RFSW_HF_INVERT` swap one band back for testing. Verified by the P2P and LoRaWAN tests on a RAK3401 + RAK13702, see Creation-Log-From-Claude-AI.md.

## LR2021 LoRaWAN RX windows

- The LR2021 needs about 60 ms longer than the SX1262 from the radio command to being on air, so the RX1 / RX2 windows opened too late and the join accept preamble was missed. The delay reported to the modem stack (`smtc_modem_hal_get_radio_tcxo_startup_delay_ms()`) is now 64 ms longer for an LR2021. Override with `-DWISBLOCK_LR2021_RX_STARTUP_EXTRA_MS=<ms>` (default 64). The value was derived from the join logs, not measured on the chip. With the default the join works on sub-GHz AS923-3 with ChirpStack, TTN and Actility.

## 2.4 GHz LoRaWAN: single data rate option

- New option `WW_2G4_SINGLE_DATARATE` (documented in `wb_lbm_config.h`, or `-DWW_2G4_SINGLE_DATARATE` in the build flags, the macro of the vendored stack): join requests use DR0 (SF12) only and the mobile ADR profiles use DR0 only. The default is unchanged (join data rate drawn at random from DR0 - DR5). Needed for gateways with one fixed spreading factor per channel, for example the RAK5148. The join channel is still picked at random from 2403, 2425 and 2479 MHz. Not tested on hardware yet.

## AT commands: P2P frequency windows

- `AT+PFREQ`, `AT+P2P` (LoRa and FSK) accept two frequency windows, depending on the radio on the board: 150 - 960 MHz for the SX1262 and the LR2021, 2400 - 2483 MHz for the LR2021 only. Everything else returns `AT_PARAM_ERROR`. The LoRa form of `AT+P2P` only rejected a frequency of 0 before, so an SX1262 board now rejects 2.4 GHz and out of range values there as well.
- New AT bandwidth indexes for the wide 2.4 GHz bandwidths (`AT+PBW`, `<bw>` of `AT+P2P`): 10 = 203 kHz, 11 = 406 kHz, 12 = 812.5 kHz, 13 = 1000 kHz, accepted on the LR2021 only (`AT_PARAM_ERROR` on an SX1262). The indexes 0 - 9 are unchanged. The TX power limit of the AT commands (5 - 22 dBm) is unchanged: the LR2021 limits it to the range of the PA that is used, +12 dBm at 2.4 GHz.

## Example fix

- `LowPowerLoRaWAN`: the message after `lora.setADR(false)` no longer says "Failed to disable ADR". The call returns false while the stack only has the change queued, it is applied after the join.

## FSK P2P

- New FSK (GFSK) P2P mode: `WISBLOCK_MODE_FSK_P2P` / `AT+NWM=2`, `AT+PBR`, `AT+PFDEV`, FSK `AT+PBW`/`AT+PPL`, example `BasicFskP2P`. Persisted config is now version 5 (older configs are converted). See Creation-Log-From-Claude-AI.md.
