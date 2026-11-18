/**
 * @file battery.cpp
 * @author Bernd Giesecke (bernd@giesecke.tk)
 * @brief Reading the battery voltage of the WisBlock base boards (RAK4631, RAK3312, RAK11310)
 * @version 0.2
 * @date 2026-10-12
 *
 * @copyright Copyright (c) 2026
 *
 */
#include <string.h>
#include "main.h"
#include "battery.h"
#include <WisBlockLoRaWAN.h>   // WISBLOCK_BATTERY_EXTERNAL_POWER, WISBLOCK_BATTERY_UNKNOWN
#include <WisBlockLoRaFlash.h> // saved trim factor

// Analog input of the battery voltage divider. The cores define WB_A0 in different ways (RAK4631:
// "static const uint8_t WB_A0 = 5;", most others: "#define WB_A0 ..."), so it cannot be tested with
// #ifdef. A board that needs another pin (or has no WB_A0) can define BATT_ADC_PIN, for example
// with -DBATT_ADC_PIN=1 in the build flags or in main.h.
#if !defined BATT_ADC_PIN
#if defined ARDUINO_ARCH_RP2040 && !defined WB_A0
#define WB_A0 26 // the Arduino-Pico variant of the RAK11300 has no WB_A0: GPIO26 = ADC0
#endif
#define BATT_ADC_PIN WB_A0
#endif

#if defined ARDUINO_ARCH_NRF52 || defined ARDUINO_ARCH_NRF52
/** Default ADC range: AR_INTERNAL_3_0 = 0.6 V reference * 5 */
#define BATT_ADC_RANGE_DEFAULT_MV 3000
/** Acquisition time of the SAADC in us, 40 us is the longest setting of the core (3, 5, 10, 15, 20, 40). The
 * divider has a source resistance of 1M || 1.5M = 600 kOhm. See the SAADC chapter of the nRF52840
 * product specification (acquisition time over source resistance): short times read too low with a
 * high source resistance - the old value of 10 us was probably part of the 1.73 compensation factor */
#define BATT_SAMPLE_TIME_US 40
/** Trim factor of the conversion, 1.0 = ideal. Measure and adjust with batt_calibrate() */
#define BATT_TRIM_DEFAULT 1.0f
#elif defined ARDUINO_ARCH_ESP32
/** Not used: analogReadMilliVolts() returns calibrated millivolts at the pin */
#define BATT_ADC_RANGE_DEFAULT_MV 0
/** Trim factor of the conversion, 1.0 = ideal. Measure and adjust with batt_calibrate() */
#define BATT_TRIM_DEFAULT 1.0f
#elif defined ARDUINO_ARCH_RP2040
/** Default ADC range: the reference of the RP2040 ADC is the 3V3 supply */
#define BATT_ADC_RANGE_DEFAULT_MV 3300
/** Trim factor of the conversion. 1.846 (value of the RAK example) * 0.6 = 1.1076, the ADC input
 * loads the divider. Measure and adjust with batt_calibrate() */
#define BATT_TRIM_DEFAULT 1.1076f
#else
#error "battery.cpp: unsupported MCU"
#endif

/** Number of conversions that are averaged */
#define BATT_SAMPLES 16

/** Analog input for battery level */
uint32_t vbat_pin = BATT_ADC_PIN;

/** Flash key of the saved trim factor */
#define BATT_TRIM_KEY "batt_trim"
/** Marks a valid saved trim factor */
#define BATT_TRIM_MAGIC 0x42415454UL

/** Saved trim factor: the factor, protected by a magic number and a check value */
struct batt_trim_store_t
{
	uint32_t magic;
	uint32_t trim_bits;
	uint32_t check;
};

/** ADC range in mV that is used for the conversion */
static uint16_t batt_adc_range_mv = BATT_ADC_RANGE_DEFAULT_MV;
/** Calibration factor of the conversion */
static float batt_trim = BATT_TRIM_DEFAULT;
/** Power source set by the application */
static batt_power_source_t batt_power_source = BATT_POWER_AUTO;

/**
 * @brief Load the saved trim factor
 *
 * @return true if a valid factor was found and is used now
 */
static bool batt_load_trim(void)
{
	batt_trim_store_t store;
	if (!WisBlockLoRaFlash::init() || !WisBlockLoRaFlash::read(BATT_TRIM_KEY, (uint8_t *)&store, sizeof(store)))
	{
		return false;
	}
	if (store.magic != BATT_TRIM_MAGIC || store.check != (store.magic ^ store.trim_bits ^ 0xA5A5A5A5UL))
	{
		return false;
	}
	float trim;
	memcpy(&trim, &store.trim_bits, sizeof(trim));
	if (!batt_trim_valid(trim))
	{
		return false;
	}
	batt_trim = trim;
	return true;
}

