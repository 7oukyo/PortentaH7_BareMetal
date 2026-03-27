# Hardware Notes

## 2026-03-27 — Cold boot fix: SystemInit full RCC reset (SOLVED)

**Finding**: MCU entered a continuous reset loop on cold boot (power cycle). PINRSTF flag was set
in RCC_RSR, indicating external NRST assertion. The MCU never reached main(). Even the ROM
bootloader (BOOT0=HIGH) couldn't run. The code worked fine when flashed via debugger (warm boot)
or when flashed after Arduino bootloader had previously configured the PMIC.

**Root cause**: Our original `SystemInit()` only enabled FPU and set VTOR. The reference project's
`SystemInit()` (from `system_stm32h7xx_dualcore_boot_cm4_cm7.c`) does a **full RCC reset** before
main() runs:
- Resets CFGR, D1CFGR, D2CFGR, D3CFGR to zero
- Resets all PLL configs (PLLCKSELR, PLLCFGR, PLL1/2/3 DIVR/FRACR)
- Sets flash latency to default
- Enables HSION, disables HSE/PLL
- Disables FMC bank1 (prevents 24us CPU speculation block)
- Enables HSEM EXTI line 78 for CM4 sync
- Applies revY silicon AXI SRAM workaround
- Sets SEVONPEND for WFI/WFE wakeup

Without the full RCC reset, the clock/power system was in an undefined state on cold boot, causing
the PMIC to hold the MCU in reset via RESETBMCU.

**Additional changes that fixed cold boot**:
1. **Linker script**: Moved stack from DTCMRAM (0x20020000) to AXI RAM (0x24080000) to match reference.
2. **main.c init sequence**: Added PH1 oscillator enable with `HAL_Delay(10)` before `HAL_GPIO_WritePin`,
   added `PeriphCommonClock_Config()` (PLL2 for SPI clocks), matched reference clock config exactly
   (added `__HAL_RCC_PLL_PLLSOURCE_CONFIG`, `PLLRGE`, `PLLVCOSEL`, `PLLFRACN` fields, all APB dividers).
3. **CM4 boot**: Reference enables CM4 with `HAL_RCCEx_EnableBootCore(RCC_BOOT_C2)`, but we disabled
   this because we have no CM4 firmware — enabling it caused stale Arduino CM4 code to run (blue LED blink).

**Lesson**: Always use the full ST-provided SystemInit for the STM32H7. The minimal "just set FPU and
VTOR" approach is insufficient — the RCC state after power-on is not deterministic without explicit reset.

---

## 2026-03-26 — PMIC init broken on cold boot (fixed)

**Finding**: PMIC init must work without HAL on cold boot. HSI 64MHz only.
Previous success was a false positive — PMIC retained its 3.3V config from the Arduino bootloader.
On first true cold boot (USB-C power cycled, no bootloader), PMIC never responded, 3.3V never came up.
SWD showed 0V on VTref. External 3.3V injection required to recover SWD access.

**Root cause**: PMIC is on I2C1 (PB6/PB7, AF4). Firmware was using I2C4 (PD12/PD13) — pins not
connected to the PMIC. Every I2C write went to empty traces. All transactions NACKed silently.
The Arduino bootloader had configured the PMIC via the correct I2C1 bus, masking the bug entirely.
Additionally: MCU boots in ~1ms, PF1550 POR takes ~5ms — startup delay also needed.

**Fix applied in pmic.c**:

1. Switched from I2C4/PD12/PD13 to I2C1/PB6/PB7 (AF4) — the actual PMIC bus.
2. Rewrote to use HAL I2C after HAL_Init() and SystemClock_Config() (matching reference).
3. I2C timing 0x307075B1 for D2PCLK1=120MHz (post clock config).

**Recovery procedure when 3.3V is not present**: inject 3.3V externally on the 3.3V rail to power
the SWD VTref and board, then flash corrected firmware. Remove external supply; cold boot from USB-C.

---

## 2026-03-25 — Rainbow LED PWM confirmed

**Firmware**: TIM6 software PWM rainbow (src/led_pwm.c)
**Observation**: RGB LED cycling smoothly through full rainbow, ~3.6s per cycle, no visible flicker
**Conclusion**: TIM6 ISR at 10kHz, 100Hz software PWM, HSV->RGB all working correctly

---

## 2026-03-25 — Board bringup: green LED blink confirmed

**Firmware**: minimal bare-metal blink (src/main.c, src/pmic.c)
**Observation**: green LED (PK6) blinking at ~1 Hz as expected after flash via `make flash`
**Conclusion**: PMIC 3.3V rail OK, 480 MHz clock tree OK, GPIO output OK. Full bringup stack verified.

---

## 2026-03-25 — OpenOCD flash errors (now resolved)

Two errors encountered during first flash attempts:

### Error 1: `Cortex-M CPUID: 0xa05f0000 is unrecognized`

- **Cause**: Using `interface/stlink.cfg` (HLA/high-level adapter mode). HLA mode cannot identify the STM32H747 CPUID correctly.
- **Fix**: Switch to `interface/stlink-dap.cfg` with `transport select dapdirect_swd`. DAP direct mode speaks to the CoreSight DP directly and correctly identifies CM7.

### Error 2: `Cortex-M CPUID: 0x23000000 is unrecognized` / `Unable to reset target`

- **Cause**: With `DUAL_CORE 1` (default), OpenOCD tries to examine and reset both cpu0 (CM7) and cpu1 (CM4). CM4 is held in reset by our firmware (never released), so its CPUID reads garbage.
- **Fix**: Set `DUAL_CORE 0` in openocd.cfg. OpenOCD then only manages cpu0 (CM7) and ignores cpu1 entirely.

**Working openocd.cfg key settings**:

```tcl
source [find interface/stlink-dap.cfg]
transport select dapdirect_swd
set DUAL_CORE 0
set CONNECT_UNDER_RESET 1
reset_config srst_only srst_nogate connect_assert_srst
```
