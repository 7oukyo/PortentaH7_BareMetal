/**
 * @file acs712.c
 * @brief ACS712 5A current sensor driver.
 *
 * ADC1 channel INP1 on PA1_C (breakout Analog A1).
 * 128-sample bias calibration at startup, validates near VCC/2 (~2500 mV).
 * Motor wiring: current DECREASES output below bias, reported as positive mA.
 */

#include "acs712.h"
#include "usbd_cdc_if.h"
#include <string.h>

static ADC_HandleTypeDef hadc1;

static float    bias_mv   = 0.0f;   /* calibrated zero-current voltage */
static bool     fault     = false;  /* true if bias out of expected range */

/* Convert raw ADC count to millivolts (16-bit: 0..65535 -> 0..VREF+). */
static float adc_to_mv(uint32_t raw)
{
    return ((float)raw / ACS712_ADC_MAX) * ACS712_VREF_MV;
}

/* Perform a single blocking ADC conversion and return raw value.
 * Must stop ADC after each single conversion on STM32H7. */
static uint32_t adc_read_raw(void)
{
    if (HAL_ADC_Start(&hadc1) != HAL_OK) {
        return 0;
    }
    if (HAL_ADC_PollForConversion(&hadc1, 100) != HAL_OK) {
        HAL_ADC_Stop(&hadc1);
        return 0;
    }
    uint32_t val = HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);
    return val;
}

/* ---- ADC MSP (called by HAL_ADC_Init) ---- */

void HAL_ADC_MspInit(ADC_HandleTypeDef *hadc)
{
    if (hadc->Instance != ADC1) return;

    /* ADC clock: uses per_ck (HSI 64 MHz) by default, prescaled by
     * ADC_CLOCK_ASYNC_DIV6 = 10.7 MHz. Do NOT reconfigure PLL2 here —
     * it's already set for SPI (PLL2P=3, 120 MHz) in PeriphCommonClock_Config.
     * Touching PLL2 would break SPI and cause unstable ADC reads. */

    __HAL_RCC_ADC12_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    /* CRITICAL: Open the analog switch between PA1 (GPIO) and PA1_C (ADC).
     * Default on STM32H747 is all switches OPEN (PMCR bits 24-27 = 1 after
     * reset), but we set it explicitly per CubeMX reference. RM0399 §5.3.3 */
    HAL_SYSCFG_AnalogSwitchConfig(SYSCFG_SWITCH_PA1, SYSCFG_SWITCH_PA1_OPEN);
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

    /* ADC offset calibration (single-ended) */
    if (HAL_ADCEx_Calibration_Start(&hadc1, ADC_CALIB_OFFSET, ADC_SINGLE_ENDED) != HAL_OK) {
        Error_Handler();
    }

    /* Configure ADC channel for PA1_C (breakout ANALOG_A1, INP1) */
    ADC_ChannelConfTypeDef ch_cfg = {0};
    ch_cfg.Channel      = ACS712_ADC_CHANNEL;
    ch_cfg.Rank         = ADC_REGULAR_RANK_1;
    ch_cfg.SamplingTime = ADC_SAMPLETIME_387CYCLES_5;  /* long sample for stable analog */
    ch_cfg.SingleDiff   = ADC_SINGLE_ENDED;
    ch_cfg.OffsetNumber = ADC_OFFSET_NONE;

    if (HAL_ADC_ConfigChannel(&hadc1, &ch_cfg) != HAL_OK) {
        Error_Handler();
    }

    /* --- Zero-current bias calibration ---
     * Average 128 samples with no load before any relay activation.
     * ACS712 outputs VCC/2 (~2500 mV) at zero current. */
    uint32_t sum = 0;
    for (uint32_t i = 0; i < ACS712_CAL_SAMPLES; i++) {
        sum += adc_read_raw();
    }
    bias_mv = adc_to_mv(sum / ACS712_CAL_SAMPLES);

    /* Sanity check: bias should be near ACS712 VCC/2 (~2500 mV) */
    if (bias_mv < (ACS712_EXPECTED_BIAS_MV - ACS712_BIAS_TOLERANCE_MV) ||
        bias_mv > (ACS712_EXPECTED_BIAS_MV + ACS712_BIAS_TOLERANCE_MV)) {
        fault = true;
    } else {
        fault = false;
    }
}

