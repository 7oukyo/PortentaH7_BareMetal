# LED Feedback & Serial Heartbeat — TIM6

**Status**: VERIFIED 2026-04-01

## Purpose

TIM6 ISR at 10kHz provides timing for a green LED blink on serial RX (~50ms).
Main loop polls `LedPwm_HeartbeatPoll()` to send `[sec.ms] alive` over CDC
every 5 seconds with a brief blue LED pulse.

## Hardware constraint

PK5, PK6, PK7 have no hardware timer PWM alternate function (FMC/LTDC pins only).
GPIO toggle via ISR or main loop is the only option.

## Timer configuration

| Item            | Value                                     |
|-----------------|-------------------------------------------|
| Timer           | TIM6 (basic timer, APB1)                  |
| TIM6 clock      | 240 MHz (APB1=120MHz, prescaler=2 -> x2)  |
| PSC             | 2399                                      |
| ARR             | 9                                         |
| ISR rate        | 240MHz / 2400 / 10 = 10 kHz              |
| Blink duration  | 500 ISR ticks = 50 ms (green LED on RX)   |
| Heartbeat       | 5000 ms poll interval (blue LED + CDC TX) |
| NVIC priority   | 5                                         |
| IRQ vector      | TIM6_DAC_IRQn (shared with DAC)           |

## Implementation

`src/led_pwm.c` / `include/led_pwm.h`

**LED roles**:
- Green (PK6): blinks ~50ms on serial packet receive (`LedPwm_BlinkOnRx()`)
- Blue (PK7): pulses ~30ms every 5s with heartbeat message (`LedPwm_HeartbeatPoll()`)
- Red (PK5): reserved for error/fault indication

**ISR logic (HAL_TIM_PeriodElapsedCallback)**: Counts down blink timer, turns green LED off when done.

**Heartbeat (main loop poll)**: Every 5s, formats `[uptime] alive\r\n` using manual integer-to-string (no snprintf — avoids pulling in newlib heap/`_sbrk`), sends via `CDC_Transmit_HS()`, flashes blue LED.

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

while (1) {
    LedPwm_HeartbeatPoll();  // 5s alive message + blue LED
}
```

## Gotchas

- **No snprintf**: Using `snprintf` pulls in newlib heap management (`_sbrk`) which we don't provide. Manual integer formatting avoids this.
- **tim_ex required**: `stm32h7xx_hal_tim.c` calls `HAL_TIMEx_BreakCallback` and `HAL_TIMEx_CommutCallback` which are weak symbols in `stm32h7xx_hal_tim_ex.c`. Omitting `tim_ex` causes linker errors.
- **TIM6 clock doubling**: APB1 prescaler=2, so TIM6 timer clock = APB1 x 2 = 240MHz, not 120MHz.
- **Shared IRQ vector**: `TIM6_DAC_IRQn` is shared with DAC. The handler checks `htim->Instance == TIM6`.
