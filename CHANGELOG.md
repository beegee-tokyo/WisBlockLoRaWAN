# WisBlockLoRaWAN
Unified LoRaWAN (Class A/B/C) and LoRa P2P library for RAKwireless WisBlock boards using Semtech SX1262 + LoRa Basics Modem.

Release Notes

No official release yet, include in PlatformIO using platformio.ini by adding these lines:
```
lib_deps =
   https://github.com/beegee-tokyo/WisBlockLoRaWAN
```
For Arduino IDE, download the repository as ZIP file and add it to Arduino IDE library folder using the "Add from ZIP file" function.

## P2P AT commands

- Added the missing RUI3 P2P commands `AT+PFREQ`, `AT+PSF`, `AT+PBW`, `AT+PCR`, `AT+PPL`, `AT+PTP`, `AT+IQINVER` and `AT+SYNCWORD`. Values are strictly checked, a bad value returns `AT_PARAM_ERROR` and changes nothing.
- `AT+PBW` and `AT+PCR` use the RUI3 index numbering.
- **Changed:** the `<bw>` field of `AT+P2P` now uses the same bandwidth index as `AT+PBW` (RUI3 numbering: 3 = 7.8 kHz ... 9 = 62.5 kHz). Before, indexes 3 - 9 were numbered the other way round (3 = 62.5 kHz ... 9 = 7.81 kHz). Indexes 0 - 2 (125 / 250 / 500 kHz) are unchanged. A bandwidth index above 9 is now rejected. The `<cr>` field of `AT+P2P` still counts 1 - 4 (`AT+PCR`: 0 - 3). Scripts that send `AT+P2P` with a bandwidth index of 3 or more must be updated; stored settings are not affected.
- New P2P settings `iqInversion` (default off) and `syncWord` (default 0x1424, the previous behavior) with `setP2PIqInversion()` and `setP2PSyncWord()`. The sync word is now written to the radio as the full 16 bit value, IQ inversion applies to TX and RX.
- The stored configuration version is now 4. Version 3 configs (keys, LoRaWAN and P2P settings, also the factory slot) are read and converted automatically, nothing is lost on update. Downgrading to an older library version discards a version 4 config.

## New example

- `examples/LoRaP2PPingPong`: P2P Ping Pong between identical nodes. After boot a node listens for 10 s, answers a received PING with a PONG and becomes Slave, otherwise it sends a PING and becomes Master. The Master sends a PING every 15 s. Extras compared to the basic idea: random listen time and PING jitter so that nodes started together do not stay deaf to each other, two Masters resolve themselves, a Slave takes over when the Master disappears. The behavior was checked with a simulation of several nodes sharing one channel.

## Configuration check and SX126x-Arduino migration guide

- New `WisBlockLoRaWAN::hasValidConfig()`: after `begin()` it tells whether a valid saved configuration was found (TRUE) or the built-in defaults are in use (FALSE, for example a new or erased device). The application can set up and `saveConfig()` the configuration only once. Documented in the API document.
- New section "Migration from SX126x-Arduino" in the API document: events, radio functions, TxConfig/RxConfig parameters, LoRaMacHelper (`lmh_xxx`) calls and an example.
