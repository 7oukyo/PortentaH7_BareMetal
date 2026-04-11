/**
 * @file acs712.h
 * @brief ACS712 5A current sensor driver (analog output via ADC1).
 *
 * Sensor on breakout board Analog A1 = PA1_C -> ADC1_INP1.
 * Sensitivity: 185 mV/A.  Zero-current output: Vcc/2 (~2.5V at 5V supply).
 */

#ifndef ACS712_H
#define ACS712_H

#include "main.h"
#include <stdbool.h>

/* ACS712-05B sensitivity: 185 mV/A */
#define ACS712_SENSITIVITY_MV_PER_A  185.0f

/* ADC reference voltage (VREF+ measured at 3.1V on this board) */
#define ACS712_VREF_MV               3100.0f

/* ADC resolution: 16-bit -> 65535 counts */
#define ACS712_ADC_MAX               65535.0f

/* Calibration: number of samples to average for zero-current bias */
#define ACS712_CAL_SAMPLES           128

/* Expected zero-current bias: ACS712 outputs VCC/2 where VCC = 5V */
#define ACS712_EXPECTED_BIAS_MV      2500.0f

/* Bias validity window: expected ± this tolerance in mV */
#define ACS712_BIAS_TOLERANCE_MV     450.0f

/* Number of samples to average per current reading (noise reduction) */
#define ACS712_READ_SAMPLES          16

/* ADC channel for PA1_C (breakout ANALOG_A1) = ADC1_INP1.
 * PA1_C is a dedicated analog pin with direct ADC path (SYSCFG PA1SO=0).
 * PA0_C (A0) tested: reads ~50 mV regardless of input — suspected trace issue.
 * PC2 (A4) tested: no channel reads correctly. */
#define ACS712_ADC_CHANNEL           ADC_CHANNEL_1

/* Initialise ADC1 on PA0_C (INP0) and calibrate zero-current bias. */
void ACS712_Init(void);

/* Read current in milliamps. Returns 0.0 if sensor is in fault state. */
float ACS712_ReadCurrent_mA(void);

/* True if startup calibration detected a bias outside expected range. */
bool ACS712_IsFault(void);

/* Return the calibrated bias voltage in mV (for diagnostics). */
float ACS712_GetBias_mV(void);

/* Print raw ADC samples (16) with min/max/avg over USB VCP as [ADC_RAW] line. */
void ACS712_PrintRawSamples(void);

/* Scan multiple ADC channels and print mV readings (pin identification). */
void ACS712_ScanChannels(void);

/* Re-run bias calibration and report result over USB VCP. */
void ACS712_Recalibrate(void);

#endif /* ACS712_H */
