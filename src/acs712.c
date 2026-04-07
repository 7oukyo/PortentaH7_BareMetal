/**
 * @file acs712.c
 * @brief ACS712 5A current sensor driver.
 *
 * ADC1 channel INP0 on PA0_C (breakout Analog A0).
 * PA0_C is a dedicated analog pin — no conflict with PA0 (UART4_TX)
 * as long as SYSCFG_PMCR.PA0SO = 0 (default).
 *
 * Startup calibration: averages 64 samples as zero-current bias,
 * validates bias is within Vcc/2 ± tolerance.
 */

#include "acs712.h"

static ADC_HandleTypeDef hadc1;

static float    bias_mv   = 0.0f;   /* calibrated zero-current voltage */
static bool     fault     = false;  /* true if bias out of expected range */

/* Convert raw ADC count to millivolts. */
static float adc_to_mv(uint32_t raw)
{
    return ((float)raw / ACS712_ADC_MAX) * ACS712_VREF_MV;
}

/* Perform a single blocking ADC conversion and return raw value. */
static uint32_t adc_read_raw(void)
{
    HAL_ADC_Start(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, 10);
    return HAL_ADC_GetValue(&hadc1);
}

/* ---- ADC MSP (called by HAL_ADC_Init) ---- */

void HAL_ADC_MspInit(ADC_HandleTypeDef *hadc)
{
    if (hadc->Instance != ADC1) return;

    __HAL_RCC_ADC12_CLK_ENABLE();

    /* PA0_C is a dedicated analog pin with direct ADC path.
     * No GPIO init needed — the pin is analog-only when SYSCFG_PMCR.PA0SO = 0
     * (power-on default). We explicitly clear it to be safe. */
    HAL_SYSCFG_AnalogSwitchConfig(SYSCFG_SWITCH_PA0, SYSCFG_SWITCH_PA0_OPEN);
}

void HAL_ADC_MspDeInit(ADC_HandleTypeDef *hadc)
{
    if (hadc->Instance != ADC1) return;
    __HAL_RCC_ADC12_CLK_DISABLE();
}

/* ---- Public API ---- */

void ACS712_Init(void)
{
    /* ADC1 configuration: single channel, software trigger, 16-bit */
    hadc1.Instance                      = ADC1;
    hadc1.Init.ClockPrescaler           = ADC_CLOCK_ASYNC_DIV6;
    hadc1.Init.Resolution               = ADC_RESOLUTION_16B;
    hadc1.Init.ScanConvMode             = ADC_SCAN_DISABLE;
    hadc1.Init.EOCSelection             = ADC_EOC_SINGLE_CONV;
    hadc1.Init.LowPowerAutoWait         = DISABLE;
    hadc1.Init.ContinuousConvMode       = DISABLE;
    hadc1.Init.NbrOfConversion          = 1;
    hadc1.Init.DiscontinuousConvMode    = DISABLE;
    hadc1.Init.ExternalTrigConv         = ADC_SOFTWARE_START;
    hadc1.Init.ExternalTrigConvEdge     = ADC_EXTERNALTRIGCONVEDGE_NONE;
    hadc1.Init.ConversionDataManagement = ADC_CONVERSIONDATA_DR;
    hadc1.Init.Overrun                  = ADC_OVR_DATA_OVERWRITTEN;
    hadc1.Init.OversamplingMode         = DISABLE;

    if (HAL_ADC_Init(&hadc1) != HAL_OK) {
        Error_Handler();
    }

    /* Run ADC self-calibration (single-ended, offset calibration) */
    if (HAL_ADCEx_Calibration_Start(&hadc1, ADC_CALIB_OFFSET, ADC_SINGLE_ENDED) != HAL_OK) {
        Error_Handler();
    }

    /* Configure channel 0 (PA0_C = ADC1_INP0) */
    ADC_ChannelConfTypeDef ch_cfg = {0};
    ch_cfg.Channel      = ADC_CHANNEL_0;
    ch_cfg.Rank         = ADC_REGULAR_RANK_1;
    ch_cfg.SamplingTime = ADC_SAMPLETIME_387CYCLES_5;  /* long sample for stable analog */
    ch_cfg.SingleDiff   = ADC_SINGLE_ENDED;
    ch_cfg.OffsetNumber = ADC_OFFSET_NONE;

    if (HAL_ADC_ConfigChannel(&hadc1, &ch_cfg) != HAL_OK) {
        Error_Handler();
    }

    /* --- Zero-current bias calibration ---
     * Average N samples with no load. Expect ~Vcc/2 (1650 mV for 3.3V). */
    uint32_t sum = 0;
    for (uint32_t i = 0; i < ACS712_CAL_SAMPLES; i++) {
        sum += adc_read_raw();
    }
    bias_mv = adc_to_mv(sum / ACS712_CAL_SAMPLES);

    /* Sanity check: bias should be near Vcc/2 */
    float expected = ACS712_VREF_MV / 2.0f;
    if (bias_mv < (expected - ACS712_BIAS_TOLERANCE_MV) ||
        bias_mv > (expected + ACS712_BIAS_TOLERANCE_MV)) {
        fault = true;
    } else {
        fault = false;
    }
}

float ACS712_ReadCurrent_mA(void)
{
    if (fault) return 0.0f;

    float mv = adc_to_mv(adc_read_raw());
    /* current = (Vout - Vbias) / sensitivity.  Result in amps, convert to mA. */
    float current_a = (mv - bias_mv) / ACS712_SENSITIVITY_MV_PER_A;
    return current_a * 1000.0f;
}

bool ACS712_IsFault(void)
{
    return fault;
}

float ACS712_GetBias_mV(void)
{
    return bias_mv;
}
