// /**
//  * @file battery_math.h
//  * @author Bernd Giesecke (bernd@giesecke.tk)
//  * @brief Conversion functions for the battery measurement, no hardware access
//  * @version 0.2
//  * @date 2026-10-12
//  *
//  * The WisBlock base boards (RAK4631: RAK19007, RAK19010, ...) measure the battery with a divider:
//  *
//  *   VBAT ---- 1.0 MOhm ----+---- ADC input (WB_A0)
//  *                          |
//  *                       1.5 MOhm
//  *                          |
//  *                         GND
//  *
//  * The ADC sees 0.6 * VBAT, so a 4.2 V battery gives 2.52 V at the ADC input.
//  */
// #ifndef BATTERY_MATH_H
// #define BATTERY_MATH_H

// #include <stdint.h>

// /** Upper resistor of the divider (VBAT to ADC input) in Ohm, 1% */
// #define BATT_R_TOP_OHM 1000000.0f
// /** Lower resistor of the divider (ADC input to GND) in Ohm, 1% */
// #define BATT_R_BOTTOM_OHM 1500000.0f
// /** Voltage at the ADC input divided by the battery voltage, 0.6 (+-0.8% with 1% resistors) */
// #define BATT_DIVIDER_RATIO (BATT_R_BOTTOM_OHM / (BATT_R_TOP_OHM + BATT_R_BOTTOM_OHM))
// /** Resolution of the ADC measurement in bits */
// #define BATT_ADC_BITS 12
// /** Highest battery voltage that has to be measured: 4.2 V charge voltage and some tolerance, in mV */
// #define BATT_MAX_MV 4300
// /** Readings below this are no LiPo/Li-Ion cell (no battery, wrong pin), in mV */
// #define BATT_MIN_VALID_MV 2500
// /** Readings above this are not a battery either (the output of the charger without a battery can be anything), in mV */
// #define BATT_MAX_VALID_MV 4500

// /**
//  * @brief Lowest ADC range that can measure a full battery
//  *
//  * @return ADC full scale range in mV: BATT_MAX_MV * divider ratio, rounded up (2580 mV, 2650 mV with the
//  * 1% resistor tolerance included)
//  */
// static inline uint16_t batt_min_adc_range_mv(void)
// {
// 	return (uint16_t)(BATT_MAX_MV * BATT_DIVIDER_RATIO * 1.027f + 0.5f);
// }

// /**
//  * @brief Convert a raw ADC value into millivolts at the ADC input
//  *
//  * @param raw Raw value, 0 to 2^BATT_ADC_BITS - 1 (can be an average)
//  * @param adc_range_mv Full scale range of the ADC in mV, as configured by the application (3000 for
//  * AR_INTERNAL_3_0 on nRF52, about 3300 for the 3V3 reference of the RP2040)
//  * @return float Voltage at the ADC input in mV
//  */
// static inline float batt_raw_to_pin_mv(float raw, uint16_t adc_range_mv)
// {
// 	return raw * (float)adc_range_mv / (float)(1UL << BATT_ADC_BITS);
// }

// /**
//  * @brief Convert the voltage at the ADC input into the battery voltage
//  *
//  * @param pin_mv Voltage at the ADC input in mV
//  * @param trim Calibration factor, 1.0 = the ideal divider. Corrects the divider tolerance, the load
//  * of the divider by the ADC input (the source resistance is 600 kOhm) and the reference tolerance
//  * @return float Battery voltage in mV
//  */
// static inline float batt_pin_mv_to_batt_mv(float pin_mv, float trim)
// {
// 	return pin_mv / BATT_DIVIDER_RATIO * trim;
// }

// /**
//  * @brief Estimate the charge of a Li-Ion/LiPo cell in percent from its voltage
//  *
//  * Generic resting voltage curve of a 3.7 V cell with linear interpolation between the points. It is
//  * not measured on your cell: replace the table with the curve of your battery if you need better
//  * values. The voltage drops while the radio transmits, measure when the device is idle.
//  *
//  * @param mv Battery voltage in mV
//  * @return uint8_t Charge in percent, 0 to 100
//  */
// static inline uint8_t batt_mv_to_percent(float mv)
// {
// 	static const uint16_t pts_mv[] = {4200, 4150, 4110, 4080, 4020, 3980, 3950, 3910, 3870, 3850, 3840,
// 									  3820, 3800, 3790, 3770, 3750, 3730, 3710, 3690, 3610, 3270};
// 	static const uint8_t pts_pct[] = {100, 95, 90, 85, 80, 75, 70, 65, 60, 55, 50, 45, 40, 35, 30, 25, 20, 15, 10, 5, 0};
// 	const int n = (int)(sizeof(pts_mv) / sizeof(pts_mv[0]));

