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
- This library was created with support of Claude AI, where the AI was doing the simplification and integration of the SWL2001 Basic Modem source codes. The requirements for functionality and testing of the functionality is done by the author of this repository.    
- _**RAK11300 and RAK11310 support is not yet fully implemented. The original Arduino and PlatformIO BSP's for the RP2040 MCU are based on MBED, which is no longer officially maintained and supported, a different approach will be required for these modules.**_     

## API documentation

_**to be done**_ See P2P and LoRaWAN examples for a first idea how to use the library.    

## AT command set

| <div style="width:150px">Command</div> | Description                                      |
| :--- | :--- |
| AT+MODE=_**0/1**_                  | 0 = LoRaWAN, 1 = LoRa P2P                          |
| AT+MODE=?                      | Query current mode                                 |
| AT+DEVEUI=_**hex8**_               | Set Device EUI                                     |
| AT+APPEUI=_**hex8**_ / AT+JOINEUI| Set Join EUI                                       |
| AT+APPKEY=_**hex16**_              | Set App/Network key (OTAA)                         |
| AT+DEVADDR=_**hex4**_              | Set Device Address (ABP)                           |
| AT+NWKSKEY=_**hex16**_             | Set Network Session Key (ABP)                      |
| AT+APPSKEY=_**hex16**_             | Set App Session Key (ABP)                          |
| AT+REGION=_**0..13**_              | EU868, US915, AU915, AS923, KR920, IN865, RU864... |
| AT+DR=_**0..15**_                  | Data rate index                                    |
| AT+CLASS=_**A/B/C**_               | Device class                                       |
| AT+JOINMODE=_**0/1**_              | 0 = OTAA, 1 = ABP                                  |
| AT+JOIN                        | Start join procedure                               |
| AT+ADR=_**0/1**_                   | ADR on/off                                         |
| AT+TXP=_**0..15**_                 | TX power index                                     |
| AT+RELAY=_**0/1/2**_               | 0 = off, 1 = relay TX (end-device), 2 = relay RX (serving) |
| AT+RELAYED=_**activation**_:_**smartLevel**_:<br>_**backoff**_:_**missedWorAckToNoSync**_:_**2ndChEnable**_:_**2ndChFreqHz**_:<br>_**2ndChAckFreqHz**_:_**2ndChDr**_ | Configure relay TX (end-device role); persisted, re-applied on AT+RELAY=1 |
| AT+RELAYSRV=_**cadPeriod**_:_**freqHz**_:<br>_**ackFreqHz**_:_**dr**_:_**errorPpm**_:_**cadToRxSymb**_ | Configure relay RX (serving role); persisted, re-applied on AT+RELAY=2. Requires LBM built with ADD_RELAY_RX |
| AT+RELAYDEV=_**idx**_:_**devAddr8hex**_:<br>_**rootWorSKey32hex**_:_**unlimited0/1**_:_**bucketFactor**_:<br>_**reloadRate**_ | Register a trusted end-device (0-15) with the serving relay - **required** before it forwards anything for that device; not persisted |
| AT+RELAYDEVDEL=_**idx**_            | Remove a trusted end-device from the serving relay's list |
| AT+SEND=_**port**_:_**hex payload**_   | Send LoRaWAN uplink                                |
| AT+CFM=_**0/1**_                   | Confirmed/unconfirmed uplinks                      |
| AT+LINKCHECK                   | Request a link check                               |
| AT+LINKCHECK=?                 | Query the most recently answered link check's margin (dB) and gateway count, without sending a new request |
| AT+TIMEREQ                     | Request network time (DeviceTimeReq)               |
| AT+P2P=_**freq**_:_**sf**_:_**bw**_:_**cr**_:_**preamble**_:_**txpower**_ | Set LoRa P2P radio params        |
| AT+CAD=_**0/1**_                   | Enable/disable CAD before P2P TX                   |
| AT+RXBOOST=_**0/1**_               | Enable/disable RX boosted gain (extra ~4-5mA RX current for a few dB sensitivity) |
| AT+PSEND=_**hex payload**_         | Send a LoRa P2P packet                             |
| AT+PRECV=_**0/timeout_ms**_        | Put radio into RX (0 = continuous)                 |
| AT+PRECVDC=_**rxTimeMs**_:_**sleepTimeMs**_ | Put radio into SX1262 hardware RX duty-cycling (chip alternates RX/sleep on its own) |
| AT+PRECVDC=AUTO                | Same, computed automatically from the currently configured bandwidth/SF/preamble length |
| AT+PRECVDC=AUTO:_**txPreambleLengthSymbols**_ | Same, computed against a given transmitter preamble length instead of this radio's own - prefer this form |
| AT+LOWPOWER=_**0/1**_              | Enable/disable low power (DIO1 wake) mode           |
| AT+SAVE                        | Persist current config to flash                    |
| AT+RESTORE                     | Reload config from flash                           |
| AT+FACTORY                     | Reset config to factory defaults                   |
| AT+STATUS                      | Dump current config + join/link status             |

