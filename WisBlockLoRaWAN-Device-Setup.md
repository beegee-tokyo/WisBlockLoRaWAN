# WisBlockLoRaWAN: setup for every supported device

How to set up the Arduino IDE and PlatformIO for each device the library supports: board, core / platform, code line, and the build settings that are required or optional.

Source of the settings: the library itself (README, `src/wb_lbm_config.h`, `extra_script.py`, the examples and the PlatformIO templates in `examples/PlatformIO`) and the RAKwireless and arduino-pico documentation. All combinations were tested on hardware (see the status table at the end).

## 1. Overview

| Device | LoRa radio | MCU | Arduino IDE board (Tools > Board) | PlatformIO platform / board | Start line in `setup()` | Required extra settings |
|---|---|---|---|---|---|---|
| RAK4631 | internal SX1262 | nRF52840 | WisBlock Core RAK4631 Board | `nordicnrf52` / `rak4630` | `lora.begin();` | none |
| RAK3312 | internal SX1262 | ESP32-S3 | RAKwireless RAK3112 | `espressif32` / `rak3112` | `lora.begin();` | none (USB serial comes with the board definition) |
| RAK11310 | internal SX1262 (RAK11300 module) | RP2040 | RAKwireless RAK11300 (arduino-pico core) | `platform-raspberrypi` (maxgerhardt) / `rakwireless_rak11300` | `lora.begin();` | FreeRTOS SMP and a file system in the flash |
| RAK3401 + RAK13300 / RAK13302 | external SX1262 | nRF52840 | WisBlock Core RAK4631 Board | `nordicnrf52` / `rak3400` | `lora.begin(wisblockLoRaHwConfigRAK3401_RAK13300());` | none |
| RAK3401 + RAK13702 | external Semtech LR2021 (sub-GHz and 2.4 GHz) | nRF52840 | WisBlock Core RAK4631 Board | `nordicnrf52` / `rak3400` | `lora.begin(wisblockLoRaHwConfigRAK3401_RAK13702());` | `WISBLOCK_ENABLE_LR2021` |

Plain `lora.begin()` picks the built-in preset from the MCU: nRF52840 = RAK4631 wiring, ESP32-S3 = RAK3312 wiring, RP2040 = RAK11310 wiring. A RAK3401 has the same MCU as the RAK4631 but no radio on the module, so it **must** use the `begin(...)` line with its own wiring.

## 2. Common to all devices

### Install the library

- **Arduino IDE:** Sketch > Include Library > Add .ZIP Library (or the Library Manager). The library has no dependency on another installed library.
- **PlatformIO:** `lib_deps = beegee-tokyo/WisBlockLoRaWAN` (or the GitHub URL `https://github.com/beegee-tokyo/WisBlockLoRaWAN`).
- The examples with Cayenne LPP and time conversion need two more libraries: `CayenneLPP` (electroniccats) and `Time` (Paul Stoffregen).

### Where build settings go

- **Arduino IDE** cannot pass compiler flags to a library. All settings the library needs (regions, Class A/B/C, stack options) come from `src/wb_lbm_config.h`. To change an option, edit that file in the installed library folder (add a `#define` line, or remove the `//` in front of a commented one). Every source file that uses the LR2021 options includes that header.
- **PlatformIO:** `extra_script.py` of the library adds the same settings. Options are added as `build_flags = -DNAME` in `platformio.ini`.
- In the sections below, an option is written once as `NAME`. Arduino IDE: `#define NAME` in `wb_lbm_config.h`. PlatformIO: `-D NAME` in `build_flags`.

### Compiled in by default (no setting needed)

All LoRaWAN regions of the stack (AS923, AU915, CN470, EU868, EU433, IN865, KR920, RU864, US915), Class A, B and C, and multicast. The region is selected at run time with `lora.setRegion()` or `AT+BAND`. Not included: FUOTA and the LoRaWAN relay.

### LoRaWAN credentials

Set with AT commands (`AT+DEVEUI`, `AT+APPEUI`, `AT+APPKEY`, `AT+BAND`, `AT+JOIN`, ...) or with API calls. See `WisBlockLoRaWAN-AT-Commands.md`. Serial settings: 115200 baud.

### PlatformIO: the `rakwireless` folder

Some WisBlock Core modules are not in the standard PlatformIO platforms. Copy the folder `rakwireless` from one of the projects in `examples/PlatformIO` (for example `BM-LoRaWAN-Template/rakwireless`) into the root of your project. It holds the board definitions (`rak4630`, `rak3400`, `rak3112`, `rak11200`, `rak11300`) and the variant files. The `[platformio]` section needs:

```ini
[platformio]
boards_dir = rakwireless/boards
```

## 3. RAK4631 (nRF52840, internal SX1262)

**Arduino IDE**

