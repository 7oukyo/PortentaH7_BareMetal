# CubeMX Configuration Plan

Reference configs to generate for validation and future peripheral bringup.

## Priority 1: Validate Existing Code

### ADC1 on PA1_C (OBSOLETE — ACS712 abandoned)

- **Location**: `CubeMX_Output/PortentaH7_MX/`
- **Status**: Generated 2026-04-10, no longer needed. ACS712 replaced by INA226 (I2C3).
- **Key finding**: `HAL_SYSCFG_AnalogSwitchConfig` is required for `_C` pins. Useful if ADC is ever revisited.

### USB OTG HS with ULPI (validate existing)

Generate CubeMX config for USB OTG HS in Device mode with external ULPI PHY (USB3320):

- **Purpose**: Confirm our ULPI pin mapping, PLL3 48 MHz clock config, and PHY reset sequence
- **CubeMX settings**:
  - USB_OTG_HS: Device_Only, External Phy
  - Middleware: USB_DEVICE -> CDC class
  - PLL3: Q=4 -> 48 MHz USB clock
  - GPIO: PJ4 for PHY reset (manual in code)
- **Compare**: Generated HAL_PCD_MspInit against our usbd_conf.c

### UART4 (validate existing C4001 config)

Generate CubeMX config for UART4 at 9600 baud:

- **Purpose**: Confirm PA0 (TX, AF8) and PI9 (RX, AF8) pin assignments and interrupt config
- **CubeMX settings**:
  - UART4: Asynchronous, 9600 baud, 8N1
  - NVIC: UART4 global interrupt enabled
- **Compare**: Generated HAL_UART_MspInit against our c4001.c

## Priority 2: New Peripherals

### SPI (for future external devices)

- **Purpose**: Validate PLL2 clock config for SPI2/SPI5
- **CubeMX settings**:
  - SPI2: Full-Duplex Master
  - PLL2: M=5, N=72, P=3 (120 MHz)
  - GPIO: Check which pins CubeMX assigns on Portenta H7
- **Note**: PLL2P is shared between SPI and potentially ADC. CubeMX may help resolve if we should use PLL3R for ADC instead.

### I2C3 or I2C4 (for user I2C bus)

- **Purpose**: Bring up an I2C bus for external sensors (separate from PMIC on I2C1)
- **CubeMX settings**:
  - I2C3: Standard or Fast mode, 100/400 kHz
  - Find available pins on Portenta breakout
- **Note**: I2C1 is occupied by PMIC. I2C3 or I2C4 would be for user devices.

### DAC (for analog output)

- **Purpose**: Generate analog voltages for testing or actuator control
- **CubeMX settings**:
  - DAC1: Channel 1 or 2
  - Check pin availability (PA4 is ULPI_D0 — may conflict)

### CAN FD (for automotive/industrial bus)

- **Purpose**: CAN bus communication for future integration
- **CubeMX settings**:
  - FDCAN1 or FDCAN2
  - Check pin mapping on Portenta breakout

### SDRAM (FMC, 8 MB)

- **Purpose**: External SDRAM at 0xC0000000 for large buffers
- **CubeMX settings**:
  - FMC: SDRAM, 16-bit data bus
  - Timing from Portenta H7 schematic (IS42S16160J or similar)
  - MPU region config for SDRAM
- **Note**: Complex — many pins, timing-critical. Best done with CubeMX reference.

### Ethernet (RMII)

- **Purpose**: Network connectivity
- **CubeMX settings**:
  - ETH: RMII mode
  - LAN8742 PHY (if Portenta uses this)
  - Requires D-Cache with maintenance, or place buffers in non-cached SRAM
- **Note**: Most complex peripheral. Reference repo has FreeRTOS+LwIP config.

## Priority 3: System Validation

### Clock Tree Full Validation

Generate a CubeMX project with ALL active peripherals configured simultaneously to verify no clock conflicts:

- PLL1: 480 MHz SYSCLK
- PLL2: SPI clocks (120 MHz)
- PLL3: USB 48 MHz + potentially ADC
- Verify HCLK/APB dividers match our code

### Power Configuration

- Compare CubeMX's PWR supply config (SMPS+LDO) against our `HAL_PWREx_ConfigSupply(PWR_SMPS_1V8_SUPPLIES_LDO)`
- Verify voltage scaling (VOS0) is correct for 480 MHz

## How To Generate

1. Open STM32CubeMX, select STM32H747XIH6
2. Work in CM7 context only
3. Configure peripheral as described above
4. Generate code (Toolchain: Makefile or SW4STM32)
5. Place output in `CubeMX_Output/<peripheral_name>/`
6. Compare generated `stm32h7xx_hal_msp.c` and `main.c` against our code
7. Document any discrepancies in `docs/hardware-notes.md`
