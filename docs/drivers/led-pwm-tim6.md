# Software PWM Rainbow — TIM6

**Status**: VERIFIED 2026-03-25

## Hardware constraint

PK5, PK6, PK7 have no hardware timer PWM alternate function (FMC/LTDC pins only).
Hardware PWM output is impossible on these pins. Software PWM via timer interrupt is required.

## Timer configuration

| Item            | Value                                     |
|-----------------|-------------------------------------------|
| Timer           | TIM6 (basic timer, APB1)                  |
| TIM6 clock      | 240 MHz (APB1=120MHz, prescaler=2 -> x2)  |
| PSC             | 2399                                      |
| ARR             | 9                                         |
| ISR rate        | 240MHz / 2400 / 10 = 10 kHz              |
| PWM period      | 100 ISR ticks                             |
| PWM frequency   | 100 Hz                                    |
| Hue update rate | once per PWM period = every 10 ms         |
| Rainbow cycle   | 360 hue steps x 10ms = 3.6 s             |
| NVIC priority   | 5                                         |
| IRQ vector      | TIM6_DAC_IRQn (shared with DAC)           |

## Implementation

`src/led_pwm.c` / `include/led_pwm.h`

**ISR logic (HAL_TIM_PeriodElapsedCallback)**:
- Software counter 0..99 increments each ISR tick
- BSRR single-register write drives all three LED pins per tick
- Active LOW: `counter < duty` -> pin LOW (LED on); else pin HIGH (LED off)
- Every 100 ticks: advance hue 0..359, recompute R/G/B duty values via HSV->RGB

**GPIO drive**: Direct `LED_GPIO_PORT->BSRR` write (one register write covers R+G+B).

**HSV->RGB**: Integer math, H=0..359, S=V=100%, output scale 0..100 (matches PWM_PERIOD).

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
// In main(), after GPIO_LEDs_Init():
LedPwm_Init();   // configures TIM6, starts ISR-driven rainbow

while (1) {
    __WFI();     // CPU sleeps; all work happens in TIM6 ISR
}
```

## Gotchas

- **tim_ex required**: `stm32h7xx_hal_tim.c` calls `HAL_TIMEx_BreakCallback` and `HAL_TIMEx_CommutCallback` which are weak symbols in `stm32h7xx_hal_tim_ex.c`. Omitting `tim_ex` causes linker errors even though TIM6 never triggers those callbacks.
- **TIM6 clock doubling**: APB1 prescaler=2, so TIM6 timer clock = APB1 x 2 = 240MHz, not 120MHz. Use 240MHz in timing calculations.
- **Shared IRQ vector**: `TIM6_DAC_IRQn` is shared with the DAC peripheral. The handler checks `htim->Instance == TIM6` to guard against spurious calls if DAC is added later.