1. File > Preferences > Additional Boards Manager URLs: add
   `https://raw.githubusercontent.com/RAKwireless/RAKwireless-Arduino-BSP-Index/main/package_rakwireless_index.json`
2. Tools > Board > Boards Manager: search for `RAK` and install the BSP of the RAK4631.
3. Tools > Board: **WisBlock Core RAK4631 Board**.
4. Code: `lora.begin();`

**PlatformIO** (`platformio.ini`)

```ini
[platformio]
boards_dir = rakwireless/boards

[env:rak4630]
platform = nordicnrf52
board = rak4630
framework = arduino
build_src_filter =
    ${env.build_src_filter}
    +<../rakwireless/variants/rak4630>
build_flags =
    -I rakwireless/variants/rak4630
lib_deps =
    beegee-tokyo/WisBlockLoRaWAN
monitor_speed = 115200
upload_speed = 115200
```

## 4. RAK3312 (ESP32-S3, internal SX1262)

**Arduino IDE**

1. Install the Espressif ESP32 core: Boards Manager > `esp32` by Espressif Systems (or add `https://espressif.github.io/arduino-esp32/package_esp32_index.json` to the Additional Boards Manager URLs).
2. Tools > Board: **RAKwireless RAK3112**. The RAK3312 is supported by the official Espressif core under this name (the RAK3312 documentation says it comes without firmware and uses the Espressif BSP).
3. Code: `lora.begin();`

The library uses the USB serial of the ESP32-S3 for the AT interface when `ARDUINO_USB_CDC_ON_BOOT` is set. The `RAKwireless RAK3112` board definition sets it (and `ARDUINO_USB_MODE=1`). If you use another ESP32-S3 board entry, enable **USB CDC On Boot**, otherwise the AT interface uses a UART.

**PlatformIO**

```ini
[platformio]
boards_dir = rakwireless/boards

[env:rak3112]
platform = espressif32
board = rak3112
framework = arduino
build_src_filter =
    ${env.build_src_filter}
    +<../rakwireless/variants/rak3112>
build_flags =
    -I rakwireless/variants/rak3112
lib_deps =
    beegee-tokyo/WisBlockLoRaWAN
lib_ldf_mode = deep+
monitor_speed = 115200
monitor_filters = colorize, esp32_exception_decoder
```

`board = rak3112` comes from the `rakwireless/boards` folder (RAKwireless describes this as the way to use the module with PlatformIO). Settings are saved in the NVS flash (Preferences), no file system setting is needed.

## 5. RAK11310 (RP2040, internal SX1262)

Only the **arduino-pico core by Earle F. Philhower** is supported. The original Arduino Mbed OS RP2040 core is not supported (the library stops with an `#error`).

**Arduino IDE**

1. File > Preferences > Additional Boards Manager URLs: add
   `https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json`
2. Boards Manager: search `pico`, install **Raspberry Pi Pico/RP2040** by Earle F. Philhower, III (not the Arduino Mbed OS one).
3. Tools > Board: **RAKwireless RAK11300** (the board entry of the RAK11300 / RAK11310).
4. Tools > **Operating System: FreeRTOS SMP**. The library needs FreeRTOS.
5. Tools > **Flash Size** with a file system, for example `2MB (Sketch: 1MB, FS: 1MB)`. The settings are saved in LittleFS.
6. Code: `lora.begin();`

If the upload does not work on Windows, install the WinUSB driver with Zadig: board in BOOTSEL mode, Options > List All Devices, select "RP2 Boot (Interface 1)" (not the mass-storage interface), install WinUSB.

**PlatformIO** (as in the template in `examples/PlatformIO`)

```ini
[env:rak11300]
platform = https://github.com/maxgerhardt/platform-raspberrypi.git
board = rakwireless_rak11300
framework = arduino
board_build.core = earlephilhower
board_build.filesystem = littlefs
board_build.filesystem_size = 0.5m
build_flags =
    -D PIO_FRAMEWORK_ARDUINO_ENABLE_FREERTOS
    -D ARDUINO_ARCH_RP2040
lib_deps =
    beegee-tokyo/WisBlockLoRaWAN
```

`PIO_FRAMEWORK_ARDUINO_ENABLE_FREERTOS` is required. The template also contains `-D HEARTH_SERIAL_PORT=Serial1` with the comment "Not sure why this is needed", it is not listed here. `board_build.core = earlephilhower` is taken from the older RAK11300 environment in the repository, to select the right core explicitly.

## 6. RAK3401 + RAK13300 / RAK13302 (nRF52840 + external SX1262)

The RAK3401 (a RAK3400 WisDuo module on a WisBlock Core board) has no LoRa radio. The SX1262 module (RAK13300 or RAK13302, same wiring) sits in the IO slot. The library calls this combination "RAK3401".