bool init_batt(uint16_t adc_range_mv)
{
	// A trim factor that was set or measured before is used again after a restart
	(void)batt_load_trim();

#if !defined ARDUINO_ARCH_ESP32
	// A full battery (4.2 V) gives 2.52 V at the ADC input
	if (adc_range_mv < batt_min_adc_range_mv())
	{
		return false;
	}
#endif

#if defined ARDUINO_ARCH_NRF52 || defined NRF52_SERIES
	switch (adc_range_mv)
	{
	case 3000:
		analogReference(AR_INTERNAL_3_0); // 0.6 V * 5 (default of the old code)
		break;
	case 3600:
		analogReference(AR_INTERNAL); // 0.6 V * 6 (default of the core)
		break;
	default:
		return false; // the other references are too low or not a fixed range
	}
	analogSampleTime(BATT_SAMPLE_TIME_US);
#endif
#if defined ARDUINO_ARCH_ESP32
	// 11 dB: about 0 to 3.1 V, enough for 2.52 V
	analogSetPinAttenuation(vbat_pin, ADC_11db);
#endif

	// Set the resolution to 12-bit (0..4095)
	analogReadResolution(BATT_ADC_BITS);

	batt_adc_range_mv = adc_range_mv;
	return true;
}

void init_batt(void)
{
	(void)init_batt((uint16_t)BATT_ADC_RANGE_DEFAULT_MV);
}

/**
 * @brief Read the voltage at the analog input
 *
 * The first conversion is thrown away (the divider has a high impedance, the sampling capacitor of
 * the ADC needs a conversion to settle), then BATT_SAMPLES conversions are averaged.
 *
 * @return float Voltage at the ADC input in mV
 */
static float read_pin_mv(void)
{
#if defined ARDUINO_ARCH_ESP32
	(void)analogReadMilliVolts(vbat_pin);
	uint32_t sum = 0;
	for (int i = 0; i < BATT_SAMPLES; i++)
	{
		sum += analogReadMilliVolts(vbat_pin);
	}
	return (float)sum / (float)BATT_SAMPLES;
#else
	(void)analogRead(vbat_pin);
	uint32_t sum = 0;
	for (int i = 0; i < BATT_SAMPLES; i++)
	{
		sum += analogRead(vbat_pin);
	}
	return batt_raw_to_pin_mv((float)sum / (float)BATT_SAMPLES, batt_adc_range_mv);
#endif
}

float read_batt(void)
{
	return batt_pin_mv_to_batt_mv(read_pin_mv(), batt_trim);
}

uint8_t mv_to_percent(float mvolts)
{
	return batt_mv_to_percent(mvolts);
}

void batt_set_power_source(batt_power_source_t source)
{
	batt_power_source = source;
}

batt_power_source_t batt_get_power_source(void)
{
	return batt_power_source;
}

bool batt_external_power(void)
{
	if (batt_power_source == BATT_POWER_EXTERNAL)
	{
		return true;
	}
	if (batt_power_source == BATT_POWER_BATTERY)
	{
		return false;
	}
#if defined ARDUINO_ARCH_NRF52 || defined NRF52_SERIES
	// The nRF52840 senses VBUS itself
	nrfx_power_usb_state_t usb_status = nrfx_power_usbstatus_get();
	return (usb_status == NRFX_POWER_USB_STATE_CONNECTED) || (usb_status == NRFX_POWER_USB_STATE_READY);
#else
	// RP2040 and ESP32-S3: the base boards have no VBUS sense, the application has to set BATT_POWER_EXTERNAL
	return false;
#endif
}

uint8_t get_lora_batt(void)
{
	if (batt_external_power())
	{
		return WISBLOCK_BATTERY_EXTERNAL_POWER;
	}
	float mv = read_batt();
	if (mv < BATT_MIN_VALID_MV || mv > BATT_MAX_VALID_MV)
	{
		// No battery, or the output of the charger: do not report a made up level
		return WISBLOCK_BATTERY_UNKNOWN;
	}
	return batt_percent_to_lora(batt_mv_to_percent(mv));
}

float batt_read_untrimmed_mv(void)
{
	return batt_pin_mv_to_batt_mv(read_pin_mv(), 1.0f);
}

float batt_calibrate(uint16_t measured_mv, bool save)
{
	float untrimmed = batt_read_untrimmed_mv();
	if (untrimmed < 1.0f || measured_mv == 0)
	{
		return batt_trim;
	}
	float trim = (float)measured_mv / untrimmed;
	if (!batt_trim_valid(trim))
	{
		return batt_trim; // not plausible: wrong pin, no battery or a wrong measurement
	}
	(void)batt_set_trim(trim, save);
	return batt_trim;
}

bool batt_set_trim(float trim, bool save)
{
	if (!batt_trim_valid(trim))
	{
		return false;
	}
	batt_trim = trim;
	if (!save)
	{
		return true;
	}
	batt_trim_store_t store;
	store.magic = BATT_TRIM_MAGIC;
	memcpy(&store.trim_bits, &trim, sizeof(trim));
	store.check = store.magic ^ store.trim_bits ^ 0xA5A5A5A5UL;
	return WisBlockLoRaFlash::init() && WisBlockLoRaFlash::write(BATT_TRIM_KEY, (const uint8_t *)&store, sizeof(store));
}

void batt_reset_trim(void)
{
	batt_trim = BATT_TRIM_DEFAULT;
	if (WisBlockLoRaFlash::init())
	{
		(void)WisBlockLoRaFlash::erase(BATT_TRIM_KEY);
	}
}

float batt_get_trim(void)
{
	return batt_trim;
}

uint16_t batt_get_adc_range_mv(void)
{
	return batt_adc_range_mv;
}