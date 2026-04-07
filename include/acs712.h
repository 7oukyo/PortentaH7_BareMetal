/**
 * @file acs712.h
 * @brief ACS712 5A current sensor driver (analog output via ADC1).
 *
 * Sensor on breakout board Analog A0 = PA0_C -> ADC1_INP0.
 * Sensitivity: 185 mV/A.  Zero-current output: Vcc/2.
 */

#ifndef ACS712_H
#define ACS712_H

#include "main.h"
#include <stdbool.h>

/* ACS712-05B sensitivity: 185 mV/A */
#define ACS712_SENSITIVITY_MV_PER_A  185.0f

/* ADC reference voltage (VREF+ = 3.3V on Portenta H7) */
#define ACS712_VREF_MV               3300.0f

/* ADC resolution: 16-bit -> 65535 counts */
#define ACS712_ADC_MAX               65535.0f

/* Calibration: number of samples to average for bias */
#define ACS712_CAL_SAMPLES           64

/* Bias validity window: Vcc/2 ± this tolerance in mV */
#define ACS712_BIAS_TOLERANCE_MV     450.0f

/* Initialise ADC1 channel 0 (PA0_C) and calibrate zero-current bias. */
void ACS712_Init(void);

/* Read current in milliamps. Returns 0.0 if sensor is in fault state. */
float ACS712_ReadCurrent_mA(void);

/* True if startup calibration detected a bias outside expected range. */
bool ACS712_IsFault(void);

/* Return the calibrated bias voltage in mV (for diagnostics). */
float ACS712_GetBias_mV(void);

#endif /* ACS712_H */
