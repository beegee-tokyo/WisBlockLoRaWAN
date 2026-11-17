// /**
//  * @file battery.h
//  * @author Bernd Giesecke (bernd@giesecke.tk)
//  * @brief Reading the battery voltage of the WisBlock base boards (RAK4631, RAK3312, RAK11310)
//  * @version 0.2
//  * @date 2026-10-12
//  */
// #ifndef BATTERY_H
// #define BATTERY_H

// #include <Arduino.h>
// #include "battery_math.h"

// /** Where the device gets its power from, see batt_set_power_source() */
// typedef enum
// {
// 	/** nRF52840: USB connected = external power. RP2040 and ESP32-S3: cannot be detected, a battery is assumed */
// 	BATT_POWER_AUTO = 0,
// 	/** The application says: battery powered. The USB state is ignored */
// 	BATT_POWER_BATTERY = 1,
// 	/** The application says: permanently powered, no battery. The battery voltage is not used */
// 	BATT_POWER_EXTERNAL = 2
// } batt_power_source_t;

// /**
//  * @brief Initialize the battery analog input with the default ADC range of the board
//  *
//  * nRF52: 3000 mV, RP2040: 3300 mV, ESP32: calibrated millivolt readings (no range needed)
//  */
// void init_batt(void);

// /**
//  * @brief Initialize the battery analog input
//  *
//  * 12 bit resolution. nRF52840: the range selects the reference (3000 mV = AR_INTERNAL_3_0, 3600 mV =
//  * AR_INTERNAL), the acquisition time is set to 40 us for the 600 kOhm source resistance of the
//  * divider. RP2040: the range is the voltage of the ADC reference (the 3V3 supply, about 3300 mV),
//  * the conversion uses it. ESP32-S3: 11 dB attenuation, analogReadMilliVolts() returns calibrated
//  * values, the range is not used.
//  * If the application changes the ADC reference or range itself it must call this with the new range,
//  * the conversion is calculated with it.
//  *
//  * @param adc_range_mv Full scale range of the ADC in mV
//  * @return false if the range cannot measure a full battery (needs at least 2650 mV: 4.2 V * 0.6) or is
//  * not supported by the MCU. Nothing is changed then
//  */
// bool init_batt(uint16_t adc_range_mv);

// /**
//  * @brief Read the battery voltage
//  *
//  * The average of 16 conversions (the first one is thrown away, the divider has a high impedance).
//  * Measure while the radio is idle, the voltage drops during the transmission.
//  *
//  * @return float Battery voltage in mV, for example 3900.0
//  */
// float read_batt(void);

// /**
//  * @brief Estimate the battery charge in percentage from milli volts
//  *
//  * @param mvolts Battery voltage in mV
//  * @return uint8_t Battery charge as percentage (0 to 100)
//  */
// uint8_t mv_to_percent(float mvolts);

// /**
//  * @brief Battery level for the LoRaWAN DevStatusAns (LNS device status request)
//  *
//  * @return uint8_t 0 = external power (see batt_external_power()), 1 to 254 = battery level, 255 = the
//  * voltage is not that of a battery (not connected, or the output of the charger). Never 0 for a battery.
//  */
// uint8_t get_lora_batt(void);

// /**
//  * @brief Tell the library where the power comes from
//  *
//  * With USB or another power supply and no battery the voltage at the measurement input is the output
//  * of the battery charger (TP4054), it can be any value. Only the nRF52840 can detect USB power
//  * itself. A device that is permanently powered must call this with BATT_POWER_EXTERNAL.
//  *
//  * @param source Power source
//  */
// void batt_set_power_source(batt_power_source_t source);

// /**
//  * @brief Get the power source setting
//  *
//  * @return batt_power_source_t Setting of batt_set_power_source(), BATT_POWER_AUTO by default
//  */
// batt_power_source_t batt_get_power_source(void);

// /**
//  * @brief Check if the device has external power
//  *
//  * @return true BATT_POWER_EXTERNAL, or BATT_POWER_AUTO on an nRF52840 with USB connected
//  */
// bool batt_external_power(void);

// /**
//  * @brief Adjust the conversion to a measured battery voltage
//  *
//  * Reads the battery voltage and sets the trim factor, so that read_batt() returns the measured
//  * value. Connect a battery, measure it with a multimeter and call this once, then write the printed
//  * factor into batt_set_trim() or into BATT_TRIM_DEFAULT in battery.cpp. 1% resistors alone are
//  * already +-0.8%, and the ADC input loads the divider.
//  *
//  * @param measured_mv Battery voltage measured with a multimeter in mV
//  * @return float The new trim factor. Not changed (and returned) if it would be outside 0.8 to 1.3
//  */
// float batt_calibrate(uint16_t measured_mv);

// /**
//  * @brief Set the trim factor
//  *
//  * @param trim Factor for the conversion, 1.0 = ideal divider and ADC
//  */
// void batt_set_trim(float trim);

// /**
//  * @brief Get the trim factor
//  *
//  * @return float Factor of the conversion
//  */
// float batt_get_trim(void);

// /**
//  * @brief Get the ADC range that is used for the conversion
//  *
//  * @return uint16_t Range in mV
//  */
// uint16_t batt_get_adc_range_mv(void);

// #endif // BATTERY_H
/**
 * @file battery.h
 * @author Bernd Giesecke (bernd@giesecke.tk)
 * @brief Reading the battery voltage of the WisBlock base boards (RAK4631, RAK3312, RAK11310)
 * @version 0.2
 * @date 2026-10-12
 */
#ifndef BATTERY_H
#define BATTERY_H

#include <Arduino.h>
#include "battery_math.h"

