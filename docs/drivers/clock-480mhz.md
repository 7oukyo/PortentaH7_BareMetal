# Clock — 480 MHz CM7 via HSE + PLL1

**Status**: VERIFIED 2026-03-25

## Configuration

| Domain       | Frequency  | Source          |
|--------------|------------|-----------------|
| CM7 SYSCLK   | 480 MHz    | PLL1P           |
| HCLK (AHB)   | 240 MHz    | SYSCLK / 2      |
| APB1/2/3/4   | 120 MHz    | HCLK / 2        |
| HSE crystal  | 25 MHz     | External BYPASS  |

## PLL1 parameters

| Param | Value | Note |
|-------|-------|------|
| M     | 5     | 25 MHz / 5 = 5 MHz ref |
| N     | 192   | 5 × 192 = 960 MHz VCO |
| P     | 2     | 960 / 2 = 480 MHz |
| Q     | 2     | (unused, 480 MHz) |
| R     | 2     | (unused, 480 MHz) |
| VCIRANGE | RCC_PLL1VCIRANGE_2 | 4–8 MHz ref range |
| VCOSEL   | RCC_PLL1VCOWIDE    | 192–836 MHz VCO |

## VOS and flash latency

- Power mode: `PWR_SMPS_1V8_SUPPLIES_LDO`
- VOS: VOS0 (highest performance, required for 480 MHz)
- Flash latency: `FLASH_LATENCY_4` (4 wait states at 480 MHz VOS0)

## Oscillator enable (PH1)

The Portenta H7 board has an on-board 25 MHz crystal controlled by GPIO PH1. PH1 must be driven HIGH before `SystemClock_Config()` is called, otherwise HSE never comes up and the PLL fails to lock.

```c
// Enable GPIOH clock and drive PH1 high before SystemClock_Config
__HAL_RCC_GPIOH_CLK_ENABLE();
HAL_GPIO_WritePin(GPIOH, GPIO_PIN_1, GPIO_PIN_SET);
GPIO_InitTypeDef g = {GPIO_PIN_1, GPIO_MODE_OUTPUT_PP, GPIO_NOPULL, GPIO_SPEED_FREQ_LOW};
HAL_GPIO_Init(GPIOH, &g);
```

## system_stm32h7xx.c requirements

HAL requires two globals and one array:
```c
uint32_t SystemCoreClock = 64000000UL;  // updated by SystemCoreClockUpdate()
uint32_t SystemD2Clock   = 64000000UL;  // used by HAL_RCC_ClockConfig
const uint8_t D1CorePrescTable[16] = {0,0,0,0,1,2,3,4,1,2,3,4,6,7,8,9};
```

`SystemD2Clock` is easy to miss — omitting it causes a linker error.

## Minimal usage

```c
// In main(), after PMIC_EarlyInit() and HAL_Init():
__HAL_RCC_GPIOH_CLK_ENABLE();
// drive PH1 high (oscillator enable)
...
SystemClock_Config();  // defined in main.c
```
