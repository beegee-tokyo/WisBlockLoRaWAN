# WisBlockLoRaWAN
This library is created to replace the [SX126x-Arduino](https://github.com/beegee-tokyo/SX126x-Arduino) in the future.     
It includes     
- an API for LoRa P2P communication, including P2P CAD and RX-Duty-Cycle implementation.
- a LoRaWAN modem stack based on Semtechs [SWL2001 Basic Modem](https://github.com/Lora-net/SWL2001/tree/master) implementation.     
It supports Class A, Class C and Class B (not yet tested), LinkCheck, ServerTime and a full implementation of the [TS001-LoRaWAN L2 1.0.4](https://resources.lora-alliance.org/technical-specifications/ts001-1-0-4-lorawan-l2-1-0-4-specification) and [Regional Parameters RP2-1.0.3](https://resources.lora-alliance.org/technical-specifications/rp2-1-0-3-lorawan-regional-parameters) specifications.     
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
| _**LoRaWAN**_ | |
| AT+NWM=_**0/1/2**_ / AT+NWM=?                  | 0 = P2P_LORA, 1 = LoRaWAN, 2 = P2P_FSK (not supported - this library has no FSK P2P mode)                          |
| AT+DEVEUI=_**hex8**_ / AT+DEVEUI=?               | Device EUI                                     |
| AT+APPEUI=_**hex8**_ / AT+JOINEUI <br> AT+APPEUI=? / AT+JOINEUI=? | Join EUI                                       |
| AT+APPKEY=_**hex16**_ / AT+APPKEY=?              | App/Network key (OTAA)                         |
| AT+DEVADDR=_**hex4**_ / AT+DEVADDR=?             | Device Address - settable for ABP; for OTAA, AT+DEVADDR=? reports the live network-assigned address once joined (empty/0 before that - see `LoRaWANEngine::getDevAddr()`'s doc comment) |
| AT+NWKSKEY=_**hex16**_ / AT+NWKSKEY=?            | Network Session Key (ABP)                      |
| AT+APPSKEY=_**hex16**_ / AT+APPSKEY=?            | App Session Key (ABP)                          |
| AT+BAND=_**0..12**_ / AT+BAND=?             | 0 EU433 (unsupported), 1 CN470, 2 RU864, 3 IN865, 4 EU868, 5 US915, 6 AU915, 7 KR920, 8 AS923-1, 9 AS923-2, 10 AS923-3, 11 AS923-4, 12 LA915 (unsupported) |
| AT+MASK=_**hex4**_ / AT+MASK=?              | Sub-band pre-select (US915/AU915/CN470/CN470_RP_1_0 only) - bit N enables sub-band N+1, 0000 = all channels |
| AT+LBT=_**0/1**_ / AT+LBT=?                 | Listen Before Talk on/off (support Korea, Japan) - silently auto-enabled where regulatorily mandatory either way |
| AT+LBTRSSI=_**dBm**_ / AT+LBTRSSI=?         | LBT RSSI threshold in dBm (signed), e.g. -80 |
| AT+LBTSCANTIME=_**ms**_ / AT+LBTSCANTIME=?  | LBT listen duration in ms before a channel is judged clear |
| AT+PGSLOT=_**0-7**_ / AT+PGSLOT=?           | Class B unicast ping slot periodicity (0=~1s ... 7=128s); also sends PingSlotInfoReq |
| AT+BFREQ=?                                  | Class B beacon DR + frequency for the current region (read-only) |
| AT+BTIME=?                                  | Class B beacon time, seconds since GPS epoch, from the last received beacon (read-only) |
| AT+FPENDING=_**0/1**_ / AT+FPENDING=?       | Auto-fetch pending downlinks (Class A) - send an empty uplink when FPending is set, default 1 |
| AT+DR=_**0..15**_ / AT+DR=?                 | Data rate                                     |
| AT+CLASS=_**A/B/C**_ / AT+CLASS=?              | Device class                                       |
| AT+NJM=_**0/1**_ / AT+NJM=?              | 0 = ABP, 1 = OTAA                                  |
| AT+JOIN=_**w**_:_**x**_:_**y**_:_**z**_ / AT+JOIN=? | Join control - w: 1=join now/0=stop, x: auto-join on power-up, y: reattempt interval (7-255s), z: max attempts (0=unlimited). Bare AT+JOIN = join now with current settings |
| AT+NJS=?                       | 0 = not joined, 1 = joined                              |
| AT+ADR=_**0/1**_ / AT+ADR=?                   | ADR on/off                                         |
| AT+TXP=_**0..15**_ / AT+TXP=?                 | TX power index                                     |
| AT+SEND=_**port**_:_**hex payload**_   | Send LoRaWAN uplink (empty hex payload = 0-length uplink, e.g. to manually fetch a pending downlink) |
| AT+CFM=_**0/1**_/ AT+CFM=?                   | Confirmed/unconfirmed uplinks                      |
| AT+LINKCHECK=<0/1/2>                   | 0 = disabled, 1 = request once (on the next uplink), 2 = request automatically on every uplink                               |
| AT+LINKCHECK=?                 | Query the most recently answered link check's margin (dB) and gateway count, without sending a new request |
| AT+TIMEREQ                     | Request network time (DeviceTimeReq)               |
| _**P2P**_ | |
| AT+P2P=_**freq**_:_**sf**_:_**bw**_:_**cr**_:_**preamble**_:_**txpower**_ | Set LoRa P2P radio params        |
| AT+CAD=_**0/1**_ / AT+CAD=?                   | Enable/disable CAD before P2P TX                   |
| AT+RXBOOST=_**0/1**_ / AT+RXBOOST=?               | Enable/disable RX boosted gain (extra ~4-5mA RX current for a few dB sensitivity) |
| AT+PSEND=_**hex payload**_         | Send a LoRa P2P packet                             |
| AT+PRECV=_**0/timeout_ms**_        | Put radio into RX (0 = continuous)                 |
| AT+PRECVDC=_**rxTimeMs**_:_**sleepTimeMs**_ | Put radio into SX1262 hardware RX duty-cycling (chip alternates RX/sleep on its own) |
| AT+PRECVDC=AUTO                | Same, computed automatically from the currently configured bandwidth/SF/preamble length |
| AT+PRECVDC=AUTO:_**txPreambleLengthSymbols**_ | Same, computed against a given transmitter preamble length instead of this radio's own - prefer this form |
| _**Others**_ | |
| AT+LOWPOWER=_**0/1**_ / AT+LOWPOWER=?             | Enable/disable low power (DIO1 wake) mode           |
| AT+SAVE                        | Persist current config to flash (user config)      |
| AT+RESTORE                     | Reload user config from flash, discarding unsaved changes |
| AT+FACTORY                     | Save current config as the factory backup, then reset MCU (see "Production flow" below) |
| ATR                             | Copy the factory backup over the current + saved user config (does not reset MCU) |
| AT+STATUS                      | Dump current config + join/link status             |
| AT+VER=?                       | RUI3-format version string (this library's own version, not RUI3 firmware) |
| AT+ALIAS=_**16char string**_ / AT+ALIAS=?   | Get/set a free-form device label (persisted, max 16 chars) |
| AT+FIRMWAREVER=_**31char string**_ / AT+FIRMWAREVER=? | Get/set a free-form firmware version label (persisted, max 31 chars - RUI3 documents this as 32, see `WisBlockLoRaWAN::setFirmwareVer()`'s doc comment) |
| _**LoRaWAN multicast (Class B/C)**_ | |
| AT+ADDMULC=_**[Class]:[DevAddr]:[NwkSKey]:[AppSKey]:[Frequency]:[Datarate]:[Periodicity]**_ | Configure a multicast group and start its RX session (keyed by DevAddr - re-running with an existing DevAddr updates that group; up to 4 groups at once) - see `WisBlockLoRaWAN::setMulticastGroup()` |
| AT+RMVMULC=_**hex4 DevAddr**_ | Stop and remove a multicast group by its DevAddr |
| AT+LSTMULC=? | List configured multicast groups, one `Class:DevAddr:NwkSKey:AppSKey:Frequency:Datarate` line each (keys shown in plaintext, matching RUI3) |
| AT+SN=?                        | Get unique device serial number |                          
| AT+HWMODEL=?                   | Get HW model (rak4630, rak3112, or rak11310) |
| AT+HWID=?                      | Get MCU ID (nrf52840, esp32-s3, or rp2040) |
| ATZ                      | Reset MCU |
| AT+BOOT                      | Force DFU mode (only RAK4631) |
| _**Custom AT commands**_ | |
| ATC+_**CMD**_=_**value**_ / ATC+_**CMD**_=? / ATC+_**CMD**_ | Application-defined command, registered with `WisBlockLoRaAT::addCustomATCommand()` - see `WisBlockLoRaAT.h` |

### Production flow: factory defaults vs. user config

Two complete configs are kept on flash, in separate slots - a "factory" backup and the
regular "user" config (the one AT+SAVE/AT+RESTORE/`saveConfig()`/`restoreConfig()` already
worked with before AT+FACTORY/ATR existed):

1. Flash firmware. On a genuinely first boot, nothing is saved yet, so the live config is
   just the compiled-in defaults (region EU868, mode LoRaWAN, join mode OTAA, and the
   shared default JoinEUI/AppKey already in `WisBlockOTAAKeys` - see `WisBlockLoRaWANTypes.h`).
2. Set this unit's real, unique `AT+DEVEUI=` (assigned by the LoRa Alliance). Leave
   everything else alone.
3. `AT+FACTORY` - snapshots the *current live config* (defaults + this unit's real DevEUI)
   into the factory slot. This is a one-time production step, not something end users run.
4. The device resets automatically right after saving.
5. `ATR` - copies the factory slot over both the live config and the user slot, so the
   factory state (including the real DevEUI from step 2) becomes what AT+SAVE/AT+RESTORE
   work with from here on too.

After that, an end user is free to change settings and `AT+SAVE` them as usual. If they end
up in a broken state, `ATR` at any time restores exactly what was captured in step 3 -
including the correct DevEUI, never the generic compiled-in one.

`AT+FACTORY` and `ATR` are easy to confuse with the already-existing `AT+RESTORE`/
`restoreConfig()` - `AT+RESTORE` reloads the *user* slot (undoing only unsaved, in-RAM
changes since the last `AT+SAVE`); `ATR` reaches further back, to the factory slot from
step 3, and overwrites the user slot with it too.