// 	if (mv >= (float)pts_mv[0])
// 	{
// 		return 100;
// 	}
// 	if (mv <= (float)pts_mv[n - 1])
// 	{
// 		return 0;
// 	}
// 	for (int i = 0; i < n - 1; i++)
// 	{
// 		if (mv >= (float)pts_mv[i + 1])
// 		{
// 			float span = (float)(pts_mv[i] - pts_mv[i + 1]);
// 			float part = (mv - (float)pts_mv[i + 1]) / span;
// 			return (uint8_t)((float)pts_pct[i + 1] + part * (float)(pts_pct[i] - pts_pct[i + 1]) + 0.5f);
// 		}
// 	}
// 	return 0;
// }

// /**
//  * @brief Convert the charge in percent into the battery level of the LoRaWAN DevStatusAns
//  *
//  * Same as wisblockBatteryLevelFromPercent() of the library: 1 (empty) to 254 (full). 0 is reserved for
//  * "external power" and 255 for "unknown", so this never returns them.
//  *
//  * @param percent Charge in percent, 0 to 100
//  * @return uint8_t Battery level 1 to 254
//  */
// static inline uint8_t batt_percent_to_lora(uint8_t percent)
// {
// 	if (percent > 100)
// 	{
// 		percent = 100;
// 	}
// 	return (uint8_t)(1 + ((uint16_t)percent * 253) / 100);
// }

// #endif // BATTERY_MATH_H
/**
 * @file battery_math.h
 * @author Bernd Giesecke (bernd@giesecke.tk)
 * @brief Conversion functions for the battery measurement, no hardware access
 * @version 0.2
 * @date 2026-10-12
 *
 * The WisBlock base boards (RAK4631: RAK19007, RAK19010, ...) measure the battery with a divider:
 *
 *   VBAT ---- 1.0 MOhm ----+---- ADC input (WB_A0)
 *                          |
 *                       1.5 MOhm
 *                          |
 *                         GND
 *
 * The ADC sees 0.6 * VBAT, so a 4.2 V battery gives 2.52 V at the ADC input.
 */
#ifndef BATTERY_MATH_H
#define BATTERY_MATH_H

#include <stdint.h>
#include <stdbool.h>

/** Upper resistor of the divider (VBAT to ADC input) in Ohm, 1% */
#define BATT_R_TOP_OHM 1000000.0f
/** Lower resistor of the divider (ADC input to GND) in Ohm, 1% */
#define BATT_R_BOTTOM_OHM 1500000.0f
/** Voltage at the ADC input divided by the battery voltage, 0.6 (+-0.8% with 1% resistors) */
#define BATT_DIVIDER_RATIO (BATT_R_BOTTOM_OHM / (BATT_R_TOP_OHM + BATT_R_BOTTOM_OHM))
/** Resolution of the ADC measurement in bits */
#define BATT_ADC_BITS 12
/** Highest battery voltage that has to be measured: 4.2 V charge voltage and some tolerance, in mV */
#define BATT_MAX_MV 4300
/** Readings below this are no LiPo/Li-Ion cell (no battery, wrong pin), in mV */
#define BATT_MIN_VALID_MV 2500
/** Readings above this are not a battery either (the output of the charger without a battery can be anything), in mV */
#define BATT_MAX_VALID_MV 4500

/** Lowest trim factor that is accepted (a correction of more than 20% means a wrong pin or no battery) */
#define BATT_TRIM_MIN 0.8f
/** Highest trim factor that is accepted */
#define BATT_TRIM_MAX 1.3f

/**
 * @brief Check if a trim factor is plausible
 *
 * @param trim Trim factor
 * @return true if it is between BATT_TRIM_MIN and BATT_TRIM_MAX
 */
