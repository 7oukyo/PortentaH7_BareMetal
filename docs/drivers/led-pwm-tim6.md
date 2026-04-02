# LED Feedback — TIM6

**Status**: VERIFIED 2026-04-02

## Purpose

TIM6 ISR at 10kHz provides timing for a green LED blink on USB serial RX (~50ms).

## Hardware constraint

PK5, PK6, PK7 have no hardware timer PWM alternate function (FMC/LTDC pins only).
GPIO toggle via ISR is the only option.

## Timer configuration

| Item            | Value                                     |
|-----------------|-------------------------------------------|
| Timer           | TIM6 (basic timer, APB1)                  |
| TIM6 clock      | 240 MHz (APB1=120MHz, prescaler=2 -> x2)  |
| PSC             | 2399                                      |
| ARR             | 9                                         |
| ISR rate        | 240MHz / 2400 / 10 = 10 kHz              |
| Blink duration  | 500 ISR ticks = 50 ms (green LED on RX)   |
| NVIC priority   | 5                                         |
| IRQ vector      | TIM6_DAC_IRQn (shared with DAC)           |

## Implementation

`src/led_pwm.c` / `include/led_pwm.h`

**LED roles**:
- Green (PK6): blinks ~50ms on USB serial packet receive (`LedPwm_BlinkOnRx()`)
- Red (PK5): ON while C4001 detects presence, OFF otherwise (driven by `C4001_Poll()`)
- Blue (PK7): unused / available

**ISR logic (HAL_TIM_PeriodElapsedCallback)**: Counts down blink timer, turns green LED off when done.

## HAL modules required

Add both to HAL_SRC in Makefile:

- `stm32h7xx_hal_tim.c`
- `stm32h7xx_hal_tim_ex.c` — required: HAL_TIM_IRQHandler calls callbacks defined here

Enable in stm32h7xx_hal_conf.h:

```c
#define HAL_TIM_MODULE_ENABLED
```

## Initialization sequence

```c
// In main(), after MX_USB_DEVICE_Init():
LedPwm_Init();   // configures TIM6, starts 10kHz ISR
```

## Gotchas

- **tim_ex required**: `stm32h7xx_hal_tim.c` calls `HAL_TIMEx_BreakCallback` and `HAL_TIMEx_CommutCallback` which are weak symbols in `stm32h7xx_hal_tim_ex.c`. Omitting `tim_ex` causes linker errors.
- **TIM6 clock doubling**: APB1 prescaler=2, so TIM6 timer clock = APB1 x 2 = 240MHz, not 120MHz.
- **Shared IRQ vector**: `TIM6_DAC_IRQn` is shared with DAC. The handler checks `htim->Instance == TIM6`.
