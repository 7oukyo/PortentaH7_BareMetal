# USB CDC Virtual COM Port (USB3320 ULPI PHY)

**Status**: VERIFIED (enumeration + echo-back)
**Date**: 2026-04-01

## Hardware

- **PHY**: USB3320C-EZK external ULPI PHY, connected to USB_OTG_HS
- **Connector**: USB-C (directly wired to USB3320 D+/D-)
- **PHY reset**: PJ4 GPIO push-pull (LOW=reset, HIGH=active)
- **PHY power**: SW1 (+3V1SW) for VDDIO/VDD3.3, LDO2 (+1V8) for VDD1.8 and 27 MHz oscillator
- **VID/PID**: 0x2341 / 0x025B (Arduino / Portenta H7)

## ULPI Pin Map (all AF10 = GPIO_AF10_OTG2_HS)

| Signal | Pin  | Direction |
|--------|------|-----------|
| D0     | PA3  | Bidir     |
| D1     | PB0  | Bidir     |
| D2     | PB1  | Bidir     |
| D3     | PB10 | Bidir     |
| D4     | PB11 | Bidir     |
| D5     | PB12 | Bidir     |
| D6     | PB13 | Bidir     |
| D7     | PB5  | Bidir     |
| CLK    | PA5  | Input     |
| STP    | PC0  | Output    |
| NXT    | PH4  | Input     |
| DIR    | PI11 | Input     |

## Clock

- **PLL3**: HSE 25 MHz / M=25 * N=192 / Q=4 = **48 MHz** USB clock
- Selected via `RCC_USBCLKSOURCE_PLL3`

## Init Sequence

1. PMIC must be initialized first (SW1 and LDO2 power the USB3320)
2. PJ4 LOW -> 10 ms delay -> PJ4 HIGH -> 10 ms delay (PHY reset)
3. `MX_USB_DEVICE_Init()` — registers CDC class, starts USB stack

In `main.c` this comes after `PMIC_Init()` and before `LedPwm_Init()`.

## Architecture (3 layers)

```
Application (usbd_cdc_if.c)    -- CDC callbacks, echo/transmit API
  |
ST USB Device Library          -- middlewares/ST/USB_Device_Library/ (unmodified)
  |                               Core: usbd_core, usbd_ctlreq, usbd_ioreq
  |                               Class/CDC: usbd_cdc
  |
HAL PCD + Board Support        -- usbd_conf.c (LL interface, MSP init, callbacks)
  |                               stm32h7xx_hal_pcd.c (HAL driver, unmodified)
  |
Hardware                       -- USB_OTG_HS peripheral + USB3320 ULPI PHY
```

## Files

| File | Purpose |
|------|---------|
| `src/usb_device.c` | Init wrapper: USBD_Init -> RegisterClass -> RegisterInterface -> Start |
| `src/usbd_conf.c` | PCD MSP init (PLL3, GPIOs, NVIC), LL driver interface, static malloc |
| `src/usbd_desc.c` | Device/config/string descriptors, serial from STM32 UID |
| `src/usbd_cdc_if.c` | CDC callbacks: Init (arms RX), Receive (echo), Transmit API |
| `include/usb_device.h` | MX_USB_DEVICE_Init() declaration |
| `include/usbd_conf.h` | USB config constants, memory macros, debug stubs |
| `include/usbd_desc.h` | Descriptor declarations |
| `include/usbd_cdc_if.h` | CDC_Transmit_HS() API, buffer size defines (2048 RX/TX) |

## Key Configuration (usbd_conf.c)

```c
hpcd_USB_OTG_HS.Init.speed               = PCD_SPEED_FULL;
hpcd_USB_OTG_HS.Init.phy_itface          = USB_OTG_ULPI_PHY;
hpcd_USB_OTG_HS.Init.vbus_sensing_enable = DISABLE;
hpcd_USB_OTG_HS.Init.dma_enable          = DISABLE;
```

FIFO: 512 words RX, 128 words EP0 TX, 372 words EP1 TX.

## Usage

```c
#include "usbd_cdc_if.h"

// Send data over USB CDC
uint8_t msg[] = "Hello from Portenta!\r\n";
CDC_Transmit_HS(msg, sizeof(msg) - 1);
// Returns USBD_BUSY if previous TX still in progress
```

Current behavior: echo-back with green LED blink on RX, plus `[sec.ms] alive` printed every 5s with blue LED pulse.

## Gotchas

- **Must use PCD_SPEED_FULL, not PCD_SPEED_HIGH**: Despite USB3320 supporting HS, the reference project uses FS and HS mode caused data transfer to silently fail (device enumerates but no data flows). FS (12 Mbps) is more than enough for CDC.
- **First receive is armed by the ST library**: `USBD_CDC_Init()` (in the middleware) calls `USBD_LL_PrepareReceive` after the `Init` callback. No need to call `USBD_CDC_ReceivePacket()` in `CDC_Init_HS()`.
- **Power sequencing**: USB3320 won't work if PMIC hasn't brought up SW1 and LDO2. PJ4 reset must happen after power is stable.
- **No VBUS sensing**: Disabled because USB-C VBUS routing on Portenta doesn't support it cleanly.
- **OTG HS interrupt priority**: Set to 15 (lowest). Increase if latency is an issue.
- **D-Cache**: D-Cache is intentionally disabled. If enabled later, USB DMA buffers would need cache maintenance or placement in non-cacheable region.