static inline bool batt_trim_valid(float trim)
{
	return trim >= BATT_TRIM_MIN && trim <= BATT_TRIM_MAX;
}

/**
 * @brief Lowest ADC range that can measure a full battery
 *
 * @return ADC full scale range in mV: BATT_MAX_MV * divider ratio, rounded up (2580 mV, 2650 mV with the
 * 1% resistor tolerance included)
 */
static inline uint16_t batt_min_adc_range_mv(void)
{
	return (uint16_t)(BATT_MAX_MV * BATT_DIVIDER_RATIO * 1.027f + 0.5f);
}

/**
 * @brief Convert a raw ADC value into millivolts at the ADC input
 *
 * @param raw Raw value, 0 to 2^BATT_ADC_BITS - 1 (can be an average)
 * @param adc_range_mv Full scale range of the ADC in mV, as configured by the application (3000 for
 * AR_INTERNAL_3_0 on nRF52, about 3300 for the 3V3 reference of the RP2040)
 * @return float Voltage at the ADC input in mV
 */
static inline float batt_raw_to_pin_mv(float raw, uint16_t adc_range_mv)
{
	return raw * (float)adc_range_mv / (float)(1UL << BATT_ADC_BITS);
}

/**
 * @brief Convert the voltage at the ADC input into the battery voltage
 *
 * @param pin_mv Voltage at the ADC input in mV
 * @param trim Calibration factor, 1.0 = the ideal divider. Corrects the divider tolerance, the load
 * of the divider by the ADC input (the source resistance is 600 kOhm) and the reference tolerance
 * @return float Battery voltage in mV
 */
static inline float batt_pin_mv_to_batt_mv(float pin_mv, float trim)
{
	return pin_mv / BATT_DIVIDER_RATIO * trim;
}

/**
 * @brief Estimate the charge of a Li-Ion/LiPo cell in percent from its voltage
 *
 * Generic resting voltage curve of a 3.7 V cell with linear interpolation between the points. It is
 * not measured on your cell: replace the table with the curve of your battery if you need better
 * values. The voltage drops while the radio transmits, measure when the device is idle.
 *
 * @param mv Battery voltage in mV
 * @return uint8_t Charge in percent, 0 to 100
 */
static inline uint8_t batt_mv_to_percent(float mv)
{
	static const uint16_t pts_mv[] = {4200, 4150, 4110, 4080, 4020, 3980, 3950, 3910, 3870, 3850, 3840,
									  3820, 3800, 3790, 3770, 3750, 3730, 3710, 3690, 3610, 3270};
	static const uint8_t pts_pct[] = {100, 95, 90, 85, 80, 75, 70, 65, 60, 55, 50, 45, 40, 35, 30, 25, 20, 15, 10, 5, 0};
	const int n = (int)(sizeof(pts_mv) / sizeof(pts_mv[0]));

	if (mv >= (float)pts_mv[0])
	{
		return 100;
	}
	if (mv <= (float)pts_mv[n - 1])
	{
		return 0;
	}
	for (int i = 0; i < n - 1; i++)
	{
		if (mv >= (float)pts_mv[i + 1])
		{
			float span = (float)(pts_mv[i] - pts_mv[i + 1]);
			float part = (mv - (float)pts_mv[i + 1]) / span;
			return (uint8_t)((float)pts_pct[i + 1] + part * (float)(pts_pct[i] - pts_pct[i + 1]) + 0.5f);
		}
	}
	return 0;
}

/**
 * @brief Convert the charge in percent into the battery level of the LoRaWAN DevStatusAns
 *
 * Same as wisblockBatteryLevelFromPercent() of the library: 1 (empty) to 254 (full). 0 is reserved for
 * "external power" and 255 for "unknown", so this never returns them.
 *
 * @param percent Charge in percent, 0 to 100
 * @return uint8_t Battery level 1 to 254
 */
static inline uint8_t batt_percent_to_lora(uint8_t percent)
{
	if (percent > 100)
	{
		percent = 100;
	}
	return (uint8_t)(1 + ((uint16_t)percent * 253) / 100);
}

#endif // BATTERY_MATH_H