**Arduino IDE**

1. Install the RAKwireless BSP as in section 3.
2. Tools > Board: **WisBlock Core RAK4631 Board**. RAKwireless states that the RAK3401 is not listed as a board and uses the RAK4631 board entry.
3. Code (a different begin than the RAK4631):

```cpp
#define RAK3401              // as in the examples, before the includes
#include <WisBlockLoRaWAN.h>

WisBlockLoRaWAN lora;

void setup()
{
    lora.begin(wisblockLoRaHwConfigRAK3401_RAK13300());
}
```

Start from `examples/RAK3401_RAK13300`.

**PlatformIO**

```ini
[platformio]
boards_dir = rakwireless/boards

[env:rak3400]
platform = nordicnrf52
board = rak3400
framework = arduino
build_src_filter =
    ${env.build_src_filter}
    +<../rakwireless/variants/rak3400>
build_flags =
    -I rakwireless/variants/rak3400
lib_deps =
    beegee-tokyo/WisBlockLoRaWAN
monitor_speed = 115200
upload_speed = 115200
```

The `rak3400` variant defines `RAK3400`, which the examples use to select the begin line (`#ifdef RAK3400`). **Check your code:** the PlatformIO templates in `examples/PlatformIO` (BM-LoRaWAN-Template, BM-LoRa-P2P-Template, BM-LoRaWAN-T_H, LoRaP2PPingPong-PIO) call plain `lora.begin()`, which is the RAK4631 wiring. For the `rak3400` environment change it to `lora.begin(wisblockLoRaHwConfigRAK3401_RAK13300());`.

## 7. RAK3401 + RAK13702 (nRF52840 + external LR2021)

Same hardware and board as section 6, with the LR2021 radio (sub-GHz and 2.4 GHz). The LR2021 backend is **off by default** and only available for the nRF52840.

**Required:** `WISBLOCK_ENABLE_LR2021`

- Arduino IDE: in `src/wb_lbm_config.h` of the installed library remove the `//` in front of `#define WISBLOCK_ENABLE_LR2021`.
- PlatformIO: `build_flags = -D WISBLOCK_ENABLE_LR2021` in the `rak3400` environment of section 6.

**Code**

```cpp
lora.begin(wisblockLoRaHwConfigRAK3401_RAK13702());
```

Start from `examples/RAK3401_RAK13702` (LoRaWAN) or `examples/LoRaP2PPingPong2G4` (P2P, 2.4 GHz). `rxBoostedGainEnabled` has no effect on the LR2021.

**Optional settings** (all default to what worked on a RAK3401 + RAK13702)

| Option | Effect |
|---|---|
| `WISBLOCK_LR2021_TCXO_VOLTAGE=SX126X_TCXO_CTRL_x_xV` | TCXO supply voltage, default 1.6 V |
| `WISBLOCK_LR2021_RX_STARTUP_EXTRA_MS=<ms>` | Extra start-up time reported to the LoRaWAN stack for the RX windows, default 64 |
| `WISBLOCK_LR2021_RFSW_LF_INVERT`, `WISBLOCK_LR2021_RFSW_HF_INVERT` | Swap the polarity of one antenna switch (other boards only) |
| `WISBLOCK_LR2021_LFCLK_XTAL` | Use a 32.768 kHz crystal for the radio, the RAK13702 has none |
| `WISBLOCK_LR2021_DEBUG` | Trace of the radio calls |
| `WW_2G4_SINGLE_DATARATE` | 2.4 GHz LoRaWAN: join requests use DR0 (SF12) only, see below |

**2.4 GHz LoRaWAN**

- Select the region with `lora.setRegion(WISBLOCK_REGION_WW2G4)` or `AT+BAND=13`. DR0 to DR7 are SF12 to SF5 at 812.5 kHz, the default channels are 2403, 2425 and 2479 MHz, RX2 is 2423 MHz at DR0, the maximum is +10 dBm EIRP. A 2.4 GHz antenna is needed.
- The gateway must receive 2.4 GHz LoRa. Gateways with SX1280 concentrators (for example the RAK5148) have **one fixed spreading factor per channel**, SF12 as shipped. A device that draws its join data rate at random (DR0 to DR5, DR0 only 5 % of the time) then joins slowly and after the join it must send at the data rate the gateway receives. For such a gateway enable `WW_2G4_SINGLE_DATARATE` (Arduino IDE: remove the `//` in front of `// #define WW_2G4_SINGLE_DATARATE` in `wb_lbm_config.h`) and set `lora.setDataRate(0)` after the join, with ADR off.
- Tested: OTAA join, uplinks and Class A downlinks on a RAK5148 gateway with a LoRaWAN server.

