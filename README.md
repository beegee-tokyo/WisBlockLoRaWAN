# WisBlockLoRaWAN
This library is created to replace the [SX126x-Arduino](https://github.com/beegee-tokyo/SX126x-Arduino) in the future.     
It includes     
- an API for LoRa P2P communication, including P2P CAD and RX-Duty-Cycle implementation.
- a LoRaWAN modem stack based on Semtechs [SWL2001 Basic Modem](https://github.com/Lora-net/SWL2001/tree/master) implementation.     
It supports Class A, Class C and Class B (not yet tested), LinkCheck, ServerTime, Relay (not yet tested) and a full implementation of the [TS001-LoRaWAN L2 1.0.4](https://resources.lora-alliance.org/technical-specifications/ts001-1-0-4-lorawan-l2-1-0-4-specification) and [Regional Parameters RP2-1.0.3](https://resources.lora-alliance.org/technical-specifications/rp2-1-0-3-lorawan-regional-parameters) specifications.     
FUOTA function is not implemented and is not planned at this time due to the complexity of FUOTA and challenge to implement it in an Arduino library.    
- An AT command interface and a local storage for device setup, e.g. LoRa P2P settings, LoRaWAN credentials, DevNonces, ...     
- Same as with the SX126x-Arduino library, it uses an FreeRTOS task in the background to handle LoRa and LoRaWAN events. This allows to create application with minimum power consumption as there is no need to use the loop() to check for events and keep the LoRa and LoRaWAN engines running.     

# IMPORTANT
- _**This library is still under testing and development. Many parts are not fully tested and challenged in real world applications, use with caution!**_
- This library was created with support of Claude AI, where the AI was doing the simplification and integration of the SWL2001 Basic Modem source codes into an Arduino Library. The requirement definitions for functionality and testing of the functionality on eal devices is done by the author of this repository.    
- _**RAK11300 and RAK11310 support is not yet fully implemented. The original Arduino and PlatformIO BSP's for the RP2040 MCU are based on MBED, which is no longer officially maintained and supported, a different approach will be required for these modules.**_     

## API documentation

_**to be done**_ See P2P and LoRaWAN examples for a first idea how to use the library.    

## AT command set

| <div style="width:150px">Command</div> | Description                                      |
| :--- | :--- |
| AT+MODE=_**0/1/2**_ / AT+MODE=?                  | 0 = P2P_LORA, 1 = LoRaWAN, 2 = P2P_FSK (not supported - this library has no FSK P2P mode)                          |
| AT+MODE=?                      | Query current mode                                 |
| AT+DEVEUI=_**hex8**_ / AT+DEVEUI=?               | Device EUI                                     |
| AT+APPEUI=_**hex8**_ / AT+JOINEUI <br> AT+APPEUI=? / AT+JOINEUI=? | Join EUI                                       |
| AT+APPKEY=_**hex16**_ / AT+APPKEY=?              | App/Network key (OTAA)                         |
| AT+DEVADDR=_**hex4**_ / AT+DEVADDR=?             | Device Address (ABP)                           |
| AT+NWKSKEY=_**hex16**_ / AT+NWKSKEY=?            | Network Session Key (ABP)                      |
| AT+APPSKEY=_**hex16**_ / AT+APPSKEY=?            | App Session Key (ABP)                          |
| AT+BAND=_**0..12**_ / AT+BAND=?             | 0 EU433 (unsupported), 1 CN470, 2 RU864, 3 IN865, 4 EU868, 5 US915, 6 AU915, 7 KR920, 8 AS923-1, 9 AS923-2, 10 AS923-3, 11 AS923-4, 12 LA915 (unsupported) |
| AT+DR=_**0..15**_ / AT+DR=?                 | Data rate                                     |
| AT+CLASS=_**A/B/C**_ / AT+CLASS=?              | Device class                                       |
| AT+NJM=_**0/1**_ / AT+NJM=?              | 0 = ABP, 1 = OTAA                                  |
| AT+JOIN                        | Start join procedure                               |
| AT+NJS=?                       | 0 = not joined, 1 = joined                              |
| AT+ADR=_**0/1**_ / AT+ADR=?                   | ADR on/off                                         |
| AT+TXP=_**0..15**_ / AT+TXP=?                 | TX power index                                     |
| AT+RELAY=_**0/1/2**_ / AT+RELAY=?              | 0 = off, 1 = relay TX (end-device), 2 = relay RX (serving) |
| AT+RELAYED=_**activation**_:_**smartLevel**_:<br>_**backoff**_:_**missedWorAckToNoSync**_:_**2ndChEnable**_:_**2ndChFreqHz**_:<br>_**2ndChAckFreqHz**_:_**2ndChDr**_ <br>/ AT+RELAYED=? | Configure relay TX (end-device role); persisted, re-applied on AT+RELAY=1 |
| AT+RELAYSRV=_**cadPeriod**_:_**freqHz**_:<br>_**ackFreqHz**_:_**dr**_:_**errorPpm**_:_**cadToRxSymb**_ <br>/ AT+RELAYSRV=? | Configure relay RX (serving role); persisted, re-applied on AT+RELAY=2. Requires LBM built with ADD_RELAY_RX |
| AT+RELAYDEV=_**idx**_:_**devAddr8hex**_:<br>_**rootWorSKey32hex**_:_**unlimited0/1**_:_**bucketFactor**_:<br>_**reloadRate**_ | Register a trusted end-device (0-15) with the serving relay - **required** before it forwards anything for that device; not persisted |
| AT+RELAYDEV=? | Returns an error - no local copy of the registered device list is kept to read back (and it contains a session key that shouldn't be echoed in plaintext regardless) |
| AT+RELAYDEVDEL=_**idx**_            | Remove a trusted end-device from the serving relay's list |
| AT+SEND=_**port**_:_**hex payload**_   | Send LoRaWAN uplink                                |
| AT+CFM=_**0/1**_/ AT+CFM=?                   | Confirmed/unconfirmed uplinks                      |
| AT+LINKCHECK=<0/1/2>                   | 0 = disabled, 1 = request once (on the next uplink), 2 = request automatically on every uplink                               |
| AT+LINKCHECK=?                 | Query the most recently answered link check's margin (dB) and gateway count, without sending a new request |
| AT+TIMEREQ                     | Request network time (DeviceTimeReq)               |
| AT+P2P=_**freq**_:_**sf**_:_**bw**_:_**cr**_:_**preamble**_:_**txpower**_ | Set LoRa P2P radio params        |
| AT+CAD=_**0/1**_ / AT+CAD=?                   | Enable/disable CAD before P2P TX                   |
| AT+RXBOOST=_**0/1**_ / AT+RXBOOST=?               | Enable/disable RX boosted gain (extra ~4-5mA RX current for a few dB sensitivity) |
| AT+PSEND=_**hex payload**_         | Send a LoRa P2P packet                             |
| AT+PRECV=_**0/timeout_ms**_        | Put radio into RX (0 = continuous)                 |
| AT+PRECVDC=_**rxTimeMs**_:_**sleepTimeMs**_ | Put radio into SX1262 hardware RX duty-cycling (chip alternates RX/sleep on its own) |
| AT+PRECVDC=AUTO                | Same, computed automatically from the currently configured bandwidth/SF/preamble length |
| AT+PRECVDC=AUTO:_**txPreambleLengthSymbols**_ | Same, computed against a given transmitter preamble length instead of this radio's own - prefer this form |
| AT+LOWPOWER=_**0/1**_ / AT+LOWPOWER=?             | Enable/disable low power (DIO1 wake) mode           |
| AT+SAVE                        | Persist current config to flash                    |
| AT+RESTORE                     | Reload config from flash                           |
| AT+FACTORY                     | Reset config to factory defaults                   |
| AT+STATUS                      | Dump current config + join/link status             |