/** Where the device gets its power from, see batt_set_power_source() */
typedef enum
{
	/** nRF52840: USB connected = external power. RP2040 and ESP32-S3: cannot be detected, a battery is assumed */
	BATT_POWER_AUTO = 0,
	/** The application says: battery powered. The USB state is ignored */
	BATT_POWER_BATTERY = 1,
	/** The application says: permanently powered, no battery. The battery voltage is not used */
	BATT_POWER_EXTERNAL = 2
} batt_power_source_t;

/**
 * @brief Initialize the battery analog input with the default ADC range of the board
 *
 * Loads a saved trim factor (batt_set_trim()). Call it after the library was started.
 *
 * nRF52: 3000 mV, RP2040: 3300 mV, ESP32: calibrated millivolt readings (no range needed)
 */
void init_batt(void);

/**
 * @brief Initialize the battery analog input
 *
 * 12 bit resolution. nRF52840: the range selects the reference (3000 mV = AR_INTERNAL_3_0, 3600 mV =
 * AR_INTERNAL), the acquisition time is set to 40 us for the 600 kOhm source resistance of the
 * divider. RP2040: the range is the voltage of the ADC reference (the 3V3 supply, about 3300 mV),
 * the conversion uses it. ESP32-S3: 11 dB attenuation, analogReadMilliVolts() returns calibrated
 * values, the range is not used.
 * If the application changes the ADC reference or range itself it must call this with the new range,
 * the conversion is calculated with it.
 *
 * @param adc_range_mv Full scale range of the ADC in mV
 * @return false if the range cannot measure a full battery (needs at least 2650 mV: 4.2 V * 0.6) or is
 * not supported by the MCU. Nothing is changed then
 */
bool init_batt(uint16_t adc_range_mv);

/**
 * @brief Read the battery voltage
 *
 * The average of 16 conversions (the first one is thrown away, the divider has a high impedance).
 * Measure while the radio is idle, the voltage drops during the transmission.
 *
 * @return float Battery voltage in mV, for example 3900.0
 */
float read_batt(void);

/**
 * @brief Estimate the battery charge in percentage from milli volts
 *
 * @param mvolts Battery voltage in mV
 * @return uint8_t Battery charge as percentage (0 to 100)
 */
uint8_t mv_to_percent(float mvolts);

/**
 * @brief Battery level for the LoRaWAN DevStatusAns (LNS device status request)
 *
 * @return uint8_t 0 = external power (see batt_external_power()), 1 to 254 = battery level, 255 = the
 * voltage is not that of a battery (not connected, or the output of the charger). Never 0 for a battery.
 */
uint8_t get_lora_batt(void);

/**
 * @brief Tell the library where the power comes from
 *
 * With USB or another power supply and no battery the voltage at the measurement input is the output
 * of the battery charger (TP4054), it can be any value. Only the nRF52840 can detect USB power
 * itself. A device that is permanently powered must call this with BATT_POWER_EXTERNAL.
 *
 * @param source Power source
 */
void batt_set_power_source(batt_power_source_t source);

/**
 * @brief Get the power source setting
 *
 * @return batt_power_source_t Setting of batt_set_power_source(), BATT_POWER_AUTO by default
 */
batt_power_source_t batt_get_power_source(void);

/**
 * @brief Check if the device has external power
 *
 * @return true BATT_POWER_EXTERNAL, or BATT_POWER_AUTO on an nRF52840 with USB connected
 */
bool batt_external_power(void);

/**
 * @brief Battery voltage without the trim factor
 *
 * Use it to find the trim factor: measure the battery voltage at the battery connector with a
 * multimeter while the device is powered from the battery only (the output of the charger changes
 * the voltage when USB is connected), read this value at the same time (for example send it in an
 * uplink or print it) and calculate trim = measured / this value. The factor can then be set over
 * any channel, also USB or a downlink, with batt_set_trim(): setting it does not measure anything.
 *
 * @return float Battery voltage in mV, with the ideal divider and no correction
 */
float batt_read_untrimmed_mv(void);

/**
 * @brief Adjust the trim factor so that read_batt() returns a voltage that was measured before
 *
 * Use it when the measured voltage is available at the moment of the measurement: for a calibration
 * on the bench, or from a downlink (a technician measures the battery with a multimeter and sends
 * the value). It is not a good idea to start it with a command over USB, the charger output changes
 * the voltage then. The new trim factor is saved (not by default if save is false).
 *
 * @param measured_mv Battery voltage measured with a multimeter at the same time, in mV
 * @param save Save the new trim factor in the flash, it is loaded by init_batt() after a restart
 * @return float The new trim factor. Not changed (and returned) if it would be outside 0.8 to 1.3
 */
float batt_calibrate(uint16_t measured_mv, bool save = true);

/**
 * @brief Set the trim factor
 *
 * No measurement is made, so it can be called from any source: a calibration table, USB, a
 * downlink. By default the factor is saved in the flash and used again after a restart.
 *
 * @param trim Factor for the conversion, 1.0 = ideal divider and ADC. Must be between 0.8 and 1.3
 * @param save Save the factor in the flash
 * @return false if the factor is not valid or could not be saved (it is used until the next restart then)
 */
bool batt_set_trim(float trim, bool save = true);

/**
 * @brief Remove the saved trim factor and go back to the default of the board (BATT_TRIM_DEFAULT)
 */
void batt_reset_trim(void);

/**
 * @brief Get the trim factor
 *
 * @return float Factor of the conversion
 */
float batt_get_trim(void);

/**
 * @brief Get the ADC range that is used for the conversion
 *
 * @return uint16_t Range in mV
 */
uint16_t batt_get_adc_range_mv(void);

#endif // BATTERY_H