**P2P at 2.4 GHz:** the frequency alone selects the band (1.5 GHz and above is 2.4 GHz). Wide bandwidths: `WISBLOCK_BW_203`, `_406`, `_812`, `_1000` in the API, indexes 10 to 13 in `AT+PBW`. `AT+PFREQ` and `AT+P2P` accept 150 to 960 MHz on every radio and 2400 to 2483 MHz on the LR2021 only.

## 8. Other devices and options

| Device / option | Status |
|---|---|
| **RAK4630** (nRF52840 + SX1262 WisDuo module) | Same MCU and radio family as the RAK4631 and listed in the library keywords. Use the section 3 settings. Not verified here. |
| **RAK3112 / RAK3212** (ESP32-S3 + SX1262 WisDuo module and breakout) | Same ESP32-S3 + SX1262 combination as the RAK3312, same board entry `RAKwireless RAK3112` in the Espressif core. Section 4 settings. Not verified here that the internal wiring is identical. |
| **RAK11300** (RP2040 + SX1262 module on the RAK11310) | Same as the RAK11310 (section 5). |
| **RAK3401 + RAK13302** | Same wiring as the RAK13300, section 6. |
| **Own board** with nRF52840, ESP32-S3 or RP2040 and an SX1262 | Describe the pins, RF switch and TCXO with a `WisBlockLoRaHwConfig` (`src/WisBlockLoRaHwConfig.h`) and call `lora.begin(config)`. The RAK3401 preset is the worked example. |
| **Own nRF52840 board with an LR2021** | Also possible with your own config (see `wisblockLoRaHwConfigRAK3401_RAK13702()` for the fields, `chipType` LR2021), plus `WISBLOCK_ENABLE_LR2021`. Not tested. |
| **RAK3401 without a radio module** | No LoRa radio, nothing to run. |
| **LR2021 on RAK3312 or RAK11310** | Not supported: the LR2021 backend is nRF52840 only. |
| **RAK11200** (ESP32, no S3) and **STM32 based modules** (RAK3172, RAK11720, RAK11162) | Not supported by this library. |
| **RP2040 with the Arduino Mbed OS core** | Not supported (use arduino-pico). |

## 9. Class B and Class C in ISM2400

The vendored region definition for the 2.4 GHz band (`region_ww_2g4_defs.h`) contains the Class B parameters (beacon at 2424 MHz with DR3, ping slots at 2424 MHz) and the RX2 settings (2423 MHz, DR0) that Class C uses. Class B and Class C are compiled in by default (`ADD_CLASS_B`, `ADD_CLASS_C`). So the stack has what it needs, but **neither class was tested in the 2.4 GHz band**:

- Class B needs a gateway that transmits timed beacons (GPS time) in the 2.4 GHz band. Whether your gateway does this is not known here.
- Class C needs the network server to send downlinks in RX2 (2423 MHz, DR0 = SF12), which a transmitter of an SX1280 gateway should be able to do. Not tested.

## 10. Quick troubleshooting

| Symptom | Check |
|---|---|
| `#error ... Arduino mbed core is not supported` | RP2040: install the arduino-pico core (section 5). |
| `#error Enable FreeRTOS SMP` (examples) | RP2040: Tools > Operating System > FreeRTOS SMP, or `-D PIO_FRAMEWORK_ARDUINO_ENABLE_FREERTOS`. |
| RP2040 settings are not saved | Select a flash size with a file system (section 5). |
| RAK3401 starts but no join / radio errors | The code uses plain `lora.begin()` (RAK4631 wiring). Use the config version (sections 6 and 7). |
| LR2021 code is missing or `wisblockLoRaHwConfigRAK3401_RAK13702` is unknown | `WISBLOCK_ENABLE_LR2021` is not set (section 7). |
| ESP32-S3: no AT answers over USB | Use the `RAKwireless RAK3112` board or enable USB CDC On Boot (section 4). |
| 2.4 GHz join takes very long, uplinks do not reach the server | The gateway has one fixed spreading factor per channel, use `WW_2G4_SINGLE_DATARATE` and DR0 (section 7). |
| Upload fails on RAK11310 (Windows) | Zadig, WinUSB driver (section 5). |

## 11. What was tested

| Combination | Tested on hardware |
|---|---|
| RAK3401 + RAK13702, sub-GHz | LoRa P2P, LoRa P2P to an SX1262 node, LoRaWAN OTAA join and uplinks (AS923-3) with ChirpStack, TTN and Actility |
| RAK3401 + RAK13702, 2.4 GHz | LoRa P2P, LoRaWAN OTAA join, uplinks and Class A downlinks (RAK5148 gateway) |
| RAK4631, RAK3312, RAK11310, RAK3401 + RAK13300 | LoRa P2P, LoRaWAN OTAA join and uplinks (US915, EU868, AU915, AS923-3) with Chirpstack, TTN, Loriot nd Actility. |
