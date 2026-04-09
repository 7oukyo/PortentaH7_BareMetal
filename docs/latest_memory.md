# Latest Working Memory

**Session Date**: 2026-04-09

---

## Session Summary

Completed three major tasks: ACS712 troubleshooting, module decoupling, and motor relay driver implementation.

## ACS712 Current Sensor — PA0_C / CH0

### Current State
- **ADC pin**: PA0_C (breakout ANALOG_A0) -> ADC1_INP0 (ADC_CHANNEL_0)
- **VREF+**: 3.1V from PMIC SW3
- **ACS712 output**: 2.43V (measured at zero motor current, VCC/2 of 5V supply)
- **Calibration**: 128-sample bias at startup, re-enabled ADC offset calibration
- **HAL_ADC_Stop**: Added after every conversion — STM32H7 requires explicit stop in single conversion mode

### PC2 Investigation (dead end — documented for reference)
PC2 (ANALOG_A4) was tested with channels 4, 12, and 18 — none read correctly:
- CH18 read USB ULPI noise (PA4/ULPI_D0, not PC2)
- CH12 and CH4 both read VREF+ (3.1V) regardless of input
- Schematic reference doc had wrong INP numbers (corrected: PC2=INP12, PA4=INP18)
- Root cause unresolved — may be SYSCFG analog switch issue or PCB routing
- **Resolution**: Reverted to PA0_C/CH0 which works correctly

### Pending
- User needs to move ACS712 wire from breakout ANALOG_A4 back to ANALOG_A0
- Once wire is moved, bias calibration should read ~2430-2500 mV instead of FAULT

## Module Decoupling — DONE

- Removed `#include "acs712.h"` from c4001.c
- Moved `send_report()` from c4001.c to main.c
- Added getters to c4001: `C4001_HasNewFrame()`, `C4001_GetFrameCount()`, `C4001_GetRxByteCount()`, `C4001_GetLastRaw()`
- Moved `adc_diag` command handler from c4001.c to main.c `HandleSerialCmd()`
- usbd_cdc_if.c now calls `HandleSerialCmd()` in main.c (not c4001 directly)
- Rule added to CLAUDE.md: module source files must be standalone

## Motor Relay Driver — DONE

- **Files**: src/motor_relay.c, include/motor_relay.h
- **Pins**: PC15 (Relay 1, GPIO_1) and PD5 (Relay 2, GPIO_3)
- **Logic**: Active-LOW relays, both HIGH = motor stopped
- **Safety**: Always goes through STOP before direction change, 100ms dead time on FWD↔REV
- **Motor test**: Auto-starts 3s after boot, cycles STOP(1s)→FWD(2s)→STOP(1s)→REV(2s)→repeat
- **Current monitoring**: 50mA threshold, emergency stop if exceeded
- **Serial commands**: motor_fwd, motor_rev, motor_stop, motor_test
- **Status output**: `[timestamp] MOTOR=FWD/REV/STOP I=xxx.xmA [TRIPPED]` every 100ms

### Pending
- Serial output not yet verified (firmware flashed successfully but COM4 not read)
- Motor relay hardware test pending (user was AFK)

## Build Status

Last successful build: 72660 text + 272 data + 24456 bss = 97388 total
Flashed successfully via OpenOCD, verified OK.
