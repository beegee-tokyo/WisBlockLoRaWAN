# WisBlockLoRaWAN AT Command Manual

## Overview

The WisBlockLoRaWAN library contains an AT command interface (class `WisBlockLoRaAT`) that follows the RUI3 AT command set, so existing tools and host software written for RUI3 devices can in most cases talk to a WisBlock module running this library. The default baud rate is not fixed by the library, it uses the `Serial` port that the application hands over in `at.begin(lora, Serial)`, for the USB port the baud rate setting has no effect.

This document follows the layout of the [RUI3 AT Command Manual](https://docs.rakwireless.com/product-categories/software-apis-and-libraries/rui3/at-command-manual/). The C++ API is described in [WisBlockLoRaWAN-API](WisBlockLoRaWAN-API.md).

## _⚠️ WARNING_
----
_**Not every RUI3 command exists here.** Only the commands listed in this manual are implemented. Everything else, for example `AT+JN1DL`, `AT+RX2DR`, `AT+PNM`, `AT+DCS`, `AT+CHE`, `AT+CHS`, `AT+BAT` or the `AT+P*` single parameter P2P commands, answers with `AT_ERROR`. See [RUI3 commands that are not implemented](#rui3-commands-that-are-not-implemented)._

_Some commands behave slightly different from RUI3. Every difference is listed in the note below the command._

----

### Supported boards

- RAK4631 (nRF52840)
- RAK3312 (ESP32-S3)
- RAK11310 (RP2040), reduced support, no background AT reception

### AT Command Format

The AT commands have the standard format `AT+XXX`, with `XXX` denoting the command. There are three command behaviors:

- `AT+XXX` is used to run a command, such as `AT+JOIN`.
- `AT+XXX=?` is used to get the value of a given command, for example, `AT+BAND=?`.
- `AT+XXX=<value>` is used to provide a value to a command, for example, `AT+CFM=1`.

## _⚠️ WARNING_
----
_The RUI3 help form `AT+XXX?` is **not implemented**. It returns `AT_ERROR`. `AT?` and `ATE` do not exist either._

----

The output format is as below:

```text
AT+XXX=<value><CR><LF>
<Status><CR><LF>
```

## _💡 NOTE_
----
_`<CR>` stands for “carriage return” and `<LF>` stands for “line feed”._
- _Commands are terminated by `<CR>` or `<LF>`. Empty lines are ignored._
- _Commands are **not case sensitive**, `at+band=?` works like `AT+BAND=?`._
- _A command line can have at most 255 characters._
- _The `AT+XXX=<value><CR><LF>` line is only printed when a value is queried with `AT+XXX=?`. A few commands print the bare value without the `AT+XXX=` prefix, this is noted at the command._
- _Every command returns a status string. `ATZ` and `AT+FACTORY` reset the MCU, so the status may not be seen._

----

The possible status codes are:

- `OK`: command runs correctly without error.
- `AT_ERROR`: generic error, or unknown command. Often preceded by a line of plain text that tells the reason, for example `unsupported band index - LA915 not built into this LBM vendoring`.
- `AT_PARAM_ERROR`: a parameter of the command is wrong.
- `AT_NO_NETWORK_JOINED`: the uplink of `AT+SEND` was not accepted. Most often the device has not joined yet, but the same code is also returned for an invalid port or a rejected payload.

The RUI3 status codes `AT_BUSY_ERROR`, `AT_TEST_PARAM_OVERFLOW`, `AT_NO_CLASSB_ENABLE` and `AT_RX_ERROR` are **never** returned by this library.

## _ℹ️ INFO_
----
_Commands starting with `ATC+` are application defined custom commands, see [Custom AT Commands](#custom-at-commands)._

----

## Content

- [AT Command Manual](#wisblocklorawan-at-command-manual)
  * [Overview](#overview)
    + [Supported boards](#supported-boards)
    + [AT Command Format](#at-command-format)
  * [Content](#content)
  * [General Commands](#general-commands)
    + [AT](#at)
    + [ATZ](#atz)
    + [ATR](#atr)
    + [AT+SN](#atsn)
    + [AT+VER](#atver)
    + [AT+HWMODEL](#athwmodel)
    + [AT+HWID](#athwid)
    + [AT+ALIAS](#atalias)
    + [AT+FIRMWAREVER](#atfirmwarever)
    + [AT+STATUS](#atstatus)
    + [AT+BOOT](#atboot)
  * [Low Power](#low-power)
    + [AT+LOWPOWER](#atlowpower)
  * [Configuration Storage](#configuration-storage)
    + [AT+SAVE](#atsave)
    + [AT+RESTORE](#atrestore)
    + [AT+FACTORY](#atfactory)
  * [LoRaWAN Keys and IDs](#lorawan-keys-and-ids)
    + [AT+DEVEUI](#atdeveui)
    + [AT+APPEUI](#atappeui)
    + [AT+APPKEY](#atappkey)
    + [AT+DEVADDR](#atdevaddr)
    + [AT+APPSKEY](#atappskey)
    + [AT+NWKSKEY](#atnwkskey)
  * [LoRaWAN Joining and Sending](#lorawan-joining-and-sending)
    + [AT+CFM](#atcfm)
    + [AT+JOIN](#atjoin)
    + [AT+NJM](#atnjm)
    + [AT+NJS](#atnjs)
    + [AT+SEND](#atsend)
    + [AT+FPENDING](#atfpending)
  * [LoRaWAN Network Management](#lorawan-network-management)
    + [AT+ADR](#atadr)
    + [AT+CLASS](#atclass)
    + [AT+DR](#atdr)
    + [AT+TXP](#attxp)
    + [AT+LINKCHECK](#atlinkcheck)
    + [AT+TIMEREQ](#attimereq)
    + [Listen Before Talk](#listen-before-talk)
    + [AT+LBT](#atlbt)
    + [AT+LBTRSSI](#atlbtrssi)
    + [AT+LBTSCANTIME](#atlbtscantime)
  * [Class B Mode](#class-b-mode)
    + [AT+PGSLOT](#atpgslot)
    + [AT+BFREQ](#atbfreq)
    + [AT+BTIME](#atbtime)
  * [LoRaWAN Regional Commands](#lorawan-regional-commands)
    + [AT+MASK](#atmask)
    + [AT+BAND](#atband)
  * [LoRaWAN Multicast Group](#lorawan-multicast-group)
    + [AT+ADDMULC](#ataddmulc)
    + [AT+RMVMULC](#atrmvmulc)
    + [AT+LSTMULC](#atlstmulc)
  * [P2P Instructions](#p2p-instructions)
    + [AT+NWM](#atnwm)
    + [AT+P2P](#atp2p)
    + [AT+CAD](#atcad)
    + [AT+RXBOOST](#atrxboost)
    + [AT+PSEND](#atpsend)
    + [AT+PRECV](#atprecv)
    + [AT+PRECVDC](#atprecvdc)
  * [Custom AT Commands](#custom-at-commands)
    + [ATC+<CMD>](#atccmd)
  * [Asynchronous Events](#asynchronous-events)
  * [RUI3 commands that are not implemented](#rui3-commands-that-are-not-implemented)

## General Commands

This section describes the generic commands related to the device.

### AT

Description: Attention

This command is used to check that the communication is working properly.

| Command | Input Parameter | Return Value | Return Code |
| ------- | --------------- | ------------ | ----------- |
| `AT` | - | - | OK |

[Back](#content)

### ATZ

Description: MCU Reset

This command is used to trigger a reset on the module.

| Command | Input Parameter | Return Value | Return Code |
| ------- | --------------- | ------------ | ----------- |
| `ATZ` | - | No return value and return code. The module resets. | - |

## _💡 NOTE_
----
_On RAK4631 and RAK3312 the MCU is reset immediately, no status is printed. On RAK11310 no reset is done and `OK` is returned._

_Unlike RUI3 no device information banner is printed by the library. The application decides what it prints in `setup()`._

----

[Back](#content)

### ATR

Description: Restore factory defaults

This command copies the **factory backup** over the current configuration and over the saved user configuration. The factory backup is created with `AT+FACTORY`.

| Command | Input Parameter | Return Value | Return Code |
| ------- | --------------- | ------------ | ----------- |
| `ATR` | - | - | OK<br/>`AT_ERROR` |

**Example:**

```text
ATR
OK
```

## _⚠️ WARNING_
----
_`AT_ERROR` is returned, preceded by the text `no factory backup saved yet - run AT+FACTORY first`, if no factory backup exists. The configuration is not changed in that case._

_The MCU is **not** reset. Reset the device (`ATZ`) so that all settings become active. The difference between `ATR`, `AT+RESTORE` and `AT+FACTORY` is explained in the [production flow](README.md#production-flow-factory-defaults-vs-user-config) of the README._

----

[Back](#content)

### AT+SN

Description: Serial number

This command reads the unique serial number of the MCU (8 bytes).

| Command | Input Parameter | Return Value | Return Code |
| ------- | --------------- | ------------ | ----------- |
| `AT+SN=?` | - | `AT+SN=<16 hex>` | OK |

**Example:**

```text
AT+SN=?
AT+SN=C1D2E3F405A6B7C8
OK
```

[Back](#content)

### AT+VER

Description: Version of the firmware

This command is used to access the version string of the library in RUI3 format.

| Command | Input Parameter | Return Value | Return Code |
| ------- | --------------- | ------------ | ----------- |
| `AT+VER=?` | - | `AT+VER=RUI_comp_<version>_<board>` | OK |

**Example:**

```text
AT+VER=?
AT+VER=RUI_comp_1.0.0_RAK4631
OK
```

## _💡 NOTE_
----
_The string contains the version of the **WisBlockLoRaWAN library**, not of a RUI3 firmware. `<board>` is `RAK4631`, `RAK3312` or `RAK11310`._

----

[Back](#content)

### AT+HWMODEL

Description: The string of the hardware model

This command is used to access the string of the hardware model.

| Command | Input Parameter | Return Value | Return Code |
| ------- | --------------- | ------------ | ----------- |
| `AT+HWMODEL=?` | - | `<module model>` | OK |

**Example:**

```text
AT+HWMODEL=?
rak4630
OK
```

## _💡 NOTE_
----
_The value is printed **without** the `AT+HWMODEL=` prefix. It is `rak4630` (nRF52840), `rak3112` (ESP32-S3) or `rak11310` (RP2040)._

----

[Back](#content)

### AT+HWID

Description: The string of the hardware ID

This command is used to access the string of the MCU type.

| Command | Input Parameter | Return Value | Return Code |
| ------- | --------------- | ------------ | ----------- |
| `AT+HWID=?` | - | `<module hw ID>` | OK |

**Example:**

```text
AT+HWID=?
nrf52840
OK
```

## _💡 NOTE_
----
_The value is printed **without** the `AT+HWID=` prefix. It is `nrf52840`, `esp32-s3` or `rp2040`._

----

[Back](#content)

### AT+ALIAS

Description: Alias name of the device

This command allows the user to set an alias name for the device. The alias is stored in the configuration (`AT+SAVE`) and has no influence on the operation.

| Command | Input Parameter | Return Value | Return Code |
| ------- | --------------- | ------------ | ----------- |
| `AT+ALIAS=?` | - | `AT+ALIAS=<string, 16char>` | OK |
| `AT+ALIAS=<Input>` | `<string, 16char>` | - | OK<br/>`AT_PARAM_ERROR` |

**Example:**

```text
AT+ALIAS=RAK
OK
```

## _💡 NOTE_
----
_`AT_PARAM_ERROR` is returned when the string is longer than 16 characters._

_The factory default alias is longer than 16 characters. It can be read but cannot be typed in again._

----

[Back](#content)

### AT+FIRMWAREVER

Description: Free-form firmware version label

This command allows the user to set a free-form firmware label. It is stored in the configuration (`AT+SAVE`).

| Command | Input Parameter | Return Value | Return Code |
| ------- | --------------- | ------------ | ----------- |
| `AT+FIRMWAREVER=?` | - | `AT+FIRMWAREVER=<string, 31char>` | OK |
| `AT+FIRMWAREVER=<Input>` | `<string, 31char>` | - | OK<br/>`AT_PARAM_ERROR` |

**Example:**

```text
AT+FIRMWAREVER=MyFW_1.2
OK
```

## _💡 NOTE_
----
_The maximum is 31 characters. RUI3 documents 32._

----

[Back](#content)

### AT+STATUS

Description: Status dump

This command prints the current mode and the main settings. It is a WisBlockLoRaWAN specific command.

| Command | Input Parameter | Return Value | Return Code |
| ------- | --------------- | ------------ | ----------- |
| `AT+STATUS` | - | `MODE=...` and further `<name>=<value>` lines | OK |

**Example:**

```text
AT+STATUS
MODE=LORAWAN
REGION=13
CLASS=A
ADR=1
MASK=0000
JOINSTATE=2
OK
```

## _⚠️ WARNING_
----
_`REGION=` shows the **internal** `WisBlockRegion` number (13 = EU433, 0 = EU868, 1 = US915, ...), **not** the RUI3 band number of `AT+BAND`. Use `AT+BAND=?` to read the RUI3 band number._

_`JOINSTATE=` is the number of `WisBlockJoinState`: 0 = idle, 1 = in progress, 2 = succeeded, 3 = failed, 4 = gave up._

----

## _💡 NOTE_
----
_In LoRaWAN mode the output contains `MODE`, `REGION`, `CLASS`, `ADR`, `MASK` (4 hex digits) and `JOINSTATE`. In P2P mode it contains `MODE=LORA_P2P`, `FREQ`, `SF`, `BW` (bandwidth index), `CR` (coding rate index) and `CAD`._

----

[Back](#content)

### AT+BOOT

Description: Bootloader mode

This command restarts the device into the bootloader for firmware upload.

| Command | Input Parameter | Return Value | Return Code |
| ------- | --------------- | ------------ | ----------- |
| `AT+BOOT` | - | - | - (the device restarts) |

## _💡 NOTE_
----
_RAK4631: the device restarts into the UF2 bootloader and shows up as a USB drive._

_RAK3312: the device is only restarted. Hold the BOOT button while resetting to enter the ROM bootloader._

_RAK11310: not supported, `OK` is returned without any action._

----

[Back](#content)

## Low Power

### AT+LOWPOWER

Description: Low power flag

This command sets the low power flag of the configuration (`isLowPowerEnabled()`). The library is intended to be used with the background task, which puts the MCU to sleep between events, the flag is a switch for application code.

| Command | Input Parameter | Return Value | Return Code |
| ------- | --------------- | ------------ | ----------- |
| `AT+LOWPOWER=?` | - | `AT+LOWPOWER=<0 or 1>` | OK |
| `AT+LOWPOWER=<Input>` | 0 or 1 | - | OK |

**Example:**

```text
AT+LOWPOWER=1
OK

AT+LOWPOWER=?
AT+LOWPOWER=1
OK
```

## _💡 NOTE_
----
_RUI3 commands `AT+SLEEP`, `AT+LPM` and `AT+LPMLVL` do not exist here._

_Any value other than `0` is treated as `1`._

----

[Back](#content)

## Configuration Storage

The configuration is kept in the flash of the MCU. There are two slots, the **user** slot and the **factory** slot.

### AT+SAVE

Description: Save configuration

This command writes the current settings to the user slot. Without `AT+SAVE` all changes are lost at the next reset.

| Command | Input Parameter | Return Value | Return Code |
| ------- | --------------- | ------------ | ----------- |
| `AT+SAVE` | - | - | OK<br/>`AT_ERROR` |

## _💡 NOTE_
----
_Multicast groups are never saved._

----

[Back](#content)

### AT+RESTORE

Description: Restore saved configuration

This command reloads the user slot and discards all unsaved changes.

| Command | Input Parameter | Return Value | Return Code |
| ------- | --------------- | ------------ | ----------- |
| `AT+RESTORE` | - | - | OK<br/>`AT_ERROR` |

## _💡 NOTE_
----
_`AT_ERROR` is returned when nothing was saved before. The defaults are loaded in that case._

----

[Back](#content)

### AT+FACTORY

Description: Save factory backup

This command copies the **current** configuration into the factory slot and then resets the MCU. It is intended as one-time production step, for example after `AT+DEVEUI` was set to the real, unique DevEUI of the device.

| Command | Input Parameter | Return Value | Return Code |
| ------- | --------------- | ------------ | ----------- |
| `AT+FACTORY` | - | - | OK<br/>`AT_ERROR` |

## _⚠️ WARNING_
----
_The MCU resets right after `OK` was sent (RAK4631, RAK3312)._

_Use `ATR` to bring a device back to this factory backup._

----

[Back](#content)

## LoRaWAN Keys and IDs

This section describes the commands related to the activation of the end device. EUIs and keys are MSB first.

## _💡 NOTE_
----
_All keys and IDs can be **read back in plain text**, including `AT+APPKEY`, `AT+NWKSKEY` and `AT+APPSKEY`. This is intentional and identical to RUI3._

_The session keys that an OTAA join generates cannot be read, the values of `AT+NWKSKEY` and `AT+APPSKEY` are the ABP keys of the configuration._

----

### AT+DEVEUI

Description: Device EUI

This command is used to access the unique end-device ID. Used in OTAA mode.

| Command | Input Parameter | Return Value | Return Code |
| ------- | --------------- | ------------ | ----------- |
| `AT+DEVEUI=?` | - | `AT+DEVEUI=<8 hex>` | OK |
| `AT+DEVEUI=<Input>` | <8 hex> | - | OK<br/>`AT_PARAM_ERROR` |

**Example:**

```text
AT+DEVEUI=?
AT+DEVEUI=AC1F09FFFE0679DB
OK

AT+DEVEUI=1122334455667788
OK
```

## _💡 NOTE_
----
_`AT_PARAM_ERROR` is returned when the value is not exactly 16 hex digits._

_The new value is applied to the LoRa stack immediately._

----

[Back](#content)

### AT+APPEUI

Description: Application identifier (JoinEUI)

This command is used to access the application identifier (AppEUI, called JoinEUI in LoRaWAN 1.1) in OTAA mode. `AT+JOINEUI` is accepted as a synonym for the same value.

| Command | Input Parameter | Return Value | Return Code |
| ------- | --------------- | ------------ | ----------- |
| `AT+APPEUI=?` | - | `AT+APPEUI=<8 hex>` | OK |
| `AT+APPEUI=<Input>` | <8 hex> | - | OK<br/>`AT_PARAM_ERROR` |

**Example:**

```text
AT+APPEUI=0102030405060708
OK

AT+APPEUI=010203040506070809
AT_PARAM_ERROR
```

## _💡 NOTE_
----
_The query of `AT+JOINEUI=?` also prints the tag `AT+APPEUI=`._

----

[Back](#content)

### AT+APPKEY

Description: Application key

This command is used to access the application key in OTAA mode. The same key is used as NwkKey.

| Command | Input Parameter | Return Value | Return Code |
| ------- | --------------- | ------------ | ----------- |
| `AT+APPKEY=?` | - | `AT+APPKEY=<16 hex>` | OK |
| `AT+APPKEY=<Input>` | <16 hex> | - | OK<br/>`AT_PARAM_ERROR` |

**Example:**

```text
AT+APPKEY=01020AFBA1CD4D20010230405A6B7F88
OK
```

## _💡 NOTE_
----
_`AT_PARAM_ERROR` is returned when the value is not exactly 32 hex digits._

----

[Back](#content)

### AT+DEVADDR

Description: Device address

This command is used to access the device address. It can be set for ABP.

| Command | Input Parameter | Return Value | Return Code |
| ------- | --------------- | ------------ | ----------- |
| `AT+DEVADDR=?` | - | `AT+DEVADDR=<4 hex>` | OK |
| `AT+DEVADDR=<Input>` | <4 hex> | - | OK<br/>`AT_PARAM_ERROR` |

**Example:**

```text
AT+DEVADDR=01020A0B
OK

AT+DEVADDR=?
AT+DEVADDR=01020A0B
OK
```

## _💡 NOTE_
----
_After an OTAA join the query returns the **address the network assigned** (`00000000` before the first join)._

_`AT_PARAM_ERROR` is returned when the value is not exactly 8 hex digits._

----

[Back](#content)

### AT+APPSKEY

Description: Application session key

This command is used to access the application session key in ABP mode.

| Command | Input Parameter | Return Value | Return Code |
| ------- | --------------- | ------------ | ----------- |
| `AT+APPSKEY=?` | - | `AT+APPSKEY=<16 hex>` | OK |
| `AT+APPSKEY=<Input>` | <16 hex> | - | OK<br/>`AT_PARAM_ERROR` |

**Example:**

```text
AT+APPSKEY=01020AFBA1CD4D20010230405A6B7F88
OK
```

[Back](#content)

### AT+NWKSKEY

Description: Network session key

This command is used to access the network session key in ABP mode.

| Command | Input Parameter | Return Value | Return Code |
| ------- | --------------- | ------------ | ----------- |
| `AT+NWKSKEY=?` | - | `AT+NWKSKEY=<16 hex>` | OK |
| `AT+NWKSKEY=<Input>` | <16 hex> | - | OK<br/>`AT_PARAM_ERROR` |

**Example:**

```text
AT+NWKSKEY=01020AFBA1CD4D20010230405A6B7F88
OK
```

[Back](#content)

## LoRaWAN Joining and Sending

This section describes the commands related to the join procedure and data payload.

### AT+CFM

Description: Confirm mode

This command is used to configure the uplink payload to be confirmed or unconfirmed type.

| Command | Input Parameter | Return Value | Return Code |
| ------- | --------------- | ------------ | ----------- |
| `AT+CFM=?` | - | `AT+CFM=<0 or 1>` | OK |
| `AT+CFM=<Input>` | 0 or 1 | - | OK |

**Example:**

```text
AT+CFM=1
OK

AT+CFM=?
AT+CFM=1
OK
```

## _💡 NOTE_
----
_The default value is **0**. Any value other than `0` is treated as `1`._

_`AT+CFS` and `AT+RETY` do not exist. The result of a confirmed uplink is not reported separately._

----

[Back](#content)

### AT+JOIN

Description: Join LoRaWAN Network

This command is used to join a LoRaWAN network, or to stop a running join.

| Command | Input Parameter | Return Value | Return Code |
| ------- | --------------- | ------------ | ----------- |
| `AT+JOIN` | - | - | OK |
| `AT+JOIN=?` | - | `AT+JOIN=Param1:Param2:Param3:Param4` | OK |
| `AT+JOIN=<Input Parameter>` | *Param1:Param2:Param3:Param4* | - | OK<br/>`AT_PARAM_ERROR` |
|  | *Param1* = **Join command**: 1 for joining the network, 0 for stop joining. |  |  |
|  | *Param2* = **Auto-Join config**: 1 for Auto-join on power up, 0 for no auto-join. (Optional parameter, 0 is default) |  |  |
|  | *Param3* = **Reattempt interval**: 7 - 255 seconds. (Optional parameter, 8 seconds is default) |  |  |
|  | *Param4* = **No. of join attempts**: 0 - 255. (Optional parameter, 0 = unlimited is default) |  |  |

**Example:**

```text
AT+JOIN=1:0:10:8
OK

AT+JOIN=?
AT+JOIN=0:0:10:8
OK
```

## _💡 NOTE_
----
_This is an asynchronous command. `OK` means that the device started to join. The result is reported through the join callback of the application (in the `ATCommandInterface` example as `+EVT:JOINED` and `+EVT:JOIN_FAILED`), or can be read with `AT+NJS=?`._

_A failed join is retried automatically, see [WisBlockLoRaWAN-API](WisBlockLoRaWAN-API.md#onjoinfailed)._

_Optional parameters that are left out keep their **current** value, they are not reset to the defaults. Parameters 2 - 4 are stored in the configuration, use `AT+SAVE` to keep them._

_The query returns `1` in the first field if the device is joined **or** a join is running. Use `AT+NJS=?` to find out if the device is really joined._

----

## _⚠️ WARNING_
----
_`AT+JOIN=1` always restarts the attempt counter. Do not send it again while a join is running._

_If joining fails, check the region (`AT+BAND=?`), the work mode (`AT+NWM=?`), and the EUIs and keys._

----

[Back](#content)

### AT+NJM

Description: Network join mode

This command is used to access the network join mode.

| Command | Input Parameter | Return Value | Return Code |
| ------- | --------------- | ------------ | ----------- |
| `AT+NJM=?` | - | `AT+NJM=<0 or 1>` | OK |
| `AT+NJM=<Input>` | 0 (ABP) or 1 (OTAA) | - | OK |

**Example:**

```text
AT+NJM=?
AT+NJM=1
OK

AT+NJM=0
OK
```

## _💡 NOTE_
----
_The default value is **1** (OTAA). `0` = ABP, any other value selects OTAA._

_This is the same numbering as RUI3, but **the opposite** of the enum `WisBlockJoinMode` in the C++ API._

----

[Back](#content)

### AT+NJS

Description: Network join status

This command is used to access the current activation status of the device.

| Command | Input Parameter | Return Value | Return Code |
| ------- | --------------- | ------------ | ----------- |
| `AT+NJS=?` | - | `AT+NJS=<0 or 1>` | OK |

**Example:**

```text
AT+NJS=?
AT+NJS=1
OK
```

## _💡 NOTE_
----
_`1` = joined, `0` = not joined._

----

[Back](#content)

### AT+SEND

Description: Send data

This command provides the way to send data on a dedicated port number.

| Command | Input Parameter | Return Value | Return Code |
| ------- | --------------- | ------------ | ----------- |
| `AT+SEND=<input>` | `<port>:<payload>` | - | OK<br/>`AT_PARAM_ERROR`<br/>`AT_NO_NETWORK_JOINED` |

**Example:**

```text
AT+SEND=12:112233
OK
```

## _💡 NOTE_
----
_`<port>`: 1 - 223. Port 0 is refused._

_`<payload>`: even number of hex digits (0-9, a-f, A-F), 1 - 242 bytes. The real limit is smaller and depends on region and data rate. An empty payload (`AT+SEND=2:`) sends an uplink without payload, which can be used to fetch a pending downlink._

_`AT_PARAM_ERROR` is returned for a missing colon, a port with 8 or more characters, an odd number of digits or non-hex characters._

_`AT_NO_NETWORK_JOINED` is returned when the library did not accept the uplink. This is **not only** the case if the device has not joined. It is returned for an invalid port, a payload that is too long for the current data rate, and a second send while one is already waiting._

_This is an asynchronous command. `OK` means that the uplink was accepted (or deferred by one step). The end of the transmission and downlinks are reported through the callbacks of the application, see [Asynchronous Events](#asynchronous-events)._

_`AT+RECV`, `AT+LPSEND` and `AT+CFS` are not implemented._

----

[Back](#content)

### AT+FPENDING

Description: Fetch pending downlinks

This command enables or disables the automatic empty uplink that fetches pending downlinks. When a downlink has the FPending bit set, the library sends an empty uplink so that the network can deliver the next queued downlink without waiting for the next scheduled uplink. It is a WisBlockLoRaWAN specific command.

| Command | Input Parameter | Return Value | Return Code |
| ------- | --------------- | ------------ | ----------- |
| `AT+FPENDING=?` | - | `AT+FPENDING=<0 or 1>` | OK |
| `AT+FPENDING=<Input>` | 0 or 1 | - | OK |

**Example:**

```text
AT+FPENDING=0
OK
```

## _💡 NOTE_
----
_The default value is **1**. Disable it if the duty cycle budget cannot afford the extra uplink._

----

[Back](#content)

## LoRaWAN Network Management

This section provides a set of commands for network management.

### AT+ADR

Description: Adaptive Rate

This command is used to access the adaptive data rate.

| Command | Input Parameter | Return Value | Return Code |
| ------- | --------------- | ------------ | ----------- |
| `AT+ADR=?` | - | `AT+ADR=<0 or 1>` | OK |
| `AT+ADR=<Input>` | 0 or 1 | - | OK |

**Example:**

```text
AT+ADR=?
AT+ADR=1
OK

AT+ADR=0
OK
```

## _💡 NOTE_
----
_The default value is **1**._

_If the setting cannot be applied yet (typically before the join with a data rate that the default channels do not support) the library prints the line `PENDING - not yet valid for the currently enabled channels, will retry automatically` before `OK`. The setting is stored and applied after the next uplinks._

_The query returns the **requested** state._

----

## _⚠️ WARNING_
----
_While ADR is off, the device refuses the data rate and TX power part of `LinkADRReq` MAC commands. Old network servers may log this as invalid data rate / TX power, this is expected. The channel mask part is still accepted._

----

[Back](#content)

### AT+CLASS

Description: LoRa Class

This command is used to access the LoRaWAN class.

| Command | Input Parameter | Return Value | Return Code |
| ------- | --------------- | ------------ | ----------- |
| `AT+CLASS=?` | - | `AT+CLASS=<A, B or C>` | OK |
| `AT+CLASS=<Input>` | A, B or C | - | OK |

**Example:**

```text
AT+CLASS=C
OK

AT+CLASS=?
AT+CLASS=C
OK
```

## _💡 NOTE_
----
_The class can only change on a joined device. A request before the join is stored and applied automatically after the join, `OK` is returned in both cases._

_The query returns the **requested** class. RUI3's Class B state suffix (`B:S0` ... `B:S3`) does not exist._

_Only the first character is evaluated, `b` and `B` are equal. Any other character selects Class A, `AT_PARAM_ERROR` is **not** returned._

----

[Back](#content)

### AT+DR

Description: Data rate

This command is used to access and configure the fixed data rate that is used while ADR is off.

| Command | Input Parameter | Return Value | Return Code |
| ------- | --------------- | ------------ | ----------- |
| `AT+DR=?` | - | `AT+DR=<value>` | OK |
| `AT+DR=<Input>` | `<value>` | - | OK |

**Example:**

```text
AT+DR=?
AT+DR=3
OK

AT+DR=2
OK
```

## _💡 NOTE_
----
_The value is not range checked by the command. If the data rate is not valid for the enabled channels yet, the line `PENDING - not yet valid for the currently enabled channels, will retry automatically` is printed before `OK`._

_The value has no effect while ADR is on._

_**EU433 / RU864 / IN865 / EU868 / CN470 / KR920**: DR0 - DR5. **AS923**: DR2 - DR5. **US915**: DR0 - DR4. **AU915**: DR0 - DR6._

_The query returns the **requested** data rate._

_Complete information about the DR parameter of each region can be found in the [RUI3 Appendix](https://docs.rakwireless.com/product-categories/software-apis-and-libraries/rui3/appendix#data-rate-by-region)._

----

[Back](#content)

### AT+TXP

Description: Transmit power

This command is used to access the stored transmit power index.

| Command | Input Parameter | Return Value | Return Code |
| ------- | --------------- | ------------ | ----------- |
| `AT+TXP=?` | - | `AT+TXP=<value>` | OK |
| `AT+TXP=<Input>` | `<value>` | - | OK |

**Example:**

```text
AT+TXP=?
AT+TXP=0
OK
```

## _⚠️ WARNING_
----
_The value is stored, but it has **no effect on the radio**. The public LoRa Basics Modem API has no call to set a fixed TX power, the power follows the network (ADR) and the region defaults. Do not use it to reduce the output power._

----

[Back](#content)

### AT+LINKCHECK

Description: Verify network link status

This command is used to access and configure the LinkCheck behavior.

| Command | Input Parameter | Return Value | Return Code |
| ------- | --------------- | ------------ | ----------- |
| `AT+LINKCHECK=?` | - | `AT+LINKCHECK=<0, 1 or 2>` | OK |
| `AT+LINKCHECK=<Input>` | 0, 1 or 2 | - | OK<br/>`AT_PARAM_ERROR` |

**Example:**

```text
AT+LINKCHECK=1
OK

AT+SEND=3:12341234
OK
```

## _💡 NOTE_
----
_`0` = disabled<br/>`1` = a LinkCheck request is added to the next uplink only, then the mode goes back to 0<br/>`2` = a LinkCheck request is added to every uplink._

_The query returns the **mode**, not a result. The answer of the network is delivered to the application by the LinkCheck callback (`onLinkCheckAnswer()`), the library does not print `+EVT:LINKCHECK` on its own._

_`AT_PARAM_ERROR` is returned for values above 2._

----

[Back](#content)

### AT+TIMEREQ

Description: Time request

This command requests the network time (DeviceTimeReq). The request is sent with the next uplink. The answer is delivered to the application by the time callback (`onTimeRequestAnswer()`).

| Command | Input Parameter | Return Value | Return Code |
| ------- | --------------- | ------------ | ----------- |
| `AT+TIMEREQ` | - | - | OK |

## _💡 NOTE_
----
_Unlike RUI3, `AT+TIMEREQ=0/1` and `AT+LTIME` do not exist, the command is only run without parameter._

----

[Back](#content)

### Listen Before Talk

RUI3 compatible `AT+LBT`, `AT+LBTRSSI` and `AT+LBTSCANTIME`. LBT is switched on automatically by LoRa Basics Modem in regions where it is mandatory (for example KR920). These commands only give control over it and can be used before the join.

### AT+LBT

Description: LoRaWAN "Listen Before Talk" (LBT)

This command is used to enable or disable LoRaWAN LBT.

| Command | Input Parameter | Return Value | Return Code |
| ------- | --------------- | ------------ | ----------- |
| `AT+LBT=?` | - | `AT+LBT=<0 or 1>` | OK |
| `AT+LBT=<Input>` | 0 or 1 | - | OK |

**Example:**

```text
AT+LBT=?
AT+LBT=0
OK
```

[Back](#content)

### AT+LBTRSSI

Description: LoRaWAN "Listen Before Talk" RSSI (LBTRSSI)

This command is used to set or get the LBT RSSI threshold in dBm.

| Command | Input Parameter | Return Value | Return Code |
| ------- | --------------- | ------------ | ----------- |
| `AT+LBTRSSI=?` | - | `AT+LBTRSSI=<RSSI>` | OK |
| `AT+LBTRSSI=<Input>` | `<RSSI>` (signed integer) | - | OK<br/>`AT_PARAM_ERROR` |

**Example:**

```text
AT+LBTRSSI=?
AT+LBTRSSI=-80
OK
```

## _⚠️ WARNING_
----
_Selecting a region does **not** load a region specific threshold. Every region starts with the generic default of -80 dBm. If a certification needs another value, set it here._

----

[Back](#content)

### AT+LBTSCANTIME

Description: LoRaWAN "Listen Before Talk" Scantime (LBTSCANTIME)

This command is used to set or get the LBT listen duration in milliseconds.

| Command | Input Parameter | Return Value | Return Code |
| ------- | --------------- | ------------ | ----------- |
| `AT+LBTSCANTIME=?` | - | `AT+LBTSCANTIME=<time>` | OK |
| `AT+LBTSCANTIME=<Input>` | `<time>` in ms | - | OK<br/>`AT_PARAM_ERROR` |

**Example:**

```text
AT+LBTSCANTIME=?
AT+LBTSCANTIME=5
OK
```

[Back](#content)

## Class B Mode

This section provides a set of commands for Class B mode management. Class B is not yet tested on real networks.

### AT+PGSLOT

Description: Periodicity

This command is used to get or set the unicast ping slot periodicity. A `PingSlotInfoReq` is sent to the network.

| Command | Input Parameter | Return Value | Return Code |
| ------- | --------------- | ------------ | ----------- |
| `AT+PGSLOT=?` | - | `AT+PGSLOT=<0-7>` | OK |
| `AT+PGSLOT=<input>` | 0 - 7 | - | OK |

**Example:**

```text
AT+PGSLOT=1
OK
```

## _💡 NOTE_
----
_The default value is 0. **Periodicity** = 0 means a ping slot about every second, **Periodicity** = 7 every 128 seconds._

_Values above 7 are clamped to 7, `AT_PARAM_ERROR` is not returned._

----

[Back](#content)

### AT+BFREQ

Description: Beacon frequency

This command is used to access the data rate and frequency of the Class B beacon.

| Command | Input Parameter | Return Value | Return Code |
| ------- | --------------- | ------------ | ----------- |
| `AT+BFREQ=?` | - | `AT+BFREQ=<DR>, <frequency in MHz>` | OK |

**Example:**

```text
AT+BFREQ=?
AT+BFREQ=3, 434.665
OK
```

## _💡 NOTE_
----
_Read only. The format differs from RUI3 (`BCON: 3, 869.525`). Before a beacon was received the values are `0, 0.000`._

----

[Back](#content)

### AT+BTIME

Description: Beacon time

This command is used to access the time of the last received beacon (seconds since the GPS epoch).

| Command | Input Parameter | Return Value | Return Code |
| ------- | --------------- | ------------ | ----------- |
| `AT+BTIME=?` | - | `AT+BTIME=<GPS seconds>` | OK |

**Example:**

```text
AT+BTIME=?
AT+BTIME=1226592311
OK
```

## _💡 NOTE_
----
_Read only. `0` if no beacon was received yet. `AT+BGW` is not implemented._

----

[Back](#content)

## LoRaWAN Regional Commands

This section provides the set of commands related to channels and LoRaWAN regions.

### AT+MASK

Description: Set the channel mask, close or open the channel

This command selects a sub-band of the device by a hexadecimal channel mask.

| Command | Input Parameter | Return Value | Return Code |
| ------- | --------------- | ------------ | ----------- |
| `AT+MASK=?` | - | `AT+MASK=<mask>` | OK |
| `AT+MASK=<input>` | `<mask>` | - | OK<br/>`AT_PARAM_ERROR` |

**Example:**

```text
AT+MASK=0001
OK

AT+MASK=?
AT+MASK=0001
OK
```

## _💡 NOTE_
----
_Only US915, AU915, CN470 and CN470 RP1.0 use the mask. In all other regions the command returns `OK` but has no effect and the query returns the mask that is active in the stack._

_`<Input>`: up to 4 hex digits, 0-9, a-f, A-F, representing a 16-bit mask. Bit N enables sub-band N+1 (8 channels each), `0000` selects all channels. `AT_PARAM_ERROR` is returned for non-hex input or values above `FFFF`._

_Set it **before** the join. It is applied again before every join attempt._

_The sub-band table is the same as in the [RUI3 AT Command Manual](https://docs.rakwireless.com/product-categories/software-apis-and-libraries/rui3/at-command-manual/#atmask). `AT+CHE` and `AT+CHS` are not implemented._

----

[Back](#content)

### AT+BAND

Description: Active region

This command sets numbers corresponding to active regions. It uses the **same numbering as the C++ API** (`WisBlockRUI3Band`, `setRegion()`, `getRegion()`).

| Command | Input Parameter | Return Value | Return Code |
| ------- | --------------- | ------------ | ----------- |
| `AT+BAND=?` | - | `AT+BAND=<0 - 11>` | OK<br/>`AT_ERROR` |
| `AT+BAND=<Input>` | 0,1,2,3,4,5,6,7,8,9,10,11 | - | OK<br/>`AT_ERROR` |

**Example:**

```text
AT+BAND=?
AT+BAND=4
OK

AT+BAND=0
OK
```

## _💡 NOTE_
----
_0: EU433, 1: CN470, 2: RU864, 3: IN865, 4: EU868, 5: US915, 6: AU915, 7: KR920, 8: AS923-1, 9: AS923-2, 10: AS923-3, 11: AS923-4, 12: LA915._

_The default value is **4** (EU868)._

_`12` (LA915) is **not supported**. `AT_ERROR` is returned, preceded by the text `unsupported band index - LA915 not built into this LBM vendoring`. The same happens for values above 12. It is **not** `AT_PARAM_ERROR`._

_If the active region has no RUI3 number (`CN470_RP_1_0`, 2.4 GHz), the query returns `AT_ERROR` with the text `current region has no RUI3 band index`._

_**EU433** is provided by the library itself (RP002-1.0.4 defaults: channels 433.175 / 433.375 / 433.575 MHz, RX2 434.665 MHz DR0, DR0 - DR7, 1 % duty cycle). Semtech's LoRa Basics Modem does not contain it anymore._

_If you are using US915 with an 8-channel gateway on channels 8 - 15, use `AT+MASK=0002`._

----

## _⚠️ WARNING_
----
_The region can only be changed **before the device has joined**. Afterwards the change is refused by the stack, although `OK` is still returned by the command. Save with `AT+SAVE` and reset the device to start with the new region._

_The RUI3 variants (low / high frequency hardware) are not checked. Make sure the module you use is built for the frequency band of the region._

----

[Back](#content)

## LoRaWAN Multicast Group

This section describes the commands related to multicast group functionality. The device must already be in Class B or Class C (`AT+CLASS`), the commands do not switch the class. Up to 4 groups can be active. Groups are kept in RAM only and are lost at reset.

### AT+ADDMULC

Description: Add multicast group

This command is used to add a new multicast group and multicast parameters.

| Command | Input Parameter | Return Value | Return Code |
| ------- | --------------- | ------------ | ----------- |
| `AT+ADDMULC=<Input Parameter>` | `[Class]:[DevAddr]:[NwkSKey]:[AppSKey]:[Frequency]:[Datarate]:[Periodicity]` | - | OK<br/>`AT_PARAM_ERROR`<br/>`AT_ERROR` |

**Example:**

```text
AT+ADDMULC=C:11223344:11223344556677881122334455667788:11223344556677881122334455667788:434665000:0:0
OK
```

## _💡 NOTE_
----
_Class B and Class C use the same command input parameters. The periodicity is required even for Class C, otherwise `AT_PARAM_ERROR` is returned._

_`[Class]` is `B` or `C`. `[DevAddr]` is 8 hex digits, the keys are 32 hex digits, `[Frequency]` is in Hz._

_The group is identified by its DevAddr. Running the command again with the same DevAddr updates that group._

_`AT_ERROR` is returned if all 4 groups are used (the text `all 4 multicast groups already in use - AT+RMVMULC one first` is printed before), or if the stack rejects the group. The stack rejects it for example when the device is not in the requested class, or the **frequency is not valid for the active region**. A log line like `INVALID FREQUENCY = 916800000` together with `AT_ERROR` means the frequency does not belong to the active band, e.g. a US915 frequency with EU433 active. For EU433 use a frequency between 433050000 and 434790000, e.g. the RX2 frequency 434665000._

----

[Back](#content)

### AT+RMVMULC

Description: Remove multicast group

This command is used to remove a configured multicast group.

| Command | Input Parameter | Return Value | Return Code |
| ------- | --------------- | ------------ | ----------- |
| `AT+RMVMULC=<Input Parameter>` | `<DevAddr>` | - | OK<br/>`AT_PARAM_ERROR`<br/>`AT_ERROR` |

**Example:**

```text
AT+RMVMULC=11223344
OK
```

## _💡 NOTE_
----
_You can only remove a group whose DevAddr was added before. Otherwise `AT_ERROR` is returned, preceded by the text `no multicast group configured with this DevAddr`._

----

[Back](#content)

### AT+LSTMULC

Description: Multicast list

This command is used to get the information about the configured multicast groups.

| Command | Input Parameter | Return Value | Return Code |
| ------- | --------------- | ------------ | ----------- |
| `AT+LSTMULC=?` | - | `[Class]:[DevAddr]:[NwkSKey]:[AppSKey]:[Frequency]:[Datarate]` (one line per group) | OK |

**Example:**

```text
AT+LSTMULC=?
C:11223344:11223344556677881122334455667788:11223344556677881122334455667788:434665000:0
OK
```

## _💡 NOTE_
----
_The keys are shown in plain text, as in RUI3. Only the groups that are in use are listed. Without a group only `OK` is returned._

_The lines do not have the `AT+LSTMULC=` prefix and no group number._

----

[Back](#content)

## P2P Instructions

This section describes the commands related to LoRa point-to-point functionality.

### AT+NWM

Description: LoRa network work mode

This command switches between LoRa P2P mode and LoRaWAN mode.

| Command | Input Parameter | Return Value | Return Code |
| ------- | --------------- | ------------ | ----------- |
| `AT+NWM=?` | - | `AT+NWM=<0 or 1>` | OK |
| `AT+NWM=<Input>` | 0 (P2P) or 1 (LoRaWAN) | - | OK<br/>`AT_PARAM_ERROR` |

**Example:**

```text
AT+NWM=?
AT+NWM=1
OK

AT+NWM=0
OK
```

## _💡 NOTE_
----
_The default value is **1** (LoRaWAN)._

_`2` (P2P_FSK) is **not supported** and returns `AT_PARAM_ERROR`. Any other value than `1` selects P2P._

_Unlike RUI3, the device does **not restart**. The mode change is active immediately. Use `AT+SAVE` to keep it over a reset._

----

[Back](#content)

### AT+P2P

Description: LoRa P2P radio parameters

This command sets or gets all P2P radio parameters with one command. It replaces the single commands `AT+PFREQ`, `AT+PSF`, `AT+PBW`, `AT+PCR`, `AT+PPL` and `AT+PTP` of RUI3.

| Command | Input Parameter | Return Value | Return Code |
| ------- | --------------- | ------------ | ----------- |
| `AT+P2P=?` | - | `AT+P2P=<freq>:<sf>:<bw>:<cr>:<preamble>:<txpower>` | OK |
| `AT+P2P=<Input>` | `<freq>:<sf>:<bw>:<cr>:<preamble>:<txpower>` | - | OK<br/>`AT_PARAM_ERROR` |
|  | *freq* = frequency in Hz |  |  |
|  | *sf* = spreading factor 7 - 12 |  |  |
|  | *bw* = bandwidth **index**: 0 = 125 kHz, 1 = 250 kHz, 2 = 500 kHz, 3 = 62.5 kHz, 4 = 41.67 kHz, 5 = 31.25 kHz, 6 = 20.83 kHz, 7 = 15.63 kHz, 8 = 10.42 kHz, 9 = 7.81 kHz |  |  |
|  | *cr* = coding rate **index**: 1 = 4/5, 2 = 4/6, 3 = 4/7, 4 = 4/8 |  |  |
|  | *preamble* = preamble length in symbols |  |  |
|  | *txpower* = TX power in dBm |  |  |

**Example:**

```text
AT+P2P=868000000:7:0:1:8:14
OK

AT+P2P=?
AT+P2P=868000000:7:0:1:8:14
OK
```

## _⚠️ WARNING_
----
_**The bandwidth and coding rate are indexes, not kHz values and not the RUI3 numbering.** RUI3 uses `125` / `250` / `500` for the bandwidth and `0` - `3` for the coding rate in `AT+P2P`. Here `AT+P2P=868000000:7:125:1:8:14` would select an invalid bandwidth._

----

## _💡 NOTE_
----
_Parameters that are left out (from the end of the list) are replaced by the defaults SF 7, BW index 0, CR index 1, preamble 8 and 14 dBm. They are **not** kept at their previous value. The only check is that the frequency is not 0._

_The settings are applied to the radio immediately and stored with `AT+SAVE`. The default frequency is 916000000._

_`AT+ENCRY`, `AT+ENCKEY`, `AT+PBR`, `AT+PFDEV`, `AT+IQINVER` and `AT+SYNCWORD` are not implemented._

----

[Back](#content)

### AT+CAD

Description: Channel Activity Detection before send

This command enables or disables a Channel Activity Detection before each `AT+PSEND`.

| Command | Input Parameter | Return Value | Return Code |
| ------- | --------------- | ------------ | ----------- |
| `AT+CAD=?` | - | `AT+CAD=<0 or 1>` | OK |
| `AT+CAD=<Input>` | 0 or 1 | - | OK |

**Example:**

```text
AT+CAD=1
OK
```

## _💡 NOTE_
----
_Unlike RUI3, this command is not a one-shot CAD. A one-shot CAD is only available in the C++ API (`startP2PCad()`)._

----

[Back](#content)

### AT+RXBOOST

Description: RX boosted gain

This command enables or disables the SX1262 boosted RX gain. It gives a few dB more sensitivity for about 4 - 5 mA more RX current. It is a WisBlockLoRaWAN specific command.

| Command | Input Parameter | Return Value | Return Code |
| ------- | --------------- | ------------ | ----------- |
| `AT+RXBOOST=?` | - | `AT+RXBOOST=<0 or 1>` | OK |
| `AT+RXBOOST=<Input>` | 0 or 1 | - | OK |

**Example:**

```text
AT+RXBOOST=0
OK
```

## _💡 NOTE_
----
_The default is **1** (boosted gain on)._

----

[Back](#content)

### AT+PSEND

Description: P2P send data

This command sends a LoRa P2P packet.

| Command | Input Parameter | Return Value | Return Code |
| ------- | --------------- | ------------ | ----------- |
| `AT+PSEND=<Input>` | `<payload>` | - | OK<br/>`AT_PARAM_ERROR`<br/>`AT_ERROR` |

**Example:**

```text
AT+PSEND=48656C6C6F
OK
```

## _💡 NOTE_
----
_`<payload>`: even number of hex digits, 1 - 255 bytes._

_`OK` means that the transmission was started. The end of the transmission is reported by the P2P TX callback (`+EVT:PTXDONE` in the `ATCommandInterface` example)._

_`AT_ERROR` is returned if the transmission could not be started._

----

[Back](#content)

### AT+PRECV

Description: P2P receive

This command puts the radio into receive mode.

| Command | Input Parameter | Return Value | Return Code |
| ------- | --------------- | ------------ | ----------- |
| `AT+PRECV=<Input>` | `<timeout in ms>` | - | OK |

**Example:**

```text
AT+PRECV=0
OK

AT+PRECV=5000
OK
```

## _⚠️ WARNING_
----
_**`0` means continuous receive.** The RUI3 values `65533`, `65534` and the RUI3 `AT+PRECV=?` query are not supported. A received packet is reported by the P2P RX callback (`+EVT:PRXDONE` in the `ATCommandInterface` example), the payload is delivered to the application only._

----

[Back](#content)

### AT+PRECVDC

Description: P2P receive with hardware duty cycle

This command puts the radio into the SX1262 hardware RX duty cycle mode, the chip alternates between RX and sleep on its own. It needs less average current than waking the MCU repeatedly. It is a WisBlockLoRaWAN specific command.

| Command | Input Parameter | Return Value | Return Code |
| ------- | --------------- | ------------ | ----------- |
| `AT+PRECVDC=<rxTimeMs>:<sleepTimeMs>` | `<rxTimeMs>:<sleepTimeMs>` in ms | - | OK<br/>`AT_PARAM_ERROR` |
| `AT+PRECVDC=AUTO` | - | - | OK<br/>`AT_PARAM_ERROR` |
| `AT+PRECVDC=AUTO:<txPreambleSymbols>` | `<txPreambleSymbols>` | - | OK<br/>`AT_PARAM_ERROR` |

**Example:**

```text
AT+PRECVDC=AUTO:32
OK

AT+PRECVDC=10:40
OK
```

## _💡 NOTE_
----
_`AUTO:<txPreambleSymbols>` calculates the timing for the **transmitter's** preamble length. This is the preferred form. `AUTO` alone uses the preamble length of this device, which usually differs from the transmitter's._

_`AT_PARAM_ERROR` is returned if a time is 0, or if the preamble is too short for any usable window._

_Send `AT+PRECV=0` or use the API `stopP2PReceive()` to leave the duty cycle mode._

----

## _⚠️ WARNING_
----
_`rxTime + sleepTime` must stay below the preamble duration of the transmitter, otherwise packets can be missed while the radio sleeps. In real hardware tests more margin was needed than the datasheet formula suggests, so verify the values on your own link._

----

[Back](#content)

## Custom AT Commands

Applications can register their own commands with `WisBlockLoRaAT::addCustomATCommand()` (comparable to `api.system.atMode.add()` in RUI3). Custom commands use the prefix `ATC+`. The API is described in [WisBlockLoRaWAN-API](WisBlockLoRaWAN-API.md#addcustomatcommand).

### ATC+<CMD>

Description: Application defined command

Runs the handler that was registered for `<CMD>`. The command name is not case sensitive. Up to 16 custom commands can be registered.

| Command | Input Parameter | Return Value | Return Code |
| ------- | --------------- | ------------ | ----------- |
| `ATC+<CMD>` | - | Defined by the handler | Status of the handler |
| `ATC+<CMD>=?` | - | Defined by the handler | Status of the handler |
| `ATC+<CMD>=<Input>` | Defined by the handler | Defined by the handler | Status of the handler |

**Example:**

```text
ATC+LED=1
OK

ATC+LED=7
AT_PARAM_ERROR
```

## _💡 NOTE_
----
_The handler receives `nullptr` for the bare command, the string `?` for a query, and the text after the `=` for a set command. The text is not split, the handler has to parse it._

_The handler returns `WISBLOCK_AT_OK` (`OK`), `WISBLOCK_AT_PARAM_ERROR` (`AT_PARAM_ERROR`) or `WISBLOCK_AT_ERROR` (`AT_ERROR`). Anything the handler prints itself comes before the status._

_An unknown `ATC+` command returns `AT_ERROR`._

----

[Back](#content)

## Asynchronous Events

The AT interface itself does **not** print asynchronous events. Results of joins, uplinks and downlinks are delivered through the callbacks of the application (`onJoinSuccess()`, `onLoRaWANTxFinished()`, `onLoRaWANRxFinished()`, `onP2PRxFinished()`, ...). The `ATCommandInterface` example prints them in the following format. Host software can rely on these lines only if the application uses the same format.

| Event | Serial output in the `ATCommandInterface` example | Description |
| ----- | ------------------------------------------------- | ----------- |
| Join success | `+EVT:JOINED` | The device joined the network. |
| Join failed | `+EVT:JOIN_FAILED` | One join attempt failed, the library retries. |
| LoRaWAN uplink | `+EVT:TXDONE=<0 or 1>` | 1 = the uplink was transmitted. |
| LoRaWAN downlink | `+EVT:RXDONE=<length>,<rssi>,<snr>` | A downlink was received. The payload is **not** printed. |
| P2P TX | `+EVT:PTXDONE=<0 or 1>` | 1 = the packet was transmitted. |
| P2P RX | `+EVT:PRXDONE=<length>,<rssi>,<snr>` | A P2P packet was received. The payload is **not** printed. |
| P2P CAD | `+EVT:CAD=<0, 1 or 2>` | 0 = clear, 1 = channel activity detected, 2 = error. |

## _ℹ️ INFO_
----
_These lines differ from the RUI3 events (`+EVT:JOINED`, `+EVT:TX_DONE`, `+EVT:RX_1:<rssi>:<snr>:UNICAST:<port>:<payload>`, `+EVT:RXP2P:...`). Change the callbacks of your sketch if your host software expects the RUI3 format._

----

[Back](#content)

## RUI3 commands that are not implemented

These RUI3 commands are **not** available. They return `AT_ERROR`.

| Group | Commands |
| ----- | -------- |
| General | `AT?`, `ATE`, `AT+BAT`, `AT+BUILDTIME`, `AT+REPOINFO`, `AT+CLIVER`, `AT+APIVER`, `AT+SYSV`, `AT+BLEMAC`, `AT+BOOTVER` |
| Low power | `AT+SLEEP`, `AT+LPM`, `AT+LPMLVL` |
| Serial | `AT+LOCK`, `AT+PWORD`, `AT+BAUD`, `AT+ATM` |
| Bootloader | `AT+BOOTSTATUS`, `AT+RUN`, `AT+RESET`, `AT+UPDATE` (the library has no separate bootloader command mode) |
| Keys | `AT+NETID`, `AT+MCROOTKEY` |
| Join and send | `AT+CFS`, `AT+RECV`, `AT+LPSEND`, `AT+RETY` |
| Network | `AT+DCS`, `AT+JN1DL`, `AT+JN2DL`, `AT+PNM`, `AT+RX1DL`, `AT+RX2DL`, `AT+RX2DR`, `AT+RX2FQ`, `AT+LTIME` |
| Class B | `AT+BGW` |
| Information | `AT+RSSI`, `AT+ARSSI`, `AT+SNR` |
| Regional | `AT+CHE`, `AT+CHS` |
| P2P | `AT+PFREQ`, `AT+PSF`, `AT+PBW`, `AT+PCR`, `AT+PPL`, `AT+PTP`, `AT+PBR`, `AT+PFDEV`, `AT+ENCRY`, `AT+ENCKEY`, `AT+PCRYPT`, `AT+PKEY`, `AT+CRYPIV`, `AT+IQINVER`, `AT+SYNCWORD`, `AT+RFFREQUENCY`, `AT+TXOUTPUTPOWER`, `AT+BANDWIDTH`, `AT+SPREADINGFACTOR`, `AT+CODINGRATE`, `AT+PREAMBLELENGTH`, `AT+SYMBOLTIMEOUT`, `AT+FIXLENGTHPAYLOAD` |
| RF test | `AT+TRSSI`, `AT+TTONE`, `AT+TTX`, `AT+TRX`, `AT+TCONF`, `AT+TTH`, `AT+TOFF`, `AT+CERTIF`, `AT+CW`, `AT+TRTH` |

Commands that exist **only** in this library: `AT+STATUS`, `AT+FPENDING`, `AT+RXBOOST`, `AT+PRECVDC`, `AT+LOWPOWER`, `AT+SAVE`, `AT+RESTORE`, `AT+FACTORY`, `AT+FIRMWAREVER`.

[Back](#content)
