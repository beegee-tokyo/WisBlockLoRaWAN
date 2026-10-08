# WisBlockLoRaWAN API

## _ℹ️ INFO_
----
_This document describes every function, data type and callback of the **WisBlockLoRaWAN** Arduino library that is accessible from application level. It follows the layout of the [RUI3 LoRaWAN API](https://docs.rakwireless.com/product-categories/software-apis-and-libraries/rui3/lorawan) documentation, so RUI3 users can find the equivalent call quickly._

_The library runs on RAK4631 (nRF52840), RAK3312 (ESP32-S3) and RAK11310 (RP2040) with the Semtech SX1262 and uses Semtech's LoRa Basics Modem (LBM) as LoRaWAN stack._    

----

## _⚠️ WARNING_
----
_**Differences to RUI3 that matter when porting code**_

| RUI3 | WisBlockLoRaWAN | Comment |
| --- | --- | --- |
| `api.lorawan.xxx.get()` / `.set()` | `lora.setXxx()` and `lora.getConfig().lorawan.xxx` | One object (`WisBlockLoRaWAN lora;`), setters are methods. Most settings have no dedicated getter, read them from `lora.getConfig()`. |
| `api.lorawan.band.set(x)` | `lora.setRegion(WISBLOCK_RUI3_BAND_xxx)` | Uses the **RUI3 band numbering** (`0` = EU433 ... `12` = LA915). The internal LoRa Basics Modem numbering is hidden. |
| `api.lorawan.join()` | `lora.join()` | Asynchronous, result is reported through callbacks. |
| `api.lorawan.registerXxxCallback()` | `lora.onXxx()` | Callback signatures differ, see [LoRaWAN Callbacks](#lorawan-callbacks). |
| `api.lorawan.rx1dl`, `rx2dl`, `jn1dl`, `jn2dl`, `rx2dr`, `rx2fq`, `pnm`, `dcs` | *not available* | The receive window timing and the public network flag are handled by LoRa Basics Modem and are not exposed. |
| `api.lora.xxx` (P2P) | `lora.setP2Pxxx()`, `lora.sendP2P()` | See [LoRa P2P](#lora-p2p). |

----

## Migration from SX126x-Arduino

This section is for code written for the [SX126x-Arduino](https://github.com/beegee-tokyo/SX126x-Arduino) library (`Radio.xxx()` for LoRa P2P, `lmh_xxx()` for LoRaWAN). The two libraries cover the same ground, but WisBlockLoRaWAN is built around a single object and a settings model instead of direct radio calls.

### Main differences

| SX126x-Arduino | WisBlockLoRaWAN | Comment |
| --- | --- | --- |
| `Radio.xxx()` function table, `RadioEvents_t` struct | One object `WisBlockLoRaWAN lora;` with methods and `lora.onXxx()` callback registration | There is no direct access to the radio: no `Radio.Write()` / `Radio.Read()` registers, no `SetTxContinuousWave()`, no FSK modem. |
| `SetTxConfig()` + `SetRxConfig()` + `SetChannel()` | One setter per parameter (`setP2PFrequency()`, `setP2PSpreadingFactor()`, ...) | The settings are shared by TX and RX, so they cannot differ. Each setter is applied to the radio immediately. |
| Settings live in the sketch only | Settings live in the library and can be **stored in flash** | Call `saveConfig()` to keep them. After `begin()`, [hasValidConfig()](#hasvalidconfig) tells whether stored settings were found. |
| LoRaWAN: `LoRaMacHelper` (`lmh_xxx()`) with LoRaMac-node | LoRaWAN: same object, Semtech LoRa Basics Modem | See [LoRaWAN](#lorawan-from-sx126x-arduino) below. |
| Callbacks are called from the library's own IRQ task | Callbacks run from `lora.handleEvents()` (in `loop()`), or in the library task after `enableBackgroundTask()` | Keep callbacks short. Set a flag or give a semaphore and do the work in `loop()`, as the `LoRaP2PPingPong` example does. |
| P2P and LoRaWAN are two independent ways to use the radio | `setWorkMode(WISBLOCK_MODE_LORA_P2P)` or `setWorkMode(WISBLOCK_MODE_LORAWAN)` selects what the radio does | One mode at a time. |
| Boards: `lora_rak4630_init()`, `lora_rak3112_init()`, `lora_rak13300_init()`, `lora_hardware_init(hwConfig)` | `lora.begin()` (board selected at compile time) or `lora.begin(WisBlockLoRaHwConfig)` | See the `RAK3401_RAK13300` example for a custom hardware configuration. |

### LoRa P2P: initialization and events

| SX126x-Arduino | WisBlockLoRaWAN | Comment |
| --- | --- | --- |
| `lora_rak4630_init()` ... `Radio.Init(&RadioEvents)` | `lora.begin(); lora.setWorkMode(WISBLOCK_MODE_LORA_P2P);` | |
| `RadioEvents.TxDone` | `lora.onP2PTxFinished(cb)` | `void cb(const WisBlockTxResult &result)`, with `result.success` and `result.airtimeMs`. |
| `RadioEvents.RxDone(payload, size, rssi, snr)` | `lora.onP2PRxFinished(cb)` | `void cb(const WisBlockRxResult &result)`, with `result.data`, `result.length`, `result.rssi`, `result.snr`. `result.port` is 0 in P2P. |
| `RadioEvents.RxTimeout` | `onP2PRxFinished` callback with `result.length == 0` | There is no separate timeout callback. |
| `RadioEvents.TxTimeout` | `onP2PRxFinished` callback with `result.length == 0` | A TX that fails is reported the same way. A sketch that waits for `onP2PTxFinished` should also treat an empty RX result as "TX failed". |
| `RadioEvents.RxError` | *not available* | **A packet with a CRC error is currently delivered to the RX callback like a good packet.** Check the content yourself (known message, own checksum). |
| `RadioEvents.CadDone(bool)` | `lora.onP2PCadResult(cb)` | `void cb(WisBlockCADResult result)`: `WISBLOCK_CAD_CHANNEL_DETECTED` or `WISBLOCK_CAD_CHANNEL_CLEAR`. |
| `RadioEvents.PreAmpDetect`, `FhssChangeChannel` | *not available* | |

### LoRa P2P: radio functions

| SX126x-Arduino | WisBlockLoRaWAN | Comment |
| --- | --- | --- |
| `Radio.Rx(timeoutMs)` | `lora.startP2PReceive(timeoutMs)` | `0` = continuous receive, as in SX126x-Arduino. After a timeout the RX callback is called with `length == 0`. |
| `Radio.RxBoosted(timeoutMs)` | `lora.setP2PRxBoostedGain(true)` + `startP2PReceive()` | Boosted RX is a setting here (default on), not a separate call. |
| `Radio.Send(buffer, size)` | `bool ok = lora.sendP2P(data, length)` | At most 255 bytes. Result in `onP2PTxFinished`. |
| `Radio.Standby()` | `lora.stopP2PReceive()` | |
| `Radio.Sleep()` | `lora.sleepRadio()` | The library wakes the radio and reconfigures it by itself on the next RX, TX or CAD. |
| `Radio.StartCad()` | `lora.startP2PCad()` | Result in `onP2PCadResult`. The CAD parameters are fixed. |
| `Radio.SetCadParams()` | *not available* | |
| `Radio.IsChannelFree()` | `lora.setP2PCad(true)` | Listen-before-talk: a CAD runs before every `sendP2P()`. |
| `Radio.SetRxDutyCycle(rx, sleep)` | `lora.startP2PReceiveDutyCycle(rxTimeMs, sleepTimeMs)` | `computeP2PRxDutyCycleTiming()` calculates matching times for a given transmitter preamble length. |
| `Radio.SetPublicNetwork(true / false)` | `lora.setP2PSyncWord(0x3444 / 0x1424)` | The default is `0x1424` (private). |
| `Radio.SetCustomSyncWord(word)` | `lora.setP2PSyncWord(word)` | 16 bit value. |
| `Radio.GetSyncWord()` | `lora.getP2PSettings().syncWord` | |
| `Radio.TimeOnAir()` | `result.airtimeMs` in `onP2PTxFinished` | Reported after the TX, there is no call to calculate it in advance. |
| `Radio.Rssi()` | *not available* | The RSSI and SNR of each received packet are in the RX result. |
| `Radio.Random()`, `CheckRfFrequency()`, `SetMaxPayloadLength()`, `SetTxContinuousWave()`, `Write()` / `Read()` | *not available* | |
| `Radio.IrqProcess()`, `Radio.BgIrqProcess()` | `lora.handleEvents()` in `loop()`, or `lora.enableBackgroundTask()` | |
| `Radio.ReInit()`, `Radio.IrqProcessAfterDeepSleep()` | *not available* | Deep sleep wake-up handling of SX126x-Arduino has no counterpart. |

### LoRa P2P: SetTxConfig() / SetRxConfig() parameters

| Parameter | WisBlockLoRaWAN | Comment |
| --- | --- | --- |
| `modem` | - | LoRa only, no FSK. |
| `power` [dBm] | `setP2PTxPower(dbm)` | Limited to -9 ... 22 dBm. |
| `bandwidth` | `setP2PBandwidth(WisBlockP2PBandwidth)` | **Same index numbers as SX126x-Arduino** (0 = 125 kHz, 1 = 250 kHz, 2 = 500 kHz, 3 = 62.5 kHz ... 9 = 7.81 kHz), also as `WISBLOCK_BW_125` ... `WISBLOCK_BW_007`. The `AT+PBW` command and the `<bw>` field of `AT+P2P` use the RUI3 numbering instead, see the [AT command manual](WisBlockLoRaWAN-AT-Commands.md). |
| `datarate` (spreading factor) | `setP2PSpreadingFactor(sf)` | The library does not check the range, use 7 ... 12. `AT+PSF` accepts 6 ... 12. |
| `coderate` | `setP2PCodingRate(WisBlockP2PCodingRate)` | **Same numbers as SX126x-Arduino** (1 = 4/5 ... 4 = 4/8), also as `WISBLOCK_CR_4_5` ... `WISBLOCK_CR_4_8`. `AT+PCR` counts from 0. |
| `preambleLen` | `setP2PPreambleLength(symbols)` | |
| `iqInverted` | `setP2PIqInversion(bool)` | Applied to TX and RX. |
| `fixLen`, `payloadLen` | - | Always variable length (explicit header). |
| `crcOn` | - | Always on. |
| `freqHopOn`, `hopPeriod` | - | Not available. |
| `symbTimeout`, `rxContinuous` | `startP2PReceive(timeoutMs)` | `0` = continuous, any other value = single RX with that timeout. |
| `timeout` (TX) | - | Handled by the library. |
| `bandwidthAfc`, `fdev` | - | FSK only, not available. |
| `SetChannel(freq)` | `setP2PFrequency(hz)` | |

### LoRaWAN (from SX126x-Arduino)

| SX126x-Arduino | WisBlockLoRaWAN | Comment |
| --- | --- | --- |
| `lmh_init(&callbacks, params, otaa, class, region)` | `lora.begin(); lora.setWorkMode(WISBLOCK_MODE_LORAWAN);` and the setters below | There is no single init call with a parameter struct. |
| `otaa` (`true` / `false`) | `lora.setJoinMode(WISBLOCK_JOIN_OTAA / WISBLOCK_JOIN_ABP)` | |
| `region` (`LORAMAC_REGION_xxx`) | `lora.setRegion(WISBLOCK_RUI3_BAND_xxx)` | RUI3 band numbering. `LORAMAC_REGION_AS923` + `lmh_setAS923Version()` become `WISBLOCK_RUI3_BAND_AS923_1` ... `_4`. `LORAMAC_REGION_CN779` and LA915 are not supported. |
| `lmh_setSubBandChannels(n)` | `lora.setChannelMask(1 << (n - 1))` | Sub-band `n` of US915 / AU915 / CN470. `0` = all channels. |
| `nodeClass` / `lmh_class_request(c)` | `lora.setDeviceClass(WISBLOCK_CLASS_A / _B / _C)` | Returns `false` while the device is not joined, and is applied automatically after the join. |
| `lmh_setDevEui()`, `lmh_setAppEui()`, `lmh_setAppKey()` | `lora.setOTAAKeys(devEui, joinEui, appKey)` | One call for all three. `AppEUI` is called `joinEui`. Both libraries take the arrays in the order the network server shows them (most significant byte first), so existing arrays can be reused. |
| `lmh_setDevAddr()`, `lmh_setNwkSKey()`, `lmh_setAppSKey()` | `lora.setABPKeys(devAddr, nwkSKey, appSKey)` | |
| `lmh_param_t.adr_enable`, `tx_data_rate` | `lora.setADR(bool)`, `lora.setDataRate(dr)` | Return `false` while not joined, applied automatically after the join. |
| `lmh_param_t.tx_power` | `lora.setTxPower(index)` | Index, region specific. |
| `lmh_param_t.nb_trials` | `lora.setMaxJoinAttempts(n)` | `0` = retry forever. `lora.setJoinReattemptInterval(seconds)` sets the pause between attempts. |
| `lmh_param_t.enable_public_network`, `duty_cycle` | *not available* | Handled by LoRa Basics Modem. |
| `lmh_join()` | `lora.join()` | Asynchronous. `lora.setAutoJoin(true)` joins automatically at start. |
| `lmh_join_status_get()` | `lora.isJoined()`, `lora.joinState()` | |
| `lmh_send(&data, LMH_CONFIRMED_MSG / LMH_UNCONFIRMED_MSG)` | `lora.setConfirmedUplinks(bool)`, then `lora.sendLoRaWAN(port, data, length)` | Confirmed or not is a setting, not a parameter of the send call. |
| `lmh_send_blocking()` | *not available* | Wait for `onLoRaWANTxFinished`. |
| `lmh_setConfRetries()`, `lmh_setSingleChannelGateway()`, `lmh_reset_mac()` | *not available* | |
| `lmh_datarate_set(dr, adr)` | `lora.setDataRate(dr)`, `lora.setADR(adr)` | |
| `lmh_getDevAddr()` | `lora.getDevAddr()` | |
| `callbacks.lmh_has_joined` | `lora.onJoinSuccess(cb)` | `void cb()` |
| `callbacks.lmh_has_joined_failed` | `lora.onJoinFailed(cb)` | `void cb()`. The library retries by itself. |
| `callbacks.lmh_RxData(&data)` | `lora.onLoRaWANRxFinished(cb)` | `void cb(const WisBlockRxResult &result)`: `result.port`, `result.data`, `result.length`, `result.rssi`, `result.snr`. |
| `callbacks.lmh_unconf_finished` | `lora.onLoRaWANTxFinished(cb)` | `void cb(const WisBlockTxResult &result)`: `result.success` is true when the uplink was sent, `result.airtimeMs` is its airtime. |
| `callbacks.lmh_conf_result(bool)` | *not available* | `onLoRaWANTxFinished` is also called for confirmed uplinks, but `result.success` only says that the frame was sent, not that the network acknowledged it. |
| `callbacks.lmh_ConfirmClass` | *not available* | `setDeviceClass()` returns whether the request was accepted. |
| `callbacks.BoardGetBatteryLevel`, `BoardGetUniqueId`, `BoardGetRandomSeed` | *not needed* | Not used by LoRa Basics Modem. |

### Example

SX126x-Arduino:

```cpp
lora_rak4630_init();
RadioEvents.TxDone = OnTxDone;
RadioEvents.RxDone = OnRxDone;
RadioEvents.RxTimeout = OnRxTimeout;
Radio.Init(&RadioEvents);
Radio.SetChannel(868300000);
Radio.SetTxConfig(MODEM_LORA, 14, 0, 0, 7, 1, 8, false, true, 0, 0, false, 3000);
Radio.SetRxConfig(MODEM_LORA, 0, 7, 1, 0, 8, 0, false, 0, true, 0, 0, false, true);
Radio.Rx(0);
...
void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr) { ... }
```

WisBlockLoRaWAN:

```cpp
lora.begin();
lora.setWorkMode(WISBLOCK_MODE_LORA_P2P);
lora.setP2PFrequency(868300000);
lora.setP2PTxPower(14);
lora.setP2PBandwidth(WISBLOCK_BW_125);   // same index as before: 0
lora.setP2PSpreadingFactor(7);
lora.setP2PCodingRate(WISBLOCK_CR_4_5);  // same index as before: 1
lora.setP2PPreambleLength(8);
lora.setP2PIqInversion(false);
lora.setP2PSyncWord(0x1424);             // private network, the default
lora.onP2PTxFinished(onTxDone);
lora.onP2PRxFinished(onRxDone);          // RX timeout and failed TX: result.length == 0
lora.startP2PReceive(0);
...
void onRxDone(const WisBlockRxResult &result) { ... result.data, result.length ... }
```

Set all parameters, including IQ inversion and the sync word, in the sketch (or check them with [hasValidConfig()](#hasvalidconfig)). A value stored in flash earlier stays active if the sketch does not set it.

## Quick Start

```cpp
#include <WisBlockLoRaWAN_all.h>

WisBlockLoRaWAN lora;

// OTAA credentials
uint8_t devEui[8]  = {0xAC, 0x1F, 0x09, 0xFF, 0xFE, 0x06, 0x79, 0xDB};
uint8_t joinEui[8] = {0x70, 0xB3, 0xD5, 0x7E, 0xD0, 0x02, 0x01, 0xE1};
uint8_t appKey[16] = {0x2B, 0x84, 0xE0, 0xB0, 0x9B, 0x68, 0xE5, 0xCB,
                      0x42, 0x17, 0x6F, 0xE7, 0x53, 0xDC, 0xEE, 0x79};

void onJoined()  { Serial.println("Joined"); }
void onJoinFail(){ Serial.println("Join attempt failed, library retries on its own"); }

void setup()
{
    Serial.begin(115200);
    lora.begin();                                   // load saved config, init radio
    lora.setWorkMode(WISBLOCK_MODE_LORAWAN);
    lora.setOTAAKeys(devEui, joinEui, appKey);
    lora.setRegion(WISBLOCK_RUI3_BAND_EU868);       // RUI3 band numbering
    lora.setDeviceClass(WISBLOCK_CLASS_A);
    lora.onJoinSuccess(onJoined);
    lora.onJoinFailed(onJoinFail);
    lora.join();
}

void loop()
{
    lora.handleEvents();                            // not needed with enableBackgroundTask()
}
```

## LoRaWAN Data Types

### WisBlockWorkMode

The top level operating mode of the library.

| Enumerator | Value | Description |
| --- | --- | --- |
| `WISBLOCK_MODE_LORAWAN` | 0 | LoRaWAN mode |
| `WISBLOCK_MODE_LORA_P2P` | 1 | LoRa P2P mode |

```cpp
enum WisBlockWorkMode : uint8_t
{
  WISBLOCK_MODE_LORAWAN  = 0, ///< LoRaWAN mode
  WISBLOCK_MODE_LORA_P2P = 1, ///< LoRa P2P mode
};
```

### WisBlockRUI3Band

The regions of LoRaWAN, using the **RUI3 `AT+BAND` numbering**. This is the enumeration used by `setRegion()`, `getRegion()` and `AT+BAND`.

| Enumerator | Value | Description |
| --- | --- | --- |
| `WISBLOCK_RUI3_BAND_EU433` | 0 | EU433 (433.05 ~ 434.79 MHz, RP002-1.0.4) |
| `WISBLOCK_RUI3_BAND_CN470` | 1 | CN470 ~ 510 |
| `WISBLOCK_RUI3_BAND_RU864` | 2 | RU864 ~ 870 |
| `WISBLOCK_RUI3_BAND_IN865` | 3 | IN865 ~ 867 |
| `WISBLOCK_RUI3_BAND_EU868` | 4 | EU863 ~ 870 |
| `WISBLOCK_RUI3_BAND_US915` | 5 | US902 ~ 928 |
| `WISBLOCK_RUI3_BAND_AU915` | 6 | AU915 ~ 928 |
| `WISBLOCK_RUI3_BAND_KR920` | 7 | KR920 ~ 923 |
| `WISBLOCK_RUI3_BAND_AS923_1` | 8 | AS923-1 |
| `WISBLOCK_RUI3_BAND_AS923_2` | 9 | AS923-2 |
| `WISBLOCK_RUI3_BAND_AS923_3` | 10 | AS923-3 |
| `WISBLOCK_RUI3_BAND_AS923_4` | 11 | AS923-4 |
| `WISBLOCK_RUI3_BAND_LA915` | 12 | LA915 - **not supported**, `setRegion()` returns `false` |
| `WISBLOCK_RUI3_BAND_UNKNOWN` | 0xFF | Returned by `getRegion()` if the active region has no RUI3 band number (`CN470_RP_1_0`, `WW2G4`). Never valid for `setRegion()`. |

```cpp
enum WisBlockRUI3Band : uint8_t
{
  WISBLOCK_RUI3_BAND_EU433   = 0,
  WISBLOCK_RUI3_BAND_CN470   = 1,
  WISBLOCK_RUI3_BAND_RU864   = 2,
  WISBLOCK_RUI3_BAND_IN865   = 3,
  WISBLOCK_RUI3_BAND_EU868   = 4,
  WISBLOCK_RUI3_BAND_US915   = 5,
  WISBLOCK_RUI3_BAND_AU915   = 6,
  WISBLOCK_RUI3_BAND_KR920   = 7,
  WISBLOCK_RUI3_BAND_AS923_1 = 8,
  WISBLOCK_RUI3_BAND_AS923_2 = 9,
  WISBLOCK_RUI3_BAND_AS923_3 = 10,
  WISBLOCK_RUI3_BAND_AS923_4 = 11,
  WISBLOCK_RUI3_BAND_LA915   = 12,
  WISBLOCK_RUI3_BAND_UNKNOWN = 0xFF,
};
```

## _💡 NOTE_
----
_**Two region enumerations exist in the library.** `WisBlockRUI3Band` (above) is the one for application code. `WisBlockRegion` is the internal numbering that mirrors the LoRa Basics Modem (SWL2001) order and is what `lora.getConfig().lorawan.region` holds. Use `wisblockRUI3BandToRegion()` / `wisblockRegionToRUI3Band()` ([Helper Functions](#helper-functions)) to convert between them. Before the change described in the Creation Log (*RUI3 band numbering for setRegion/getRegion*) the API and the AT commands used different numberings, which was an easy source of confusion._

----

### WisBlockRegion

The internal LoRaWAN region numbering (LoRa Basics Modem / SWL2001 order). Only relevant when you read `getConfig().lorawan.region` directly.

| Enumerator | Value | Description |
| --- | --- | --- |
| `WISBLOCK_REGION_EU868` | 0 | EU863 ~ 870 |
| `WISBLOCK_REGION_US915` | 1 | US902 ~ 928 |
| `WISBLOCK_REGION_AU915` | 2 | AU915 ~ 928 |
| `WISBLOCK_REGION_AS923_1` | 3 | AS923-1 |
| `WISBLOCK_REGION_AS923_2` | 4 | AS923-2 |
| `WISBLOCK_REGION_AS923_3` | 5 | AS923-3 |
| `WISBLOCK_REGION_AS923_4` | 6 | AS923-4 |
| `WISBLOCK_REGION_KR920` | 7 | KR920 ~ 923 |
| `WISBLOCK_REGION_IN865` | 8 | IN865 ~ 867 |
| `WISBLOCK_REGION_RU864` | 9 | RU864 ~ 870 |
| `WISBLOCK_REGION_CN470` | 10 | CN470 ~ 510 |
| `WISBLOCK_REGION_CN470_RP_1_0` | 11 | CN470 RP1.0 (no RUI3 band number) |
| `WISBLOCK_REGION_WW2G4` | 12 | 2.4 GHz worldwide (no RUI3 band number, not applicable to the SX1262) |
| `WISBLOCK_REGION_EU433` | 13 | EU433, re-added by this library (see below) |

```cpp
enum WisBlockRegion : uint8_t
{
  WISBLOCK_REGION_EU868 = 0,  WISBLOCK_REGION_US915 = 1,  WISBLOCK_REGION_AU915 = 2,
  WISBLOCK_REGION_AS923_1 = 3, WISBLOCK_REGION_AS923_2 = 4, WISBLOCK_REGION_AS923_3 = 5,
  WISBLOCK_REGION_AS923_4 = 6, WISBLOCK_REGION_KR920 = 7,  WISBLOCK_REGION_IN865 = 8,
  WISBLOCK_REGION_RU864 = 9,  WISBLOCK_REGION_CN470 = 10, WISBLOCK_REGION_CN470_RP_1_0 = 11,
  WISBLOCK_REGION_WW2G4 = 12, WISBLOCK_REGION_EU433 = 13,
};
```

## _💡 NOTE_
----
_**EU433** is not part of Semtech's current LoRa Basics Modem anymore. The library ships its own EU433 region (`region_eu_433.c/.h/_defs.h`) based on the LoRaWAN Regional Parameters **RP002-1.0.4** defaults:_

- _Frequency band 433.05 - 434.79 MHz_
- _Default join channels 433.175, 433.375 and 433.575 MHz (DR0 - DR5)_
- _CFList type 0 (frequency list), up to 16 channels_
- _RX2 434.665 MHz, DR0. Class B beacon and ping slot frequency 434.665 MHz, DR3_
- _Data rates DR0 - DR7 (SF12 - SF7 BW125, SF7 BW250, FSK 50 kbps), no LR-FHSS_
- _TX power: default max EIRP +12 dBm, 6 power steps (index 0 - 5) of 2 dB_
- _One global duty cycle band with 1 %, no dwell time, no LBT_

----

### WisBlockDeviceClass

The LoRaWAN device classes.

| Enumerator | Value | Description |
| --- | --- | --- |
| `WISBLOCK_CLASS_A` | 0 | Class A |
| `WISBLOCK_CLASS_B` | 1 | Class B |
| `WISBLOCK_CLASS_C` | 2 | Class C |

```cpp
enum WisBlockDeviceClass : uint8_t
{
  WISBLOCK_CLASS_A = 0, ///< Class A
  WISBLOCK_CLASS_B = 1, ///< Class B
  WISBLOCK_CLASS_C = 2, ///< Class C
};
```

### WisBlockJoinMode

The LoRaWAN network join modes. Note that the numbering is **the opposite of RUI3** (`RAK_LORA_ABP = 0`, `RAK_LORA_OTAA = 1`)!

| Enumerator | Value | Description |
| --- | --- | --- |
| `WISBLOCK_JOIN_OTAA` | 0 | Over-the-air activation |
| `WISBLOCK_JOIN_ABP` | 1 | Activation by personalization |

```cpp
enum WisBlockJoinMode : uint8_t
{
  WISBLOCK_JOIN_OTAA = 0, ///< over-the-air activation
  WISBLOCK_JOIN_ABP  = 1, ///< activation by personalization
};
```

### WisBlockJoinState

The state of the join procedure, returned by `joinState()`.

| Enumerator | Value | Description |
| --- | --- | --- |
| `WISBLOCK_JOIN_IDLE` | 0 | No join started |
| `WISBLOCK_JOIN_IN_PROGRESS` | 1 | A join attempt is running |
| `WISBLOCK_JOIN_SUCCEEDED` | 2 | Joined |
| `WISBLOCK_JOIN_FAILED` | 3 | This attempt failed, the next one is already scheduled |
| `WISBLOCK_JOIN_GAVE_UP` | 4 | `maxJoinAttempts` reached, retrying has stopped |

```cpp
enum WisBlockJoinState : uint8_t
{
  WISBLOCK_JOIN_IDLE = 0, WISBLOCK_JOIN_IN_PROGRESS = 1, WISBLOCK_JOIN_SUCCEEDED = 2,
  WISBLOCK_JOIN_FAILED = 3, WISBLOCK_JOIN_GAVE_UP = 4,
};
```

### WisBlockP2PBandwidth

The LoRa P2P bandwidth options (SX126x `LORA_BW_*` indices).

| Enumerator | Value | Description |
| --- | --- | --- |
| `WISBLOCK_BW_125` | 0 | 125 kHz |
| `WISBLOCK_BW_250` | 1 | 250 kHz |
| `WISBLOCK_BW_500` | 2 | 500 kHz |
| `WISBLOCK_BW_062` | 3 | 62.5 kHz |
| `WISBLOCK_BW_041` | 4 | 41.67 kHz |
| `WISBLOCK_BW_031` | 5 | 31.25 kHz |
| `WISBLOCK_BW_020` | 6 | 20.83 kHz |
| `WISBLOCK_BW_015` | 7 | 15.63 kHz |
| `WISBLOCK_BW_010` | 8 | 10.42 kHz |
| `WISBLOCK_BW_007` | 9 | 7.81 kHz |

```cpp
enum WisBlockP2PBandwidth : uint8_t
{
  WISBLOCK_BW_125 = 0, WISBLOCK_BW_250 = 1, WISBLOCK_BW_500 = 2, WISBLOCK_BW_062 = 3, WISBLOCK_BW_041 = 4,
  WISBLOCK_BW_031 = 5, WISBLOCK_BW_020 = 6, WISBLOCK_BW_015 = 7, WISBLOCK_BW_010 = 8, WISBLOCK_BW_007 = 9,
};
```

### WisBlockP2PCodingRate

The LoRa P2P coding rates.

| Enumerator | Value | Description |
| --- | --- | --- |
| `WISBLOCK_CR_4_5` | 1 | 4/5 |
| `WISBLOCK_CR_4_6` | 2 | 4/6 |
| `WISBLOCK_CR_4_7` | 3 | 4/7 |
| `WISBLOCK_CR_4_8` | 4 | 4/8 |

```cpp
enum WisBlockP2PCodingRate : uint8_t
{
  WISBLOCK_CR_4_5 = 1, WISBLOCK_CR_4_6 = 2, WISBLOCK_CR_4_7 = 3, WISBLOCK_CR_4_8 = 4,
};
```

### WisBlockCADResult

The result of a Channel Activity Detection, delivered to the CAD callback.

| Enumerator | Value | Description |
| --- | --- | --- |
| `WISBLOCK_CAD_CHANNEL_CLEAR` | 0 | No activity detected |
| `WISBLOCK_CAD_CHANNEL_DETECTED` | 1 | Activity detected |
| `WISBLOCK_CAD_ERROR` | 2 | CAD failed |

```cpp
enum WisBlockCADResult : uint8_t
{
  WISBLOCK_CAD_CHANNEL_CLEAR = 0, WISBLOCK_CAD_CHANNEL_DETECTED = 1, WISBLOCK_CAD_ERROR = 2,
};
```

### WisBlockAtStatus

The status a custom AT command handler returns, see [addCustomATCommand()](#addcustomatcommand).

| Enumerator | Value | Description |
| --- | --- | --- |
| `WISBLOCK_AT_OK` | 0 | The library replies `OK` |
| `WISBLOCK_AT_ERROR` | 1 | The library replies `AT_ERROR` |
| `WISBLOCK_AT_PARAM_ERROR` | 2 | The library replies `AT_PARAM_ERROR` |

```cpp
enum WisBlockAtStatus { WISBLOCK_AT_OK = 0, WISBLOCK_AT_ERROR, WISBLOCK_AT_PARAM_ERROR };
```

### WisBlockRxResult

The structure of a received frame (LoRaWAN and LoRa P2P). It is passed to the RX callbacks.

```cpp
struct WisBlockRxResult
{
  uint8_t  port = 0;
  uint8_t  data[242] = {0};
  uint8_t  length = 0;
  int16_t  rssi = 0;
  int8_t   snr = 0;
  bool     fpending = false;
  bool     isMulticast = false;
  uint8_t  multicastGroupId = 0;
};
```

#### port

The application port (FPort) of a LoRaWAN downlink. `0` for LoRa P2P.

```cpp
uint8_t port
```

#### data

The received payload.

```cpp
uint8_t data[242]
```

#### length

The number of valid bytes in `data`.

```cpp
uint8_t length
```

#### rssi

RSSI of the received packet in dBm.

```cpp
int16_t rssi
```

#### snr

Signal-to-noise ratio of the received packet in dB.

```cpp
int8_t snr
```

#### fpending

LoRaWAN only. The FPending bit of the downlink: the network has more downlinks queued. By default the library sends an empty uplink to fetch them, see `setFetchPendingDownlinks()`.

```cpp
bool fpending
```

#### isMulticast

LoRaWAN only. `true` if the downlink arrived on a multicast group RX window (Class B or C).

```cpp
bool isMulticast
```

#### multicastGroupId

The multicast group (0 - 3) the downlink arrived on. Only valid if `isMulticast` is `true`, so check `isMulticast` first (group 0 is a valid ID).

```cpp
uint8_t multicastGroupId
```

### WisBlockTxResult

The structure of a finished transmission, passed to the TX callbacks.

```cpp
struct WisBlockTxResult
{
  bool     success = false;
  uint32_t airtimeMs = 0;
};
```

#### success

`true` if the frame was transmitted. `false` if it was discarded before it was sent.

```cpp
bool success
```

#### airtimeMs

The time on air of the frame in milliseconds.

```cpp
uint32_t airtimeMs
```

### WisBlockLinkCheckResult

The result of a LinkCheck request.

```cpp
struct WisBlockLinkCheckResult
{
  uint8_t demodMargin = 0;
  uint8_t gatewayCount = 0;
};
```

#### demodMargin

Demodulation margin in dB (0 - 254) reported by the network.

```cpp
uint8_t demodMargin
```

#### gatewayCount

The number of gateways that received the LinkCheck request.

```cpp
uint8_t gatewayCount
```

### WisBlockTimeAnswer

The answer of a DeviceTimeReq.

```cpp
struct WisBlockTimeAnswer
{
  uint32_t gpsEpochSeconds = 0;
  uint32_t fractionalSeconds = 0;
};
```

#### gpsEpochSeconds

Seconds since the GPS epoch (6 Jan 1980).

```cpp
uint32_t gpsEpochSeconds
```

#### fractionalSeconds

Fractional second as delivered by the network (1/256 s steps).

```cpp
uint32_t fractionalSeconds
```

### WisBlockMulticastGroup

The configuration of one multicast group. Up to `WISBLOCK_MULTICAST_GROUP_COUNT` (4) groups can be active at once, a hard limit of LoRa Basics Modem. Groups are **not saved** to flash.

```cpp
struct WisBlockMulticastGroup
{
  bool configured = false;
  WisBlockDeviceClass deviceClass = WISBLOCK_CLASS_C;
  uint32_t devAddr = 0;
  uint8_t  nwkSKey[16] = {0};
  uint8_t  appSKey[16] = {0};
  uint32_t frequencyHz = 0;
  uint8_t  dataRate = 0;
  uint8_t  periodicity = 0;
};
```

#### configured

`true` if this group ID is in use.

```cpp
bool configured
```

#### deviceClass

`WISBLOCK_CLASS_B` or `WISBLOCK_CLASS_C`. Class A is not valid for a multicast group.

```cpp
WisBlockDeviceClass deviceClass
```

#### devAddr

The multicast group address.

```cpp
uint32_t devAddr
```

#### nwkSKey

The multicast network session key.

```cpp
uint8_t nwkSKey[16]
```

#### appSKey

The multicast application session key.

```cpp
uint8_t appSKey[16]
```

#### frequencyHz

The downlink frequency of the group in Hz. Must be valid for the active region.

```cpp
uint32_t frequencyHz
```

#### dataRate

The downlink data rate of the group.

```cpp
uint8_t dataRate
```

#### periodicity

Class B ping slot periodicity (0 - 7). Ignored for Class C, but still expected by the AT command.

```cpp
uint8_t periodicity
```

## _💡 NOTE_
----
_Unlike the OTAA/ABP keys, the multicast session keys **can be read back** in plaintext with `getMulticastGroup()` and `AT+LSTMULC=?`. This is intentional and matches RUI3's `AT+LSTMULC` behavior (see Creation Log, entry 2026-09-25)._

----

### WisBlockLoRaWANSettings

The persisted LoRaWAN settings, available as `lora.getConfig().lorawan`. Read-only for the application, change them with the setter functions.

#### region

Active region in the **internal** numbering. Use `lora.getRegion()` for the RUI3 band.

```cpp
WisBlockRegion region
```

#### deviceClass

The **requested** device class. It becomes active after the join.

```cpp
WisBlockDeviceClass deviceClass
```

#### joinMode

OTAA or ABP.

```cpp
WisBlockJoinMode joinMode
```

#### dataRate

The **requested** data rate (used if ADR is off).

```cpp
uint8_t dataRate
```

#### adrEnabled

The **requested** ADR state. Default `true`.

```cpp
bool adrEnabled
```

#### txPower

The stored TX power index. See the note at `setTxPower()`.

```cpp
uint8_t txPower
```

#### confirmedUplinks

`sendLoRaWAN()` sends confirmed uplinks if `true`. Default `false`.

```cpp
bool confirmedUplinks
```

#### channelMask

Sub-band selection for US915, AU915 and CN470. `0` = all channels.

```cpp
uint16_t channelMask
```

#### pingSlotPeriodicity

Class B ping slot periodicity 0 - 7.

```cpp
uint8_t pingSlotPeriodicity
```

#### fetchPendingDownlinks

Automatically fetch pending downlinks (Class A). Default `true`.

```cpp
bool fetchPendingDownlinks
```

#### autoJoin

Join automatically when the LoRaWAN engine starts. Default `false`.

```cpp
bool autoJoin
```

#### joinReattemptIntervalS

Seconds between join attempts, 7 - 255. Default 8.

```cpp
uint8_t joinReattemptIntervalS
```

#### maxJoinAttempts

Maximum join attempts, 0 = unlimited (default).

```cpp
uint8_t maxJoinAttempts
```

#### otaa

`devEui[8]`, `joinEui[8]`, `appKey[16]`.

```cpp
WisBlockOTAAKeys otaa
```

#### abp

`devAddr`, `nwkSKey[16]`, `appSKey[16]`.

```cpp
WisBlockABPKeys abp
```

### WisBlockP2PSettings

The persisted LoRa P2P settings, available as `lora.getP2PSettings()`.

#### frequencyHz

Frequency in Hz. Default 916000000.

```cpp
uint32_t frequencyHz
```

#### spreadingFactor

SF7 - SF12. Default 7.

```cpp
uint8_t spreadingFactor
```

#### bandwidth

Default `WISBLOCK_BW_125`.

```cpp
WisBlockP2PBandwidth bandwidth
```

#### codingRate

Default `WISBLOCK_CR_4_5`.

```cpp
WisBlockP2PCodingRate codingRate
```

#### preambleLength

Preamble length in symbols. Default 8.

```cpp
uint16_t preambleLength
```

#### txPowerDbm

TX power in dBm. Default 14.

```cpp
int8_t txPowerDbm
```

#### cadEnabled

Do a CAD before each transmission.

```cpp
bool cadEnabled
```

#### symbolTimeout

RX symbol timeout.

```cpp
uint16_t symbolTimeout
```

#### rxBoostedGainEnabled

SX1262 boosted RX gain. Default `true`.

```cpp
bool rxBoostedGainEnabled
```

#### iqInversion

Inverts the IQ signals on TX and RX. Both ends of a link must match. Default `false`. Set with `setP2PIqInversion()` or `AT+IQINVER`.

```cpp
bool iqInversion
```

#### syncWord

16 bit LoRa sync word. `0x1424` private (default), `0x3444` public (LoRaWAN). Both ends of a link must match. Set with `setP2PSyncWord()` or `AT+SYNCWORD`.

```cpp
uint16_t syncWord
```

### WisBlockPersistedConfig

The complete persisted configuration, returned by `lora.getConfig()`.

#### workMode

LoRaWAN or LoRa P2P.

```cpp
WisBlockWorkMode workMode
```

#### lowPowerEnabled

Low power flag, see `setLowPowerEnabled()`.

```cpp
bool lowPowerEnabled
```

#### alias

The device alias (`AT+ALIAS`).

```cpp
char alias[32]
```

#### firmwarever

The firmware version label (`AT+FIRMWAREVER`).

```cpp
char firmwarever[32]
```

#### lorawan

LoRaWAN settings.

```cpp
WisBlockLoRaWANSettings lorawan
```

#### p2p

LoRa P2P settings.

```cpp
WisBlockP2PSettings p2p
```

## LoRaWAN Callbacks

The callbacks are plain C function pointers (no captures). Register them before `join()`. All callbacks run in the context that pumps the engine: `loop()` when you call `handleEvents()`, or the background task if [enableBackgroundTask()](#enablebackgroundtask) is active. Keep them short and do not block.

| | |
| --- | --- |
| **Callback type** | Signature |
| **JoinSuccessCb** | `void (*)()` |
| **JoinFailedCb** | `void (*)()` |
| **TxFinishedCb** | `void (*)(const WisBlockTxResult &)` |
| **RxFinishedCb** | `void (*)(const WisBlockRxResult &)` |
| **TimeRequestCb** | `void (*)(bool success, const WisBlockTimeAnswer &)` |
| **LinkCheckCb** | `void (*)(bool success, const WisBlockLinkCheckResult &)` |
| **CadResultCb (P2P)** | `void (*)(WisBlockCADResult)` |

### onJoinSuccess()

Registers the callback that is called when the join procedure succeeded (OTAA join accept, or ABP activation).

```cpp
lora.onJoinSuccess(callback);
```

| | |
| --- | --- |
| **Function** | `void onJoinSuccess(LoRaWANEngine::JoinSuccessCb cb)` |
| **Parameters** | **cb** - the callback function |

<details>
<summary><b>Click to view the code</b></summary>

```cpp
WisBlockLoRaWAN lora;

void joined() { Serial.println("Joined"); }

void setup()
{
  lora.begin();
  lora.onJoinSuccess(joined);
}
```

</details>

### onJoinFailed()

Registers the callback that is called after **every failed join attempt**.

```cpp
lora.onJoinFailed(callback);
```

| | |
| --- | --- |
| **Function** | `void onJoinFailed(LoRaWANEngine::JoinFailedCb cb)` |
| **Parameters** | **cb** - the callback function |

## _⚠️ WARNING_
----
_Do **not** call `lora.join()` from this callback. The library already retries on its own (LoRa Basics Modem's own back-off, or the interval and maximum set with `setJoinReattemptInterval()` / `setMaxJoinAttempts()`). Calling `join()` resets the attempt counter, so `maxJoinAttempts` is never reached, and it races the library's scheduled retry (Creation Log, entry 2026-09-17 *AT+JOIN parameters*)._

_To find out if this was the **last** attempt, check `lora.joinState() == WISBLOCK_JOIN_GAVE_UP` inside the callback._

----

<details>
<summary><b>Click to view the code</b></summary>

```cpp
WisBlockLoRaWAN lora;

void joinFailed()
{
  if (lora.joinState() == WISBLOCK_JOIN_GAVE_UP)
    Serial.println("Giving up");
  else
    Serial.println("Failed, retrying");
}

void setup()
{
  lora.begin();
  lora.onJoinFailed(joinFailed);
}
```

</details>

### onLoRaWANTxFinished()

Registers the callback that is called when an uplink is finished (or was discarded).

```cpp
lora.onLoRaWANTxFinished(callback);
```

| | |
| --- | --- |
| **Function** | `void onLoRaWANTxFinished(LoRaWANEngine::TxFinishedCb cb)` |
| **Parameters** | **cb** - the callback function, receives a `WisBlockTxResult` |

## 💡 NOTE

----
_`airtimeMs` is calculated from the active data rate and payload size. If the airtime could not be determined it is reported as 0._

----

<details>
<summary><b>Click to view the code</b></summary>

```cpp
WisBlockLoRaWAN lora;

void txDone(const WisBlockTxResult &r)
{
  Serial.printf("TX %s, airtime %lu ms\n", r.success ? "ok" : "NOT SENT", r.airtimeMs);
}

void setup()
{
  lora.begin();
  lora.onLoRaWANTxFinished(txDone);
}
```

</details>

### onLoRaWANRxFinished()

Registers the callback that is called when a downlink was received.

```cpp
lora.onLoRaWANRxFinished(callback);
```

| | |
| --- | --- |
| **Function** | `void onLoRaWANRxFinished(LoRaWANEngine::RxFinishedCb cb)` |
| **Parameters** | **cb** - the callback function, receives a `WisBlockRxResult` |

<details>
<summary><b>Click to view the code</b></summary>

```cpp
WisBlockLoRaWAN lora;

void rxDone(const WisBlockRxResult &r)
{
  Serial.printf("RX port %d, %d bytes, RSSI %d, SNR %d%s\n", r.port, r.length, r.rssi, r.snr,
                r.isMulticast ? " (multicast)" : "");
  for (int i = 0; i < r.length; i++) Serial.printf("%02X", r.data[i]);
  Serial.println();
}

void setup()
{
  lora.begin();
  lora.onLoRaWANRxFinished(rxDone);
}
```

</details>

### onTimeRequestAnswer()

Registers the callback that is called with the answer of a DeviceTimeReq, see [requestDeviceTime()](#requestdevicetime).

```cpp
lora.onTimeRequestAnswer(callback);
```

| | |
| --- | --- |
| **Function** | `void onTimeRequestAnswer(LoRaWANEngine::TimeRequestCb cb)` |
| **Parameters** | **cb** - the callback function |

<details>
<summary><b>Click to view the code</b></summary>

```cpp
WisBlockLoRaWAN lora;

void timeAnswer(bool ok, const WisBlockTimeAnswer &t)
{
  if (ok) Serial.printf("GPS epoch %lu s\n", t.gpsEpochSeconds);
}

void setup()
{
  lora.begin();
  lora.onTimeRequestAnswer(timeAnswer);
}
```

</details>

### onLinkCheckAnswer()

Registers the callback that is called with the answer of a LinkCheckReq, see [requestLinkCheck()](#requestlinkcheck).

```cpp
lora.onLinkCheckAnswer(callback);
```

| | |
| --- | --- |
| **Function** | `void onLinkCheckAnswer(LoRaWANEngine::LinkCheckCb cb)` |
| **Parameters** | **cb** - the callback function |

<details>
<summary><b>Click to view the code</b></summary>

```cpp
WisBlockLoRaWAN lora;

void linkCheck(bool ok, const WisBlockLinkCheckResult &r)
{
  if (ok) Serial.printf("Margin %d dB, %d gateway(s)\n", r.demodMargin, r.gatewayCount);
}

void setup()
{
  lora.begin();
  lora.onLinkCheckAnswer(linkCheck);
}
```

</details>

## System and Engine Control

### begin()

Loads the saved configuration (or the factory defaults), initializes the board pins, the flash storage and the radio. Call it **once** from `setup()`, before any other call.

```cpp
lora.begin();
```

| | |
| --- | --- |
| **Function** | `void begin()` |

## 💡 NOTE

----
_`begin()` deliberately does **not** start the LoRaWAN engine. The engine (LBM initialization, region, class and ADR setup) is started lazily by the first LoRaWAN specific call, e.g. `setWorkMode(WISBLOCK_MODE_LORAWAN)`, `setRegion()`, `setOTAAKeys()` or `join()`. A P2P-only application therefore never starts LoRaWAN, which would otherwise fight the P2P engine for the same radio (Creation Log, *LoRaWAN engine started unconditionally, even in P2P-only builds*)._

----

<details>
<summary><b>Click to view the code</b></summary>

```cpp
WisBlockLoRaWAN lora;

void setup()
{
  Serial.begin(115200);
  lora.begin();
  lora.setWorkMode(WISBLOCK_MODE_LORAWAN);
}
```

</details>

### handleEvents()

Pumps the LoRaWAN or P2P engine. Call it in **every** `loop()` unless the [background task](#enablebackgroundtask) is active, in which case the call is a harmless no-op.

```cpp
uint32_t wait = lora.handleEvents();
```

| | |
| --- | --- |
| **Function** | `uint32_t handleEvents()` |
| **Returns** | the time in milliseconds after which `handleEvents()` must be called again at the latest. Only relevant for the background task, safe to ignore in a simple `loop()`. |

<details>
<summary><b>Click to view the code</b></summary>

```cpp
WisBlockLoRaWAN lora;

void loop()
{
  lora.handleEvents();
}
```

</details>

### setWorkMode()

Sets the operating mode. Switching to `WISBLOCK_MODE_LORAWAN` starts the LoRaWAN engine.

```cpp
lora.setWorkMode(mode);
```

| | |
| --- | --- |
| **Function** | `void setWorkMode(WisBlockWorkMode mode)` |
| **Parameters** | **mode** - `WISBLOCK_MODE_LORAWAN` or `WISBLOCK_MODE_LORA_P2P` |

<details>
<summary><b>Click to view the code</b></summary>

```cpp
WisBlockLoRaWAN lora;

void setup()
{
  lora.begin();
  lora.setWorkMode(WISBLOCK_MODE_LORA_P2P);
  Serial.println(lora.getWorkMode() == WISBLOCK_MODE_LORAWAN ? "LoRaWAN" : "P2P");
}
```

</details>

`WisBlockWorkMode getWorkMode() const` returns the current mode.

### enableBackgroundTask()

Starts a FreeRTOS task that drives the engine. `loop()` no longer has to call `handleEvents()` and the MCU can idle in between events, which is the recommended way for low power applications.

```cpp
bool ok = lora.enableBackgroundTask();
```

| | |
| --- | --- |
| **Function** | `bool enableBackgroundTask()` |
| **Returns** | bool |
| **Return Values** | **TRUE** the background task is running<br/>**FALSE** FreeRTOS is not available, keep calling `handleEvents()` from `loop()` |

## 💡 NOTE

----
_Call it after `begin()`. It works out of the box on RAK4631 and RAK3312. RAK11310 needs a FreeRTOS-Kernel port added to the project first._

----

## ⚠️ WARNING

----
_Only **one** `WisBlockLoRaWAN` instance can use the background task. Do not combine it with `sleep()`._

----

<details>
<summary><b>Click to view the code</b></summary>

```cpp
WisBlockLoRaWAN lora;

void setup()
{
  lora.begin();
  lora.setWorkMode(WISBLOCK_MODE_LORAWAN);
  if (!lora.enableBackgroundTask())
    Serial.println("No FreeRTOS, use handleEvents() in loop()");
}

void loop()
{
  // application work only
}
```

</details>

### lockLbm() / unlockLbm()

Take and release the mutex that protects the LoRa Basics Modem from concurrent access. Only needed if you call library functions from **another task** than the one that owns the background task (the AT command layer does this automatically).

```cpp
lora.lockLbm();
// ... library calls from your own task ...
lora.unlockLbm();
```

| | |
| --- | --- |
| **Function** | `void lockLbm()<br/>void unlockLbm()` |

## 💡 NOTE

----
_No-op if `enableBackgroundTask()` was never called successfully._

----

### sleep()

Parks the MCU in a low power wait until the SX1262 DIO1 interrupt fires or the time is over. Per platform: `waitForEvent()` on RAK4631, `esp_light_sleep_start()` on RAK3312, a `__wfi()` loop on RAK11310.

```cpp
lora.sleep(maxDurationMs);
```

| | |
| --- | --- |
| **Function** | `void sleep(uint32_t maxDurationMs = 0)` |
| **Parameters** | **maxDurationMs** - maximum sleep time in ms, `0` = wait for DIO1 only |

## 💡 NOTE

----
_It does not touch the radio. In P2P mode call `sleepRadio()` first. LoRaWAN mode already sleeps the radio between tasks. Not meant to be combined with `enableBackgroundTask()`._

----

### setLowPowerEnabled()

Sets the low power flag that is stored in the configuration.

```cpp
lora.setLowPowerEnabled(enabled);
```

| | |
| --- | --- |
| **Function** | `void setLowPowerEnabled(bool enabled)` |
| **Parameters** | **enabled** - TRUE or FALSE |

`bool isLowPowerEnabled() const` returns the flag.

## Device Identity

### setAlias()

Sets the free-form device label (RUI3 `AT+ALIAS`). Stored with the configuration, not used for operation.

```cpp
lora.setAlias("MyNode");
```

| | |
| --- | --- |
| **Function** | `bool setAlias(const char *alias)` |
| **Parameters** | **alias** - string of up to 16 characters |
| **Returns** | bool |
| **Return Values** | **TRUE** set<br/>**FALSE** `NULL` pointer or more than 16 characters, nothing changed |

## 💡 NOTE

----

_`getAlias()` returns a `const char *`. The factory default (for example `WISBLOCK_BASICMODEM_RAK4631`) is longer than 16 characters and can be read but not entered again through this setter._

----

<details>
<summary><b>Click to view the code</b></summary>

```cpp
WisBlockLoRaWAN lora;

void setup()
{
  lora.begin();
  Serial.println(lora.setAlias("Node-01") ? "OK" : "Fail");
  Serial.println(lora.getAlias());
}
```

</details>

### setFirmwareVer()

Sets the free-form firmware label (RUI3 `AT+FIRMWAREVER`).

```cpp
lora.setFirmwareVer("MyFW_1.2");
```

| | |
| --- | --- |
| **Function** | `bool setFirmwareVer(const char *firmwarever)` |
| **Parameters** | **firmwarever** - string of up to 31 characters |
| **Returns** | bool |
| **Return Values** | **TRUE** set<br/>**FALSE** `NULL` pointer or 32 characters and more |

## 💡 NOTE

----
_`getFirmwareVer()` returns a `const char *`._

----

## Keys, IDs, and EUIs Management

### setOTAAKeys()

Sets the OTAA credentials and switches the join mode to OTAA.

```cpp
lora.setOTAAKeys(devEui, joinEui, appKey);
```

| | |
| --- | --- |
| **Function** | `void setOTAAKeys(const uint8_t devEui[8], const uint8_t joinEui[8], const uint8_t appKey[16])` |
| **Parameters** | **devEui** - 8 bytes, MSB first<br/>**joinEui** - 8 bytes (AppEUI), MSB first<br/>**appKey** - 16 bytes. Also used as NwkKey. |

## 💡 NOTE

----
_The keys are applied to the LoRa Basics Modem on every call, even if the LoRaWAN engine was not started yet. `getConfig().lorawan.otaa` shows the values. The keys have **no read-back** through the stack itself._

----

<details>
<summary><b>Click to view the code</b></summary>

```cpp
WisBlockLoRaWAN lora;

uint8_t devEui[8]  = {0xAC,0x1F,0x09,0xFF,0xFE,0x06,0x79,0xDB};
uint8_t joinEui[8] = {0x70,0xB3,0xD5,0x7E,0xD0,0x02,0x01,0xE1};
uint8_t appKey[16] = {0x2B,0x84,0xE0,0xB0,0x9B,0x68,0xE5,0xCB,0x42,0x17,0x6F,0xE7,0x53,0xDC,0xEE,0x79};

void setup()
{
  lora.begin();
  lora.setOTAAKeys(devEui, joinEui, appKey);
}
```

</details>

### setABPKeys()

Sets the ABP credentials and switches the join mode to ABP.

```cpp
lora.setABPKeys(devAddr, nwkSKey, appSKey);
```

| | |
| --- | --- |
| **Function** | `void setABPKeys(uint32_t devAddr, const uint8_t nwkSKey[16], const uint8_t appSKey[16])` |
| **Parameters** | **devAddr** - the device address<br/>**nwkSKey** - 16 bytes network session key<br/>**appSKey** - 16 bytes application session key |

<details>
<summary><b>Click to view the code</b></summary>

```cpp
WisBlockLoRaWAN lora;

uint8_t nwkSKey[16] = {0xD6,0x03,0x37,0xAC,0x97,0x4C,0x43,0x2F,0xF3,0x7A,0xF9,0xA7,0x9B,0xE8,0x50,0xF7};
uint8_t appSKey[16] = {0x25,0xC4,0xF1,0xD1,0x78,0xC8,0x8D,0x01,0xA8,0x80,0xC2,0x79,0xA7,0x9F,0x34,0x3B};

void setup()
{
  lora.begin();
  lora.setABPKeys(0x26011234, nwkSKey, appSKey);
}
```

</details>

### setJoinMode()

Selects OTAA or ABP without changing any key.

```cpp
lora.setJoinMode(mode);
```

| | |
| --- | --- |
| **Function** | `void setJoinMode(WisBlockJoinMode mode)` |
| **Parameters** | **mode** - `WISBLOCK_JOIN_OTAA` or `WISBLOCK_JOIN_ABP` |

## ⚠️ WArning

----
_The values are **reversed compared with RUI3**: OTAA is `0` and ABP is `1` here._

----

### getDevAddr()

Returns the current device address. For OTAA it is the address the network assigned in the join accept (`0` before the first join). For ABP it is the configured address.

```cpp
uint32_t addr = lora.getDevAddr();
```

| | |
| --- | --- |
| **Function** | `uint32_t getDevAddr() const` |
| **Returns** | the device address |

## 💡 NOTE

----
_The NwkSKey and AppSKey derived by an OTAA join cannot be read. LoRa Basics Modem has no getter for them, by design._

----

## LoRaWAN Network Configuration

### setRegion()

Sets the LoRaWAN region using the **RUI3 band numbering** ([WisBlockRUI3Band](#wisblockrui3band)). The value is converted internally to the LoRa Basics Modem numbering.

```cpp
lora.setRegion(WISBLOCK_RUI3_BAND_EU868);
```

| | |
| --- | --- |
| **Function** | `bool setRegion(WisBlockRUI3Band band)` |
| **Parameters** | **band** - the region, see [WisBlockRUI3Band](#wisblockrui3band) |
| **Returns** | bool |
| **Return Values** | **TRUE** region set<br/>**FALSE** the band is not supported (`WISBLOCK_RUI3_BAND_LA915`, `WISBLOCK_RUI3_BAND_UNKNOWN`, any value out of range), nothing changed |

## ⚠️ WARNING

----
_The region can only be changed **before the device joins**. After a join the LoRa Basics Modem refuses the change._

----

## 💡 NOTE

----
_The region is saved with `saveConfig()` and restored on the next boot. If the join still uses the wrong frequencies, check that the `REGION_xxx` define of your region is part of the build (see `extra_script.py`), otherwise the modem silently keeps the previous region._

----

<details>
<summary><b>Click to view the code</b></summary>

```cpp
WisBlockLoRaWAN lora;

void setup()
{
  lora.begin();
  lora.setWorkMode(WISBLOCK_MODE_LORAWAN);
  if (!lora.setRegion(WISBLOCK_RUI3_BAND_EU433))
    Serial.println("Region not supported");
  Serial.printf("Band = %d\n", (int)lora.getRegion());
}
```

</details>

### getRegion()

Returns the active region in the **RUI3 band numbering**.

```cpp
WisBlockRUI3Band band = lora.getRegion();
```

| | |
| --- | --- |
| **Function** | `WisBlockRUI3Band getRegion() const` |
| **Returns** | the region as [WisBlockRUI3Band](#wisblockrui3band) |
| **Return Values** | **WISBLOCK_RUI3_BAND_UNKNOWN (0xFF)** the active region has no RUI3 band number (`CN470_RP_1_0`, `WW2G4`). Read `getConfig().lorawan.region` in that case. |

<details>
<summary><b>Click to view the code</b></summary>

```cpp
WisBlockLoRaWAN lora;

void setup()
{
  lora.begin();
  Serial.printf("Band = %d\n", (int)lora.getRegion());  // 0 = EU433, 4 = EU868, 5 = US915 ...
}
```

</details>

### setDeviceClass()

Sets the LoRaWAN device class.

```cpp
lora.setDeviceClass(WISBLOCK_CLASS_C);
```

| | |
| --- | --- |
| **Function** | `bool setDeviceClass(WisBlockDeviceClass deviceClass)` |
| **Parameters** | **deviceClass** - `WISBLOCK_CLASS_A`, `_B` or `_C` |
| **Returns** | bool |
| **Return Values** | **TRUE** the class is active now<br/>**FALSE** the device is not joined yet. The request is stored and applied automatically as soon as the join is done. |

## 💡 NOTE

----
_A class change is only possible on a joined device. Before the fix in the Creation Log (2026-09-14 *Class C bug fix*) the request was silently dropped when made before the join. Now it is remembered and re-applied after the join and again after each uplink until it succeeded, so it is enough to call it once in `setup()`._

----

<details>
<summary><b>Click to view the code</b></summary>

```cpp
WisBlockLoRaWAN lora;

void setup()
{
  lora.begin();
  lora.setWorkMode(WISBLOCK_MODE_LORAWAN);
  lora.setDeviceClass(WISBLOCK_CLASS_C);   // FALSE now, becomes active after the join
  lora.join();
}
```

</details>

### setADR()

Enables or disables the adaptive data rate.

```cpp
lora.setADR(false);
```

| | |
| --- | --- |
| **Function** | `bool setADR(bool enabled)` |
| **Parameters** | **enabled** - TRUE = network controlled ADR (default), FALSE = fixed data rate, see `setDataRate()` |
| **Returns** | bool |
| **Return Values** | **TRUE** the setting is active on the radio now<br/>**FALSE** the setting is **stored but not active yet**. It is retried after every uplink. |

## ⚠️ WARNING

----
_A `FALSE` return does **not** mean the call was rejected. Right after a join only the region's default channels are enabled. If the requested data rate is not supported by them, LoRa Basics Modem refuses the custom ADR profile and stays network controlled, until the network's `NewChannelReq` MAC commands widen the channel list. The library retries after each uplink. Until then `getConfig().lorawan.adrEnabled` shows what was **requested**, not what is active (Creation Log, *setADR(false) silently not taking effect*)._

_While ADR is off the device also refuses the data rate and TX power part of `LinkADRReq` commands. Some older network servers log this as an invalid data rate / TX power answer, which is expected. The channel mask part is still accepted._

----

<details>
<summary><b>Click to view the code</b></summary>

```cpp
WisBlockLoRaWAN lora;

void setup()
{
  lora.begin();
  lora.setWorkMode(WISBLOCK_MODE_LORAWAN);
  if (!lora.setADR(false))
    Serial.println("ADR off pending, will apply after channels are known");
  lora.setDataRate(3);
}
```

</details>

### setDataRate()

Sets the fixed data rate used while ADR is off.

```cpp
lora.setDataRate(3);
```

| | |
| --- | --- |
| **Function** | `bool setDataRate(uint8_t dataRate)` |
| **Parameters** | **dataRate** - DR index. The valid range depends on the region, see the [RUI3 Appendix](https://docs.rakwireless.com/product-categories/software-apis-and-libraries/rui3/appendix/#data-rate-by-region). EU433: DR0 - DR5 for normal use. |
| **Returns** | bool |
| **Return Values** | **TRUE** active now<br/>**FALSE** stored, not active yet (same mechanism as `setADR()`) |

## 💡 NOTE

----
_The value has no effect while ADR is on. During a join some regions force a specific data rate._

----

### setTxPower()

Stores the TX power index in the configuration.

```cpp
lora.setTxPower(index);
```

| | |
| --- | --- |
| **Function** | `void setTxPower(uint8_t txPowerIndex)` |
| **Parameters** | **txPowerIndex** - power index, `0` is the highest power |

## ⚠️ WARNING

----
_**Currently there is no radio effect.** The public LoRa Basics Modem API has no call to set a fixed TX power. The value is stored and shown in `getConfig().lorawan.txPower`, but the power is controlled by the network (ADR) or follows the region's default maximum. Do not rely on this call to reduce the output power (Creation Log / `LoRaWANEngine::setTxPower()`)._

----

### setConfirmedUplinks()

Sets if `sendLoRaWAN()` sends confirmed or unconfirmed uplinks.

```cpp
lora.setConfirmedUplinks(true);
```

| | |
| --- | --- |
| **Function** | `void setConfirmedUplinks(bool confirmed)` |
| **Parameters** | **confirmed** - TRUE = confirmed uplinks, FALSE = unconfirmed (default) |

## 💡 NOTE

----
_Unlike RUI3's `cfm`, there is no per-call override and no retry count setting. The confirmation result is not reported separately._

----

### setChannelMask()

Pre-selects a sub-band for regions with many channels (US915, AU915, CN470, CN470_RP_1_0). Mirrors RUI3's `AT+MASK`: bit N enables sub-band N+1 (8 channels each). `0` = all channels.

```cpp
lora.setChannelMask(0x0001);
```

| | |
| --- | --- |
| **Function** | `bool setChannelMask(uint16_t mask)` |
| **Parameters** | **mask** - sub-band bit mask |
| **Returns** | bool |
| **Return Values** | **TRUE** always. It has no effect in other regions, use `getChannelMask()` to read what is active. |

## 💡 NOTE
----
_Set it **before** `join()`. It avoids wasting join attempts on sub-bands the gateway does not listen to. Unlike `setDeviceClass()` and `setADR()` it does not need a joined device. It is re-applied before every join attempt._

----

<details>
<summary><b>Click to view the code</b></summary>

```cpp
WisBlockLoRaWAN lora;

void setup()
{
  lora.begin();
  lora.setRegion(WISBLOCK_RUI3_BAND_US915);
  lora.setChannelMask(0x0001);    // sub-band 1, e.g. The Things Network US915
  lora.join();
}
```

</details>

`uint16_t getChannelMask() const` returns the mask that is active in the stack.

### Listen Before Talk (LBT)

RUI3 compatible `AT+LBT`, `AT+LBTRSSI` and `AT+LBTSCANTIME`. LBT is switched on automatically by LoRa Basics Modem in regions where it is mandatory. These calls only give control over it. All of them are safe to call before the join.

#### setLbtEnabled()

Switches LBT on or off.

```cpp
lora.setLbtEnabled(true);
```

| | |
| --- | --- |
| **Function** | `bool setLbtEnabled(bool enabled)` |
| **Parameters** | **enabled** - TRUE or FALSE |
| **Returns** | bool |
| **Return Values** | **TRUE** success<br/>**FALSE** the stack refused the request |

`bool getLbtEnabled() const` returns the state.

#### setLbtThreshold()

Sets the RSSI threshold in dBm (signed) above which the channel counts as busy.

```cpp
lora.setLbtThreshold(-80);
```

| | |
| --- | --- |
| **Function** | `bool setLbtThreshold(int16_t thresholdDbm)` |
| **Parameters** | **thresholdDbm** - e.g. -80 |
| **Returns** | bool |
| **Return Values** | **TRUE** success<br/>**FALSE** the stack refused the request |

## _⚠️ WARNING_
----
Selecting a region (KR920, AS923 for Japan) does **not** apply a region specific threshold. Every region uses LoRa Basics Modem's generic default of -80 dBm. If your certification needs another value, set it here (Creation Log, entry 2026-09-20).

----

`int16_t getLbtThreshold() const` returns the value.

#### setLbtScanTime()

Sets the listen duration in milliseconds before a channel is declared clear. Default is about 5 ms.

```cpp
lora.setLbtScanTime(5);
```

| | |
| --- | --- |
| **Function** | `bool setLbtScanTime(uint32_t scanTimeMs)` |
| **Parameters** | **scanTimeMs** - listen time in ms |
| **Returns** | bool |
| **Return Values** | **TRUE** success<br/>**FALSE** the stack refused the request |

`uint32_t getLbtScanTime() const` returns the value.

## Joining and Sending Data on LoRaWAN

### join()

Starts a join attempt cycle (OTAA), or activates the session (ABP).

```cpp
lora.join();
```

| | |
| --- | --- |
| **Function** | `void join()` |

## _💡 NOTE_
-----
_`join()` is **asynchronous** and returns nothing. The result is reported through `onJoinSuccess()` / `onJoinFailed()`, or poll `isJoined()` and `joinState()`._

_A failed OTAA join is retried automatically. With the defaults (interval 8 s, unlimited attempts) LoRa Basics Modem's own region-conform back-off is used. As soon as `setJoinReattemptInterval()` or `setMaxJoinAttempts()` are changed from the defaults, the library schedules the retries itself._

----

## _⚠️ WARNING_
-----
_`join()` always restarts the attempt counter. Never call it from `onJoinFailed()`._

----

<details>
<summary><b>Click to view the code</b></summary>

```cpp
WisBlockLoRaWAN lora;

void setup()
{
  lora.begin();
  lora.setWorkMode(WISBLOCK_MODE_LORAWAN);
  lora.setJoinReattemptInterval(30);   // 30 s between attempts
  lora.setMaxJoinAttempts(5);          // give up after 5
  lora.join();
}

void loop()
{
  lora.handleEvents();
}
```

</details>

### stopJoin()

Stops a running join and cancels the library's own retry timer (RUI3 `AT+JOIN=0:...`).

```cpp
lora.stopJoin();
```

| | |
| --- | --- |
| **Function** | `void stopJoin()` |

### isJoined()

Returns the join status.

```cpp
bool joined = lora.isJoined();
```

| | |
| --- | --- |
| **Function** | `bool isJoined() const` |
| **Returns** | bool |
| **Return Values** | **TRUE** the device is joined<br/>**FALSE** the device is not joined |

<details>
<summary><b>Click to view the code</b></summary>

```cpp
WisBlockLoRaWAN lora;

void loop()
{
  lora.handleEvents();
  if (lora.isJoined()) { /* send data */ }
}
```

</details>

### joinState()

Returns the detailed state of the join procedure.

```cpp
WisBlockJoinState s = lora.joinState();
```

| | |
| --- | --- |
| **Function** | `WisBlockJoinState joinState() const` |
| **Returns** | the state, see [WisBlockJoinState](#wisblockjoinstate) |

### setAutoJoin()

Enables joining automatically as soon as the LoRaWAN engine starts, so the device joins after power-up without an explicit `join()` (RUI3 `AT+JOIN=1:...`). Stored in the configuration.

```cpp
lora.setAutoJoin(true);
```

| | |
| --- | --- |
| **Function** | `void setAutoJoin(bool enabled)` |
| **Parameters** | **enabled** - TRUE = auto join, FALSE = manual (default) |

## _💡 NOTE_
----
_Pure configuration. It does not start the engine or a join itself. It is evaluated when the engine starts, so it is normally combined with `saveConfig()`. `bool getAutoJoin() const` returns the value._

----

<details>
<summary><b>Click to view the code</b></summary>

```cpp
WisBlockLoRaWAN lora;

void setup()
{
  lora.begin();
  if (!lora.getAutoJoin())
  {
    lora.setAutoJoin(true);
    lora.saveConfig();
  }
  lora.setWorkMode(WISBLOCK_MODE_LORAWAN);   // engine starts, joins on its own
}
```

</details>

### setJoinReattemptInterval()

Sets the time between join attempts.

```cpp
lora.setJoinReattemptInterval(30);
```

| | |
| --- | --- |
| **Function** | `void setJoinReattemptInterval(uint8_t seconds)` |
| **Parameters** | **seconds** - 7 to 255. Values outside are clamped, not rejected. Default 8. |

## _💡 NOTE_
----
_`uint8_t getJoinReattemptInterval() const` returns the (clamped) value. Takes effect with the next `join()` or retry._

----

### setMaxJoinAttempts()

Sets the maximum number of join attempts.

```cpp
lora.setMaxJoinAttempts(5);
```

| | |
| --- | --- |
| **Function** | `void setMaxJoinAttempts(uint8_t attempts)` |
| **Parameters** | **attempts** - 0 to 255. `0` = retry forever (default). |

## _💡 NOTE_
----
_When the limit is reached, retrying stops and `joinState()` returns `WISBLOCK_JOIN_GAVE_UP`. `onJoinFailed()` is still called for every single failed attempt. `uint8_t getMaxJoinAttempts() const` returns the value._

----

### sendLoRaWAN()

Queues an uplink on the given port.

```cpp
lora.sendLoRaWAN(port, data, length);
```

| | |
| --- | --- |
| **Function** | `bool sendLoRaWAN(uint8_t port, const uint8_t *data, uint8_t length)` |
| **Parameters** | **port** - application port 1 - 223 (port 0 is reserved for MAC commands and is refused)<br/>**data** - the payload. May be `nullptr` if `length` is 0 (empty uplink).<br/>**length** - payload length. The maximum depends on region and data rate. |
| **Returns** | bool |
| **Return Values** | **TRUE** the uplink was **queued** (or deferred, see below)<br/>**FALSE** not joined, invalid port, `NULL` data with `length` > 0, payload not accepted, or a deferred send is already waiting |

## _⚠️ WARNING_
----
_`TRUE` means *accepted*, **not** transmitted. Use `onLoRaWANTxFinished()` (transmission) and `onLoRaWANRxFinished()` (downlink)._

_LoRa Basics Modem holds **one** pending uplink. If a send is still in flight (also the library's own automatic fetch of pending downlinks), a new call is **deferred** by one level and transmitted automatically as soon as the previous one is finished. It still returns `TRUE`. A second call before the deferred one went out returns `FALSE`. A Class A cycle with RX1/RX2 and MAC commands can take several seconds, so do not schedule sends tighter than that (Creation Log, entries *First send after join lost* and 2026-09-16 *FPending auto-fetch fix #2/#3*)._

----

## _💡 NOTE_
----
_Confirmed or unconfirmed depends on `setConfirmedUplinks()`. An empty uplink (length 0, valid port) is a legal way to open a receive window and fetch a pending downlink._

----

<details>
<summary><b>Click to view the code</b></summary>

```cpp
WisBlockLoRaWAN lora;

void loop()
{
  lora.handleEvents();
  static uint32_t last = 0;
  if (lora.isJoined() && millis() - last > 30000)
  {
    last = millis();
    uint8_t payload[] = "example";
    if (!lora.sendLoRaWAN(2, payload, sizeof(payload)))
      Serial.println("Not queued");
  }
}
```

</details>

### setFetchPendingDownlinks()

Enables or disables the automatic uplink that fetches pending downlinks (Class A). When a downlink has the FPending bit set the library sends an empty uplink on the same port, so the network can deliver the next queued downlink without waiting for your next scheduled send. Default **enabled**.

```cpp
lora.setFetchPendingDownlinks(false);
```

| | |
| --- | --- |
| **Function** | `void setFetchPendingDownlinks(bool enabled)` |
| **Parameters** | **enabled** - TRUE (default) or FALSE |

## _💡 NOTE_
----
_Disable it if your duty cycle budget cannot afford the extra uplink. `WisBlockRxResult::fpending` still reports the bit. Class B and C do not need it. `bool getFetchPendingDownlinks() const` returns the value._

----

### requestLinkCheck()

Requests a LinkCheck from the network. The answer arrives with the next uplink and is reported through `onLinkCheckAnswer()`.

```cpp
lora.requestLinkCheck();
```

| | |
| --- | --- |
| **Function** | `void requestLinkCheck()` |

### getLinkCheckResult()

Reads the most recent LinkCheck answer directly, without a callback.

```cpp
WisBlockLinkCheckResult r;
bool ok = lora.getLinkCheckResult(r);
```

| | |
| --- | --- |
| **Function** | `bool getLinkCheckResult(WisBlockLinkCheckResult &out) const` |
| **Parameters** | **out** - receives the result |
| **Returns** | bool |
| **Return Values** | **TRUE** a result is available<br/>**FALSE** no LinkCheck was ever answered |

### setLinkCheckMode()

Sets when a LinkCheck is requested (RUI3 `AT+LINKCHECK`).

```cpp
lora.setLinkCheckMode(2);
```

| | |
| --- | --- |
| **Function** | `void setLinkCheckMode(uint8_t mode)` |
| **Parameters** | **mode** - `0` disabled<br/>`1` request on the next uplink only, then back to 0<br/>`2` request on every uplink |

## _💡 NOTE_
----
_Evaluated inside `sendLoRaWAN()`, so it works for the API and the AT layer alike. `uint8_t getLinkCheckMode() const` returns the mode._

----

### requestDeviceTime()

Requests the network time (DeviceTimeReq). The answer is reported through `onTimeRequestAnswer()`.

```cpp
lora.requestDeviceTime();
```

| | |
| --- | --- |
| **Function** | `void requestDeviceTime()` |

## _💡 NOTE_
----
_The request is sent with the next uplink._

----

## Class B

### setPingSlotPeriodicity()

Sets the Class B unicast ping slot periodicity (RUI3 `AT+PGSLOT`) and informs the network with a `PingSlotInfoReq`.

```cpp
lora.setPingSlotPeriodicity(3);
```

| | |
| --- | --- |
| **Function** | `bool setPingSlotPeriodicity(uint8_t periodicity)` |
| **Parameters** | **periodicity** - 0 to 7. `0` = about 1 s, `7` = 128 s. Larger values are clamped to 7. |
| **Returns** | bool |
| **Return Values** | **TRUE** accepted by the stack<br/>**FALSE** refused by the stack |

## _💡 NOTE_
----
_`uint8_t getPingSlotPeriodicity() const` returns the value._

----

### getBeaconFrequencyAndDr()

Reads the frequency and data rate of the next Class B beacon (RUI3 `AT+BFREQ`).

```cpp
uint32_t f; uint8_t dr;
bool ok = lora.getBeaconFrequencyAndDr(f, dr);
```

| | |
| --- | --- |
| **Function** | `bool getBeaconFrequencyAndDr(uint32_t &frequencyHz, uint8_t &dr) const` |
| **Parameters** | **frequencyHz** - frequency in Hz<br/>**dr** - data rate |
| **Returns** | bool |
| **Return Values** | **TRUE** a beacon was received before<br/>**FALSE** no beacon received yet, values are 0/0 |

### getBeaconTime()

Returns the GPS time (seconds since the GPS epoch) from the last received beacon (RUI3 `AT+BTIME`).

```cpp
uint32_t t = lora.getBeaconTime();
```

| | |
| --- | --- |
| **Function** | `uint32_t getBeaconTime() const` |
| **Returns** | GPS seconds, `0` if no beacon was received yet |

## _💡 NOTE_
----
_`AT+BGW` (gateway info from the beacon) is not implemented._

----

## LoRaWAN Multicast

Up to 4 groups (IDs 0 - 3). The device must already be in the requested class (`setDeviceClass()`), the call does not switch it. Groups are kept in RAM only.

### setMulticastGroup()

Configures a multicast group and starts its Class B or C session (RUI3 `AT+ADDMULC`).

```cpp
lora.setMulticastGroup(groupId, deviceClass, devAddr, nwkSKey, appSKey, frequencyHz, dataRate, periodicity);
```

| | |
| --- | --- |
| **Function** | `bool setMulticastGroup(uint8_t groupId, WisBlockDeviceClass deviceClass, uint32_t devAddr, const uint8_t nwkSKey[16], const uint8_t appSKey[16], uint32_t frequencyHz, uint8_t dataRate, uint8_t periodicity = 0)` |
| **Parameters** | **groupId** - 0 - 3<br/>**deviceClass** - `WISBLOCK_CLASS_B` or `WISBLOCK_CLASS_C`<br/>**devAddr** - multicast address<br/>**nwkSKey** / **appSKey** - 16 bytes each<br/>**frequencyHz** - downlink frequency, **must be valid for the active region**<br/>**dataRate** - downlink data rate<br/>**periodicity** - Class B ping slot periodicity 0 - 7 |
| **Returns** | bool |
| **Return Values** | **TRUE** group configured and session started<br/>**FALSE** invalid ID/class, device not in that class, or the stack rejected the configuration (for example a frequency outside the region) |

## _💡 NOTE_
----
_A log message like `INVALID FREQUENCY = 916800000` together with a failing group configuration means the frequency does not belong to the active band, e.g. a US915 frequency while EU433 is active. For EU433 use a frequency between 433.05 and 434.79 MHz, e.g. the RX2 frequency 434665000._

----

<details>
<summary><b>Click to view the code</b></summary>

```cpp
WisBlockLoRaWAN lora;

uint8_t nwk[16] = {0};   // provisioned by the network operator
uint8_t app[16] = {0};

void joined()
{
  lora.setDeviceClass(WISBLOCK_CLASS_C);
  lora.setMulticastGroup(0, WISBLOCK_CLASS_C, 0x01020304, nwk, app, 434665000, 0);
}
```

</details>

### removeMulticastGroup()

Stops the session and clears a group.

```cpp
lora.removeMulticastGroup(0);
```

| | |
| --- | --- |
| **Function** | `bool removeMulticastGroup(uint8_t groupId)` |
| **Parameters** | **groupId** - 0 - 3 |
| **Returns** | bool |
| **Return Values** | **TRUE** removed<br/>**FALSE** invalid or unused group ID |

### getMulticastGroup()

Returns the configuration of a group.

```cpp
const WisBlockMulticastGroup *g = lora.getMulticastGroup(0);
```

| | |
| --- | --- |
| **Function** | `const WisBlockMulticastGroup *getMulticastGroup(uint8_t groupId) const` |
| **Parameters** | **groupId** - 0 - 3 |
| **Returns** | pointer to the group, `nullptr` if invalid or not configured |

### findMulticastGroupByDevAddr()

Finds the group that uses a given address.

```cpp
int id = lora.findMulticastGroupByDevAddr(0x01020304);
```

| | |
| --- | --- |
| **Function** | `int findMulticastGroupByDevAddr(uint32_t devAddr) const` |
| **Parameters** | **devAddr** - multicast address |
| **Returns** | the group ID (0 - 3), or **-1** if no group uses this address |

## LoRa P2P

Set the mode to `WISBLOCK_MODE_LORA_P2P` first. The P2P engine drives the SX1262 directly. P2P settings are applied to the radio immediately.

### setP2PFrequency()

Sets the P2P frequency.

```cpp
lora.setP2PFrequency(868000000);
```

| | |
| --- | --- |
| **Function** | `void setP2PFrequency(uint32_t frequencyHz)` |
| **Parameters** | **frequencyHz** - frequency in Hz |

### setP2PSpreadingFactor()

Sets the P2P spreading factor.

```cpp
lora.setP2PSpreadingFactor(7);
```

| | |
| --- | --- |
| **Function** | `void setP2PSpreadingFactor(uint8_t sf)` |
| **Parameters** | **sf** - 7 to 12 |

### setP2PBandwidth()

Sets the P2P bandwidth.

```cpp
lora.setP2PBandwidth(WISBLOCK_BW_125);
```

| | |
| --- | --- |
| **Function** | `void setP2PBandwidth(WisBlockP2PBandwidth bw)` |
| **Parameters** | **bw** - see [WisBlockP2PBandwidth](#wisblockp2pbandwidth) |

### setP2PCodingRate()

Sets the P2P coding rate.

```cpp
lora.setP2PCodingRate(WISBLOCK_CR_4_5);
```

| | |
| --- | --- |
| **Function** | `void setP2PCodingRate(WisBlockP2PCodingRate cr)` |
| **Parameters** | **cr** - see [WisBlockP2PCodingRate](#wisblockp2pcodingrate) |

### setP2PPreambleLength()

Sets the P2P preamble length.

```cpp
lora.setP2PPreambleLength(8);
```

| | |
| --- | --- |
| **Function** | `void setP2PPreambleLength(uint16_t symbols)` |
| **Parameters** | **symbols** - preamble length in symbols |

### setP2PTxPower()

Sets the P2P TX power.

```cpp
lora.setP2PTxPower(14);
```

| | |
| --- | --- |
| **Function** | `void setP2PTxPower(int8_t dbm)` |
| **Parameters** | **dbm** - TX power in dBm |

### setP2PCad()

Enables a Channel Activity Detection before every `sendP2P()`.

```cpp
lora.setP2PCad(true);
```

| | |
| --- | --- |
| **Function** | `void setP2PCad(bool enabled)` |
| **Parameters** | **enabled** - TRUE or FALSE |

### setP2PRxBoostedGain()

Selects the SX1262 boosted RX gain. Boosted gain gives a few dB more sensitivity for about 4 - 5 mA more RX current. Default is on.

```cpp
lora.setP2PRxBoostedGain(false);
```

| | |
| --- | --- |
| **Function** | `void setP2PRxBoostedGain(bool enabled)` |
| **Parameters** | **enabled** - TRUE or FALSE |

### setP2PIqInversion()

Inverts the IQ signals for transmit and receive. Both sides of a P2P link must use the same setting. Default is off. Applied to the radio immediately.

```cpp
lora.setP2PIqInversion(true);
```

| | |
| --- | --- |
| **Function** | `void setP2PIqInversion(bool enabled)` |
| **Parameters** | **enabled** - TRUE or FALSE |

### setP2PSyncWord()

Sets the 16 bit LoRa sync word. `0x1424` is the private sync word (default), `0x3444` the public one used by LoRaWAN. Both sides of a P2P link must use the same value. Applied to the radio immediately.

```cpp
lora.setP2PSyncWord(0x3444);
```

| | |
| --- | --- |
| **Function** | `void setP2PSyncWord(uint16_t syncWord)` |
| **Parameters** | **syncWord** - 16 bit sync word, for example `0x1424` or `0x3444` |

### getP2PSettings()

Returns the P2P settings that are applied to the radio.

```cpp
const WisBlockP2PSettings &s = lora.getP2PSettings();
```

| | |
| --- | --- |
| **Function** | `const WisBlockP2PSettings &getP2PSettings() const` |
| **Returns** | a reference to [WisBlockP2PSettings](#wisblockp2psettings) |

### sendP2P()

Sends a LoRa P2P packet.

```cpp
lora.sendP2P(data, length);
```

| | |
| --- | --- |
| **Function** | `bool sendP2P(const uint8_t *data, uint8_t length)` |
| **Parameters** | **data** - payload<br/>**length** - payload length |
| **Returns** | bool |
| **Return Values** | **TRUE** the transmission was started<br/>**FALSE** the transmission could not be started |

## _💡 NOTE_
----
_The end of the transmission is reported through `onP2PTxFinished()`. If CAD is enabled the packet is only sent on a clear channel._

----

<details>
<summary><b>Click to view the code</b></summary>

```cpp
WisBlockLoRaWAN lora;

void txDone(const WisBlockTxResult &r) { Serial.println(r.success ? "TX ok" : "TX fail"); }

void setup()
{
  lora.begin();
  lora.setWorkMode(WISBLOCK_MODE_LORA_P2P);
  lora.setP2PFrequency(868000000);
  lora.onP2PTxFinished(txDone);
  uint8_t msg[] = "Hello";
  lora.sendP2P(msg, sizeof(msg));
}

void loop()
{
  lora.handleEvents();
}
```

</details>

### startP2PReceive()

Starts receiving. Packets are reported through `onP2PRxFinished()`.

```cpp
lora.startP2PReceive(timeoutMs);
```

| | |
| --- | --- |
| **Function** | `void startP2PReceive(uint32_t timeoutMs = 0)` |
| **Parameters** | **timeoutMs** - receive timeout in ms, `0` = continuous |

<details>
<summary><b>Click to view the code</b></summary>

```cpp
WisBlockLoRaWAN lora;

void rxDone(const WisBlockRxResult &r) { Serial.printf("Got %d bytes, RSSI %d\n", r.length, r.rssi); }

void setup()
{
  lora.begin();
  lora.setWorkMode(WISBLOCK_MODE_LORA_P2P);
  lora.onP2PRxFinished(rxDone);
  lora.startP2PReceive();      // continuous
}

void loop()
{
  lora.handleEvents();
}
```

</details>

### stopP2PReceive()

Stops receiving (also cancels an RX duty cycle).

```cpp
lora.stopP2PReceive();
```

| | |
| --- | --- |
| **Function** | `void stopP2PReceive()` |

### startP2PReceiveDutyCycle()

Starts the SX1262 hardware RX duty cycle. The chip alternates RX and sleep phases on its own, which needs less average current than waking the MCU repeatedly. If a preamble is detected the chip receives the whole packet.

```cpp
lora.startP2PReceiveDutyCycle(rxTimeMs, sleepTimeMs);
```

| | |
| --- | --- |
| **Function** | `void startP2PReceiveDutyCycle(uint32_t rxTimeMs, uint32_t sleepTimeMs)` |
| **Parameters** | **rxTimeMs** - length of each RX phase in ms<br/>**sleepTimeMs** - length of each sleep phase in ms |

## _⚠️ WARNING_
----
_`rxTimeMs + sleepTimeMs` must stay below the **transmitter's** preamble duration, otherwise a packet can pass unnoticed while the radio sleeps. Use `computeP2PRxDutyCycleTiming()` for a starting value and verify it on your link. Call `stopP2PReceive()` to cancel._

----

### computeP2PRxDutyCycleTiming()

Calculates usable `rxTimeMs` / `sleepTimeMs` for `startP2PReceiveDutyCycle()`.

```cpp
lora.computeP2PRxDutyCycleTiming(txPreambleSymbols, rxMs, sleepMs);
lora.computeP2PRxDutyCycleTiming(rxMs, sleepMs);
```

| | |
| --- | --- |
| **Function** | `bool computeP2PRxDutyCycleTiming(uint16_t txPreambleLengthSymbols, uint32_t &rxTimeMs, uint32_t &sleepTimeMs, uint8_t marginSymbols = 5) const<br/>bool computeP2PRxDutyCycleTiming(uint32_t &rxTimeMs, uint32_t &sleepTimeMs, uint8_t marginSymbols = 5) const` |
| **Parameters** | **txPreambleLengthSymbols** - the **transmitter's** preamble length (preferred overload)<br/>**rxTimeMs** - out: RX phase<br/>**sleepTimeMs** - out: sleep phase<br/>**marginSymbols** - safety margin, increase it if packets are missed |
| **Returns** | bool |
| **Return Values** | **TRUE** values computed<br/>**FALSE** the preamble is too short for any usable window (outputs untouched). Use a longer preamble or plain `startP2PReceive()`. |

## _💡 NOTE_
----
_The overload without the preamble uses this device's own preamble setting, which usually differs from the transmitter's. Real hardware tests needed more margin than the datasheet formula suggests, treat the result as a starting point._

----

<details>
<summary><b>Click to view the code</b></summary>

```cpp
WisBlockLoRaWAN lora;

void setup()
{
  lora.begin();
  lora.setWorkMode(WISBLOCK_MODE_LORA_P2P);
  uint32_t rx, sleep;
  if (lora.computeP2PRxDutyCycleTiming(32, rx, sleep))   // transmitter uses 32 symbols
    lora.startP2PReceiveDutyCycle(rx, sleep);
}
```

</details>

### startP2PCad()

Starts a one-shot Channel Activity Detection. The result is reported through `onP2PCadResult()`.

```cpp
lora.startP2PCad();
```

| | |
| --- | --- |
| **Function** | `void startP2PCad()` |

<details>
<summary><b>Click to view the code</b></summary>

```cpp
WisBlockLoRaWAN lora;

void cad(WisBlockCADResult r)
{
  Serial.println(r == WISBLOCK_CAD_CHANNEL_DETECTED ? "Busy" : "Clear");
}

void setup()
{
  lora.begin();
  lora.setWorkMode(WISBLOCK_MODE_LORA_P2P);
  lora.onP2PCadResult(cad);
  lora.startP2PCad();
}
```

</details>

### sleepRadio()

Puts the SX1262 in low power sleep. Only meaningful in P2P mode (LoRaWAN mode already sleeps the radio between tasks, the call is a no-op there). The radio is re-configured automatically on the next send, receive or CAD.

```cpp
lora.sleepRadio();
```

| | |
| --- | --- |
| **Function** | `void sleepRadio()` |

### onP2PTxFinished()

Registers the P2P TX callback (`void (*)(const WisBlockTxResult &)`).

```cpp
lora.onP2PTxFinished(callback);
```

| | |
| --- | --- |
| **Function** | `void onP2PTxFinished(LoRaP2PEngine::TxFinishedCb cb)` |
| **Parameters** | **cb** - the callback function |

### onP2PRxFinished()

Registers the P2P RX callback (`void (*)(const WisBlockRxResult &)`).

```cpp
lora.onP2PRxFinished(callback);
```

| | |
| --- | --- |
| **Function** | `void onP2PRxFinished(LoRaP2PEngine::RxFinishedCb cb)` |
| **Parameters** | **cb** - the callback function |

### onP2PCadResult()

Registers the P2P CAD callback (`void (*)(WisBlockCADResult)`).

```cpp
lora.onP2PCadResult(callback);
```

| | |
| --- | --- |
| **Function** | `void onP2PCadResult(LoRaP2PEngine::CadResultCb cb)` |
| **Parameters** | **cb** - the callback function |

## Configuration Storage

The configuration is stored in the flash of the MCU with a magic number, a version and a CRC. A missing or invalid configuration (first boot, changed library version) is replaced by the built-in default settings, [hasValidConfig()](#hasvalidconfig) tells which case applies. There are two slots: the regular **user** slot and a separate **factory** slot.

### saveConfig()

Writes the current settings to the user slot.

```cpp
bool ok = lora.saveConfig();
```

| | |
| --- | --- |
| **Function** | `bool saveConfig()` |
| **Returns** | bool |
| **Return Values** | **TRUE** saved<br/>**FALSE** flash write failed |

## _💡 NOTE_
----
_Setters change the live configuration only. Call `saveConfig()` to keep it over a reboot. Multicast groups are never saved._

----

### restoreConfig()

Reloads the user slot and discards unsaved changes.

```cpp
bool ok = lora.restoreConfig();
```

| | |
| --- | --- |
| **Function** | `bool restoreConfig()` |
| **Returns** | bool |
| **Return Values** | **TRUE** loaded<br/>**FALSE** nothing was ever saved, defaults were loaded |

### saveFactoryDefaults()

Copies the **current** live configuration into the factory slot (intended one-time production step, e.g. after setting a unique DevEUI).

```cpp
bool ok = lora.saveFactoryDefaults();
```

| | |
| --- | --- |
| **Function** | `bool saveFactoryDefaults()` |
| **Returns** | bool |
| **Return Values** | **TRUE** saved<br/>**FALSE** flash write failed |

### restoreFactoryDefaults()

Loads the factory slot, makes it the live configuration and writes it to the user slot too.

```cpp
bool ok = lora.restoreFactoryDefaults();
```

| | |
| --- | --- |
| **Function** | `bool restoreFactoryDefaults()` |
| **Returns** | bool |
| **Return Values** | **TRUE** loaded<br/>**FALSE** no factory slot saved, configuration untouched |

### hasValidConfig()

Tells whether the library runs on a **valid saved configuration** or on its built-in defaults. Call it after `begin()` to let the application decide whether it has to set up the configuration or can use the stored one, for example on a new device or one that was erased.

```cpp
lora.begin();
if (!lora.hasValidConfig())
{
  // First start of a new or erased device: set everything up once and keep it
  lora.setWorkMode(WISBLOCK_MODE_LORA_P2P);
  lora.setP2PFrequency(868300000);
  lora.setP2PSpreadingFactor(7);
  // ...
  lora.saveConfig();
}
// Callbacks, join() or startP2PReceive() are never stored, they are needed after every boot
lora.onP2PRxFinished(onRxDone);
lora.startP2PReceive(0);
```

| | |
| --- | --- |
| **Function** | `bool hasValidConfig() const` |
| **Returns** | bool |
| **Return Values** | **TRUE** a valid saved configuration was loaded by `begin()` (or has been saved since)<br/>**FALSE** the library runs on the built-in defaults |

## _💡 NOTE_
----
- _FALSE after `begin()` means: nothing was ever saved (new device or erased flash), the stored data is damaged, or it was written by an incompatible library version. A normal firmware upload typically keeps the stored configuration, so FALSE is not expected after a plain re-flash._
- _The value follows the user slot: a successful `saveConfig()` or `restoreFactoryDefaults()` makes it TRUE, `restoreConfig()` sets it to what it returns. Changing a setting does not change it._
- _It only says that a valid configuration exists. It does not say that the content is complete, for example that the OTAA keys were set. If the application needs that, check `lora.getConfig()` as well._

----

### getConfig()

Returns the complete live configuration (read-only).

```cpp
const WisBlockPersistedConfig &cfg = lora.getConfig();
```

| | |
| --- | --- |
| **Function** | `const WisBlockPersistedConfig &getConfig() const` |
| **Returns** | a reference to [WisBlockPersistedConfig](#wisblockpersistedconfig) |

<details>
<summary><b>Click to view the code</b></summary>

```cpp
WisBlockLoRaWAN lora;

void setup()
{
  lora.begin();
  const WisBlockPersistedConfig &cfg = lora.getConfig();
  Serial.printf("ADR requested: %d, DR requested: %d\n", cfg.lorawan.adrEnabled, cfg.lorawan.dataRate);
  Serial.printf("Class requested: %d\n", cfg.lorawan.deviceClass);
}
```

</details>

## AT Command Interface

`WisBlockLoRaAT` is a thin text protocol on top of the same `WisBlockLoRaWAN` object, so AT commands and API calls always stay in sync. The command set follows RUI3 (`AT+BAND`, `AT+NJM`, `AT+JOIN`, `AT+SEND`, ...), see [WisBlockLoRaWAN-AT-Commands](WisBlockLoRaWAN-AT-Commands.md) for the complete command reference. Custom commands use the prefix `ATC+`.

## _ℹ️ INFO_
----    
_`AT+BAND` uses the same [WisBlockRUI3Band](#wisblockrui3band) numbering as `setRegion()` / `getRegion()`. `AT+BAND=0` selects EU433._

----

### begin() (AT)

Connects the AT parser to a `WisBlockLoRaWAN` object and a serial port.

```cpp
at.begin(lora, Serial);
```

| | |
| --- | --- |
| **Function** | `void begin(WisBlockLoRaWAN &lora, Stream &port)` |
| **Parameters** | **lora** - the object, `lora.begin()` must have been called before<br/>**port** - the `Stream` (USB `Serial` or a UART) used for AT input and output |

<details>
<summary><b>Click to view the code</b></summary>

```cpp
WisBlockLoRaWAN lora;
WisBlockLoRaAT at;

void setup()
{
  Serial.begin(115200);
  lora.begin();
  at.begin(lora, Serial);
}

void loop()
{
  lora.handleEvents();
  at.handleSerial();
}
```

</details>

### handleSerial()

Reads the port, collects lines ended by CR or LF and executes them. Call it in every `loop()`, or after the wake callback of [setRxWakeCallback()](#setrxwakecallback) was called. It does nothing while [enableBackgroundRx()](#enablebackgroundrx) runs the commands in its own task, except delivering the lines for [onUnhandledDataInLoop()](#onunhandleddatainloop).

```cpp
at.handleSerial();
```

| | |
| --- | --- |
| **Function** | `void handleSerial()` |

### processLine()

Executes one complete command line (without CR/LF) and writes the reply to the port. The line is converted to upper case before it is compared.

```cpp
at.processLine("AT+BAND=?");
```

| | |
| --- | --- |
| **Function** | `void processLine(const char *line)` |
| **Parameters** | **line** - the command line |

<details>
<summary><b>Click to view the code</b></summary>

```cpp
WisBlockLoRaWAN lora;
WisBlockLoRaAT at;

void setup()
{
  lora.begin();
  at.begin(lora, Serial);
  at.processLine("AT+BAND=0");   // select EU433
  at.processLine("AT+BAND=?");   // prints AT+BAND=0
}
```

</details>

### onUnhandledData()

Registers a callback for lines that do not start with `AT`, so your application can run its own serial protocol on the same port. A line that starts with `AT` but is unknown still gets the `ERROR: unknown command` reply.

```cpp
at.onUnhandledData(callback);
```

| | |
| --- | --- |
| **Function** | `void onUnhandledData(UnhandledDataCb cb)` |
| **Parameters** | **cb** - `void (*)(const char *line)` |

The callback runs in the context that reads the port: the task of [enableBackgroundRx()](#enablebackgroundrx), or `loop()` if you call `handleSerial()`. Use [onUnhandledDataInLoop()](#onunhandleddatainloop) if the lines must be handled in `loop()`.

### onUnhandledDataInLoop()

Same lines as [onUnhandledData()](#onunhandleddata), but the callback is called from `handleSerial()`, that means from `loop()`. The lines wait in a queue of `WB_AT_UNHANDLED_QUEUE_LINES` (4) lines of up to 255 characters, 256 bytes of RAM each (define another value before including the library). If the queue is full, new lines are dropped. The text is only valid during the call.

```cpp
void onLine(const char *line) { Serial.printf("Got: %s\n", line); }

at.onUnhandledDataInLoop(onLine);   // setup()
at.handleSerial();                  // loop(), also in task mode
```

| | |
| --- | --- |
| **Function** | `void onUnhandledDataInLoop(UnhandledDataCb cb)` |
| **Parameters** | **cb** - `void (*)(const char *line)` |

Both callbacks can be registered at the same time. If `loop()` sleeps, use the `onUnhandledData()` callback to wake it up (set a flag or give a semaphore) and let `loop()` call `handleSerial()`.

### addCustomATCommand()

Registers a custom `ATC+<CMD>` command, comparable to RUI3's `api.system.atMode.add()`. Up to `MAX_CUSTOM_AT_COMMANDS` (16) commands.

```cpp
at.addCustomATCommand("LED", "Switch the LED", handler);
```

| | |
| --- | --- |
| **Function** | `bool addCustomATCommand(const char *cmd, const char *usage, CustomAtHandler handler)` |
| **Parameters** | **cmd** - the name only, without `AT`, `ATC+` or `=` (case insensitive)<br/>**usage** - short description, currently unused but kept for a future help listing<br/>**handler** - `WisBlockAtStatus (*)(Stream &port, const char *cmd, char *args)` |
| **Returns** | bool |
| **Return Values** | **TRUE** registered<br/>**FALSE** null or empty argument, table full, or command already registered |

## _💡 INFO_
----
_The handler gets the full upper case command name in `cmd` (one handler can serve several commands) and `args` as follows:_

- _`nullptr` for a bare `ATC+LED`_
- _the string `"?"` for a query `ATC+LED=?`_
- _the text after `=` for a set `ATC+LED=1:2`. It is passed unsplit, parse it yourself._

_Return `WISBLOCK_AT_OK` and the library replies `OK`, otherwise `AT_ERROR` or `AT_PARAM_ERROR`._

----

<details>
<summary><b>Click to view the code</b></summary>

```cpp
WisBlockLoRaWAN lora;
WisBlockLoRaAT at;

WisBlockAtStatus ledCmd(Stream &port, const char *cmd, char *args)
{
  if (args == nullptr) return WISBLOCK_AT_ERROR;      // ATC+LED needs a value
  if (args[0] == '?') { port.println(digitalRead(LED_BUILTIN)); return WISBLOCK_AT_OK; }
  if (args[0] != '0' && args[0] != '1') return WISBLOCK_AT_PARAM_ERROR;
  digitalWrite(LED_BUILTIN, args[0] == '1');
  return WISBLOCK_AT_OK;
}

void setup()
{
  lora.begin();
  at.begin(lora, Serial);
  at.addCustomATCommand("LED", "ATC+LED=0|1", ledCmd);
}
```

</details>

### setRxWakeCallback()

Optional. Registers a function that is called whenever USB data arrives, for the **loop mode** of [enableBackgroundRx()](#enablebackgroundrx). Call it before `enableBackgroundRx()`. The function runs in the context of the USB driver, so it must be short: set a flag or give a semaphore and let your own task call `handleSerial()`. Typical for a `loop()` that sleeps on a semaphore, as in the `LowPower*` examples.

```cpp
void atRxWake(void) { /* set a flag, give the semaphore that wakes loop() */ }

at.setRxWakeCallback(atRxWake);
```

| | |
| --- | --- |
| **Function** | `void setRxWakeCallback(RxWakeCallback cb)` |
| **Parameters** | **cb** - `void (*)(void)` |

### enableBackgroundRx()

Runs the AT commands in the background, so `loop()` does not have to call `handleSerial()`. It hooks the USB receive notification of `Serial` (`tud_cdc_rx_cb()` of TinyUSB on RAK4631, the receive handler of `Serial` on RAK3312: `Serial.onReceive()` for a UART, the USB CDC RX event for the native USB). RAK4631 and RAK3312 work the same way, in one of two modes:

- **Task mode** (no wake callback registered): the library starts a task `WB_AT`. The receive callback only gives a semaphore, the task wakes up and handles the incoming data like `handleSerial()` does. The commands run in that task, with its own stack, not in the USB driver and not in `loop()`. `lora.enableBackgroundTask()` must be running, because the commands call into the LoRa Basics Modem from a second task, and `lockLbm()` / `unlockLbm()` (used around every command) only protect it if the background task is running.
- **Loop mode** (wake callback registered with [setRxWakeCallback()](#setrxwakecallback)): the receive callback only calls your wake callback, and you call `handleSerial()` from your own task.

```cpp
lora.enableBackgroundTask();           // needed for task mode
bool ok = at.enableBackgroundRx();     // task mode
```

| | |
| --- | --- |
| **Function** | `bool enableBackgroundRx()` |
| **Returns** | bool |
| **Return Values** | **TRUE** active<br/>**FALSE** not supported (RAK11310), `port` is not `Serial`, another object already uses it, or in task mode the LoRa background task is not running or the task could not be created |

The task can be tuned with defines before the library is built (for example in `platformio.ini`):

| Define | Default | Meaning |
| --- | --- | --- |
| `WB_AT_TASK_STACK_BYTES` | 8192 | Stack of the task in bytes. Commands like `AT+STATUS` print a lot. |
| `WB_AT_TASK_PRIORITY` | 1 | Priority, the same as `loop()` and the LoRa Basics Modem task. |
| `WB_AT_TASK_CORE` | core of `loop()` | ESP32 only. `tskNO_AFFINITY` lets FreeRTOS pick the core. |

## _⚠️ WARNING_
----
- _The `port` given to `begin()` must be the `Serial` that receives the AT commands._
- _Only **one** `WisBlockLoRaAT` instance can use it._
- _It uses `tud_cdc_rx_cb()` (RAK4631) or the receive handler of `Serial` (RAK3312). Your sketch **must not define or set these itself**._
- _The task mode on the ESP32 is new and so far only tested with a simulation on a PC. A task of the library read the port in an earlier version and replies were lost and devices hung under load, the cause was never found. If you see this, use the loop mode and tell us._
- _The task answers the commands while `loop()` and the LoRa task print their own messages. The messages can be mixed on the serial port._

----

## Helper Functions

### wisblockRUI3BandToRegion()

Converts a RUI3 band number to the internal `WisBlockRegion`.

```cpp
WisBlockRegion r;
bool ok = wisblockRUI3BandToRegion(WISBLOCK_RUI3_BAND_EU433, r);
```

| | |
| --- | --- |
| **Function** | `bool wisblockRUI3BandToRegion(WisBlockRUI3Band band, WisBlockRegion &outRegion)` |
| **Parameters** | **band** - RUI3 band<br/>**outRegion** - receives the region |
| **Returns** | bool |
| **Return Values** | **TRUE** converted<br/>**FALSE** no equivalent (`LA915`, `UNKNOWN`, out of range), `outRegion` untouched |

### wisblockRegionToRUI3Band()

Converts the internal `WisBlockRegion` to the RUI3 band number.

```cpp
WisBlockRUI3Band b = wisblockRegionToRUI3Band(WISBLOCK_REGION_EU433);
```

| | |
| --- | --- |
| **Function** | `WisBlockRUI3Band wisblockRegionToRUI3Band(WisBlockRegion region)` |
| **Parameters** | **region** - internal region |
| **Returns** | the RUI3 band, `WISBLOCK_RUI3_BAND_UNKNOWN` if there is none |

## Library Version

| | |
| --- | --- |
| **Macro** | Meaning |
| **`WISBLOCK_LORAWAN_VERSION_MAJOR / _MINOR / _PATCH`** | The numeric parts of the library version |
| **`WISBLOCK_LORAWAN_VERSION_STRING`** | The version as string literal, e.g. `"1.0.0"`. Can be concatenated with other literals. |
| **`DEF_FW_VER`** | Default firmware label including the board, e.g. `WB_BM_RAK4631_1.0.0` |

Include `WisBlockLoRaWAN_all.h` to get the LoRaWAN API and the AT interface together.

## Known Limitations and Notes

Collected from the library's Creation Log (`Creation-Log-From-Claude-AI.md`) for quick reference:

- **Integration scaffold, not a certified stack.** Run the LoRa Alliance / Semtech certification tests before shipping Class B/C products.
- **Class B** is not yet tested on real networks. `AT+BGW` is not implemented.
- **`setTxPower()`** has no radio effect, see above.
- **`setADR()`, `setDataRate()`, `setDeviceClass()`** can return `FALSE` before the join although the request is stored and applied automatically later.
- **`sendLoRaWAN()`** returning `TRUE` only means *queued*. One pending uplink plus one deferred uplink is the maximum.
- **`join()`** must not be called from `onJoinFailed()`.
- **Region** can only be changed before joining. LA915 is not supported. EU433 is provided by the library itself (RP002-1.0.4).
- **OTAA/ABP keys** (`appKey`, session keys) cannot be read back from the stack. Multicast keys can.
- **Multicast groups** live in RAM only and are lost at reboot. Remote Multicast Setup (over-the-air provisioning) and FUOTA are not enabled.
- **Not vendored:** FUOTA, clock sync, cloud services, store-and-forward, geolocation, relay (removed on 2026-09-14) and the 2.4 GHz region.
- **Background task** (`enableBackgroundTask()`) and **background AT RX** (`enableBackgroundRx()`, task mode or loop mode with a wake callback) are the recommended low power setup on RAK4631 and RAK3312. RAK11310 needs a FreeRTOS port and has no background AT RX, `handleSerial()` in `loop()` is the only option there.
- **P2P sleep** uses cold-start sleep. The idle current improvement is reasoned, not yet measured on hardware.
- **P2P RX duty cycle** timing usually needs more margin than the datasheet formula, verify on your own link.
