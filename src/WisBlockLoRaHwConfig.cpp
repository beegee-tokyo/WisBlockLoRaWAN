/* WisBlockLoRaWAN: build flags for the Arduino IDE, see wb_lbm_config.h */
#include "wb_lbm_config.h"
#include "WisBlockLoRaHwConfig.h"
#include "WisBlockLoRaBoards.h"

#if defined(ARDUINO_ARCH_NRF52) || defined(NRF52840_XXAA)
#include <Arduino.h> // NRF_SPIM1
#endif

namespace
{
/**
 * @brief Fill in the settings that are the same for all RAKwireless WisBlock cores
 *
 * Common to all four presets below: every WisBlock-family SX1262 module
 * this library has been tested against uses the SX1262's own DIO2 for RF
 * switching, a TCXO on DIO3 (not a crystal), the DC-DC regulator, and a
 * single RXEN pin doubling as antenna-switch power (no separate TXEN).
 *
 * FIX: the TCXO startup time used everywhere in this library before this
 * file existed (LoRaP2PEngine.cpp, wisblock_ral_sx126x_bsp.c) was encoded
 * directly as "50 << 6" (50ms in 15.625us ticks) but *commented* as "5ms" -
 * the tick value, not the stale comment, is what was actually programmed
 * into the chip on every real build so far. Reproduced here as 50000us to
 * keep behavior identical; only the misleading comment is corrected.
 *
 * @param cfg Configuration to fill in
 */
void applyCommonWisBlockDefaults(WisBlockLoRaHwConfig &cfg)
{
	cfg.chipType = WISBLOCK_LORA_CHIP_SX1262;
	cfg.useDio2AntSwitch = true;
	cfg.useDio3Tcxo = true;
	cfg.useDio3AntSwitch = false;
	cfg.useRxenAntPwr = true;
	cfg.useLdo = false;
	cfg.tcxoCtrlVoltage = SX126X_TCXO_CTRL_3_3V;
	cfg.tcxoStartupTimeUs = 50000; // 50ms - see note above
	cfg.spiHz = 8000000UL;
}
} // namespace

WisBlockLoRaHwConfig wisblockLoRaHwConfigRAK4631()
{
	WisBlockLoRaHwConfig cfg;
	applyCommonWisBlockDefaults(cfg);
#if defined(ARDUINO_ARCH_NRF52) || defined(NRF52840_XXAA)
	cfg.pinNss = LORA_SPI_NSS;
	cfg.pinReset = LORA_RESET;
	cfg.pinBusy = LORA_BUSY;
	cfg.pinDio1 = LORA_DIO1;
	cfg.pinSck = LORA_SPI_SCK;
	cfg.pinMosi = LORA_SPI_MOSI;
	cfg.pinMiso = LORA_SPI_MISO;
	cfg.radioRxEn = LORA_ANT_PWR;
#endif
	return cfg;
}

WisBlockLoRaHwConfig wisblockLoRaHwConfigRAK3312()
{
	WisBlockLoRaHwConfig cfg;
	applyCommonWisBlockDefaults(cfg);
#if defined(ARDUINO_ARCH_ESP32)
	cfg.pinNss = LORA_SPI_NSS;
	cfg.pinReset = LORA_RESET;
	cfg.pinBusy = LORA_BUSY;
	cfg.pinDio1 = LORA_DIO1;
	cfg.pinSck = LORA_SPI_SCK;
	cfg.pinMosi = LORA_SPI_MOSI;
	cfg.pinMiso = LORA_SPI_MISO;
	cfg.radioRxEn = LORA_ANT_PWR;
#endif
	return cfg;
}