float ACS712_ReadCurrent_mA(void)
{
    if (fault) return 0.0f;

    /* Average multiple samples to reduce noise */
    uint32_t sum = 0;
    for (uint32_t i = 0; i < ACS712_READ_SAMPLES; i++) {
        sum += adc_read_raw();
    }
    float mv = adc_to_mv(sum / ACS712_READ_SAMPLES);

    /* Motor wiring: current flow DECREASES output below bias.
     * (bias - mv) gives positive mA when motor draws current. */
    float current_a = (bias_mv - mv) / ACS712_SENSITIVITY_MV_PER_A;
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

/* Helper: append unsigned integer to buffer, return pointer past last digit. */
static char *uint_to_str(char *p, uint32_t v)
{
    char tmp[10];
    int i = 0;
    if (v == 0) { *p++ = '0'; return p; }
    while (v) { tmp[i++] = '0' + (v % 10); v /= 10; }
    while (i--) *p++ = tmp[i];
    return p;
}


/* Print raw ADC samples over USB for diagnostics.
 * Samples 16 values and reports min/max/avg plus bias and SYSCFG state. */
void ACS712_PrintRawSamples(void)
{
    uint32_t sum = 0, min_raw = 65535, max_raw = 0;
    const uint32_t sample_count = 16;
    uint32_t valid = 0;

    for (uint32_t i = 0; i < sample_count; i++) {
        uint32_t raw = adc_read_raw();
        if (raw > 0) {  /* Skip 0 values from ADC start/poll errors */
            sum += raw;
            valid++;
            if (raw < min_raw) min_raw = raw;
            if (raw > max_raw) max_raw = raw;
        }
    }

    uint32_t avg_raw = (valid > 0) ? (sum / valid) : 0;
    float avg_mv = adc_to_mv(avg_raw);

    char buf[200];
    char *p = buf;

    const char *hdr = "[ADC_RAW] samples=";
    while (*hdr) *p++ = *hdr++;
    p = uint_to_str(p, sample_count);

    const char *avg_label = " avg_raw=";
    while (*avg_label) *p++ = *avg_label++;
    p = uint_to_str(p, avg_raw);

    const char *min_label = " min_raw=";
    while (*min_label) *p++ = *min_label++;
    p = uint_to_str(p, min_raw);

    const char *max_label = " max_raw=";
    while (*max_label) *p++ = *max_label++;
    p = uint_to_str(p, max_raw);

    const char *avg_mv_label = " avg_mV=";
    while (*avg_mv_label) *p++ = *avg_mv_label++;
    uint32_t avg_int = (uint32_t)avg_mv;
    uint32_t avg_frac = (uint32_t)((avg_mv - (float)avg_int) * 10.0f);
    p = uint_to_str(p, avg_int);
    *p++ = '.';
    p = uint_to_str(p, avg_frac);

    /* Bias and SYSCFG info */
    const char *bias_label = " bias=";
    while (*bias_label) *p++ = *bias_label++;
    uint32_t b_int = (uint32_t)bias_mv;
    uint32_t b_frac = (uint32_t)((bias_mv - (float)b_int) * 10.0f);
    p = uint_to_str(p, b_int);
    *p++ = '.';
    p = uint_to_str(p, b_frac);

    /* SYSCFG_PMCR: bits 24-27 = PA0SO/PA1SO/PC2SO/PC3SO (1=switch open) */
    const char *pmcr_label = " PMCR=0x";
    while (*pmcr_label) *p++ = *pmcr_label++;
    uint32_t pmcr = SYSCFG->PMCR;
    /* Print hex: 8 nibbles */
    for (int i = 28; i >= 0; i -= 4) {
        uint8_t nibble = (pmcr >> i) & 0xF;
        *p++ = nibble < 10 ? ('0' + nibble) : ('A' + nibble - 10);
    }

    const char *fault_label = fault ? " FAULT" : " OK";
    while (*fault_label) *p++ = *fault_label++;

    *p++ = '\r';
    *p++ = '\n';

    CDC_Transmit_HS((uint8_t *)buf, (uint16_t)(p - buf));
}

/* Read a single ADC1 channel and return avg mV over 8 samples.
 * Temporarily reconfigures channel, restores ACS712_ADC_CHANNEL after. */
static float adc_read_channel_mv(uint32_t channel)
{
    ADC_ChannelConfTypeDef ch_cfg = {0};
    ch_cfg.Channel      = channel;
    ch_cfg.Rank         = ADC_REGULAR_RANK_1;
    ch_cfg.SamplingTime = ADC_SAMPLETIME_387CYCLES_5;
    ch_cfg.SingleDiff   = ADC_SINGLE_ENDED;
    ch_cfg.OffsetNumber = ADC_OFFSET_NONE;
    HAL_ADC_ConfigChannel(&hadc1, &ch_cfg);

    uint32_t sum = 0;
    for (int i = 0; i < 8; i++) {
        sum += adc_read_raw();
    }

    /* Restore original channel */
    ch_cfg.Channel = ACS712_ADC_CHANNEL;
    HAL_ADC_ConfigChannel(&hadc1, &ch_cfg);

    return adc_to_mv(sum / 8);
}

/* Scan multiple ADC channels and print results.
 * Helps identify which pin has a signal connected. */
void ACS712_ScanChannels(void)
{
    /* Scan all plausible ADC1 channels to find where the signal is */
    struct { uint32_t ch; const char *name; } channels[] = {
        { ADC_CHANNEL_0,  "CH0" },
        { ADC_CHANNEL_1,  "CH1" },
        { ADC_CHANNEL_2,  "CH2" },
        { ADC_CHANNEL_3,  "CH3" },
        { ADC_CHANNEL_4,  "CH4" },
        { ADC_CHANNEL_5,  "CH5" },
    };
    const int n = sizeof(channels) / sizeof(channels[0]);

    char buf[200];
    char *p = buf;

    const char *hdr = "[ADC_SCAN]";
    while (*hdr) *p++ = *hdr++;

    for (int i = 0; i < n; i++) {
        *p++ = ' ';
        const char *name = channels[i].name;
        while (*name) *p++ = *name++;
        *p++ = '=';
        float mv = adc_read_channel_mv(channels[i].ch);
        uint32_t mv_int = (uint32_t)mv;
        uint32_t mv_frac = (uint32_t)((mv - (float)mv_int) * 10.0f);
        p = uint_to_str(p, mv_int);
        *p++ = '.';
        p = uint_to_str(p, mv_frac);
        const char *unit = "mV";
        while (*unit) *p++ = *unit++;
    }

    *p++ = '\r'; *p++ = '\n';
    CDC_Transmit_HS((uint8_t *)buf, (uint16_t)(p - buf));
}

/* Re-run bias calibration (use after connecting sensor wire). */
void ACS712_Recalibrate(void)
{
    uint32_t sum = 0;
    for (uint32_t i = 0; i < ACS712_CAL_SAMPLES; i++) {
        sum += adc_read_raw();
    }
    bias_mv = adc_to_mv(sum / ACS712_CAL_SAMPLES);

    if (bias_mv < (ACS712_EXPECTED_BIAS_MV - ACS712_BIAS_TOLERANCE_MV) ||
        bias_mv > (ACS712_EXPECTED_BIAS_MV + ACS712_BIAS_TOLERANCE_MV)) {
        fault = true;
    } else {
        fault = false;
    }

    /* Report result */
    char buf[80];
    char *p = buf;
    const char *hdr = "[ADC_CAL] bias=";
    while (*hdr) *p++ = *hdr++;
    uint32_t b_int = (uint32_t)bias_mv;
    uint32_t b_frac = (uint32_t)((bias_mv - (float)b_int) * 10.0f);
    p = uint_to_str(p, b_int);
    *p++ = '.';
    p = uint_to_str(p, b_frac);
    const char *mv = "mV";
    while (*mv) *p++ = *mv++;
    const char *status = fault ? " FAULT" : " OK";
    while (*status) *p++ = *status++;
    *p++ = '\r'; *p++ = '\n';
    CDC_Transmit_HS((uint8_t *)buf, (uint16_t)(p - buf));
}
