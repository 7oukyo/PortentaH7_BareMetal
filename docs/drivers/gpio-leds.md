# GPIO — Onboard RGB LED (GPIOK)

**Status**: VERIFIED 2026-04-01

## Hardware

| LED   | Pin | Active level |
|-------|-----|--------------|
| Red   | PK5 | LOW          |
| Green | PK6 | LOW          |
| Blue  | PK7 | LOW          |

All three on GPIOK. Push-pull output, no pull resistor needed.

## Initialization

```c
__HAL_RCC_GPIOK_CLK_ENABLE();

GPIO_InitTypeDef g = {0};
g.Pin   = GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7;
g.Mode  = GPIO_MODE_OUTPUT_PP;
g.Pull  = GPIO_NOPULL;
g.Speed = GPIO_SPEED_FREQ_LOW;
HAL_GPIO_Init(GPIOK, &g);

// All LEDs off (active LOW → set HIGH)
HAL_GPIO_WritePin(GPIOK, GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7, GPIO_PIN_SET);
```

## Usage

```c
// Turn green on
HAL_GPIO_WritePin(GPIOK, GPIO_PIN_6, GPIO_PIN_RESET);

// Turn green off
HAL_GPIO_WritePin(GPIOK, GPIO_PIN_6, GPIO_PIN_SET);

// Toggle green (blink)
HAL_GPIO_TogglePin(GPIOK, GPIO_PIN_6);
HAL_Delay(500);
```

## Current LED Roles

| LED   | Function                          | Trigger                     |
|-------|-----------------------------------|-----------------------------|
| Green | RX blink (~50ms)                  | `LedPwm_BlinkOnRx()` from CDC receive |
| Red   | ON while presence detected         | `C4001_Poll()` from main loop |
| Blue  | Unused / available                 | —                           |

## Gotchas

- Active LOW: writing `GPIO_PIN_RESET` turns the LED **on**.
- GPIOK clock must be enabled before `HAL_GPIO_Init()` — easy to miss since GPIOK is not a commonly used port.
- GPIOK is in the AHB4 domain (`__HAL_RCC_GPIOK_CLK_ENABLE()`).