WisBlockLoRaHwConfig wisblockLoRaHwConfigRAK11310()
{
	WisBlockLoRaHwConfig cfg;
	applyCommonWisBlockDefaults(cfg);
#if defined(ARDUINO_ARCH_RP2040)
	cfg.pinNss = LORA_SPI_NSS;
	cfg.pinReset = LORA_RESET;
	cfg.pinBusy = LORA_BUSY;
	cfg.pinDio1 = LORA_DIO1;
	cfg.pinSck = LORA_SPI_SCK;
	cfg.pinMosi = LORA_SPI_MOSI;
	cfg.pinMiso = LORA_SPI_MISO;
	cfg.radioRxEn = LORA_ANT_PWR;
	cfg.spiInstance = &SPI1; // the SX1262 is wired to SPI1 (GPIO10/11/12) on the RAK11300/RAK11310
#endif
	return cfg;
}

WisBlockLoRaHwConfig wisblockLoRaHwConfigRAK11300()
{
	return wisblockLoRaHwConfigRAK11310();
}

#if defined(ARDUINO_ARCH_NRF52) || defined(NRF52840_XXAA)
namespace
{
/**
 * @brief Second SPI peripheral used for the radio of the RAK3401
 *
 * RAK3400's default SPI0 is already committed elsewhere on the module, so
 * the RAK13300/RAK13302 transceiver is wired to a second SPIM peripheral
 * instead - pins are MISO=29, SCK=3, MOSI=30 (Adafruit nRF52 core's
 * SPIClass(NRF_SPIM_Type*, miso, sck, mosi) constructor order).
 */
SPIClass spiLoraRak3401(NRF_SPIM1, 29, 3, 30);
} // namespace

WisBlockLoRaHwConfig wisblockLoRaHwConfigRAK3401_RAK13300()
{
	WisBlockLoRaHwConfig cfg;
	cfg.chipType = WISBLOCK_LORA_CHIP_SX1262;
	cfg.pinReset = 4;
	cfg.pinNss = 26;
	cfg.pinSck = 3;
	cfg.pinMiso = 29;
	cfg.pinDio1 = 10;
	cfg.pinBusy = 9;
	cfg.pinMosi = 30;
	cfg.radioTxEn = -1;
	cfg.radioRxEn = 21;
	cfg.useDio2AntSwitch = true;
	cfg.useDio3Tcxo = true;
	cfg.useDio3AntSwitch = false;
	cfg.useRxenAntPwr = true;
	cfg.useLdo = false;
	cfg.tcxoCtrlVoltage = SX126X_TCXO_CTRL_3_3V;
	cfg.tcxoStartupTimeUs = 50000;
	cfg.spiHz = 8000000UL;
	cfg.spiInstance = &spiLoraRak3401;
	return cfg;
}

WisBlockLoRaHwConfig wisblockLoRaHwConfigRAK3401_RAK13702()
{
	// Same MCU-side wiring as the RAK13300 (SPI1 pins, NSS, RESET, BUSY, IRQ pin, antenna
	// switch power, TCXO 3.3 V / 50 ms), so start from that preset and change only what differs.
	WisBlockLoRaHwConfig cfg = wisblockLoRaHwConfigRAK3401_RAK13300();
	cfg.chipType = WISBLOCK_LORA_CHIP_LR2021;
	// The SX1262-only DIO2/DIO3 functions do not exist on the LR2021
	cfg.useDio2AntSwitch = false;
	cfg.useDio3Tcxo = false;
	cfg.useDio3AntSwitch = false;
	// Pin 21 (WB_IO3) powers both antenna switches, as on the RAK13300
	cfg.radioTxEn = -1;
	cfg.radioRxEn = 21;
	cfg.useRxenAntPwr = true;
	cfg.useLdo = false; // DC-DC
	// LR2021 chip DIOs: the IRQ (DIO8) is wired to the MCU pin that is DIO1 on the RAK13300
	cfg.lrIrqDio = 8;
	cfg.lrRfSwitchDioLf = 7; // RF_LF1 / RF_LF2 (sub-GHz) antenna switch
	cfg.lrRfSwitchDioHf = 9; // RFI_HF (2.4 GHz) antenna switch
	return cfg;
}
#endif
