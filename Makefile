##############################################################################
# Portenta H7 bare-metal Makefile
# Target: STM32H747XIH6 CM7 core, arm-none-eabi-gcc, Windows host
##############################################################################

TARGET    = firmware
BUILD_DIR = build

# Toolchain
CC      = arm-none-eabi-gcc
AS      = arm-none-eabi-gcc -x assembler-with-cpp
OBJCOPY = arm-none-eabi-objcopy
SIZE    = arm-none-eabi-size

# MCU flags: Cortex-M7 with FPv5-D16 hard-float ABI
MCU = -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard

# Preprocessor defines required by STM32 HAL and CMSIS
DEFS = -DUSE_HAL_DRIVER -DSTM32H747xx -DCORE_CM7

# Include search paths
INC = \
  -Iinclude \
  -Idrivers/CMSIS/Include \
  -Idrivers/CMSIS/Device/ST/STM32H7xx/Include \
  -Idrivers/STM32H7xx_HAL_Driver/Inc \
  -Idrivers/STM32H7xx_HAL_Driver/Inc/Legacy \
  -Imiddlewares/ST/USB_Device_Library/Core/Inc \
  -Imiddlewares/ST/USB_Device_Library/Class/CDC/Inc

# Application source files
APP_SRC = \
  src/main.c \
  src/pmic.c \
  src/led_pwm.c \
  src/system_stm32h7xx.c \
  src/stm32h7xx_it.c \
  src/stm32h7xx_hal_msp.c \
  src/usb_device.c \
  src/usbd_conf.c \
  src/usbd_desc.c \
  src/usbd_cdc_if.c \
  src/c4001.c \
  src/acs712.c

# HAL driver source files (only modules enabled in stm32h7xx_hal_conf.h)
HAL_SRC = \
  drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal.c \
  drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_cortex.c \
  drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_dma.c \
  drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_dma_ex.c \
  drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_exti.c \
  drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_flash.c \
  drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_flash_ex.c \
  drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_gpio.c \
  drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_hsem.c \
  drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_i2c.c \
  drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_i2c_ex.c \
  drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_mdma.c \
  drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pwr.c \
  drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pwr_ex.c \
  drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_rcc.c \
  drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_rcc_ex.c \
  drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_tim.c \
  drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_tim_ex.c \
  drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c \
  drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd_ex.c \
  drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_uart.c \
  drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_uart_ex.c \
  drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_usb.c \
  drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_adc.c \
  drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_adc_ex.c

# USB Device Library middleware
USB_SRC = \
  middlewares/ST/USB_Device_Library/Core/Src/usbd_core.c \
  middlewares/ST/USB_Device_Library/Core/Src/usbd_ctlreq.c \
  middlewares/ST/USB_Device_Library/Core/Src/usbd_ioreq.c \
  middlewares/ST/USB_Device_Library/Class/CDC/Src/usbd_cdc.c

# Startup assembly
ASM_SRC = startup/startup_stm32h747xihx.s

# Linker script
LDSCRIPT = linker/STM32H747XIHX_CM7.ld

##############################################################################
# Build flags
##############################################################################

CFLAGS = $(MCU) $(DEFS) $(INC) \
  -Wall -Wextra \
  -fdata-sections -ffunction-sections \
  -g3 -O0 \
  -std=c11

ASFLAGS = $(MCU) -g3

LDFLAGS = $(MCU) \
  -T$(LDSCRIPT) \
  --specs=nano.specs \
  -Wl,-Map=$(BUILD_DIR)/$(TARGET).map,--cref \
  -Wl,--gc-sections \
  -Wl,--print-memory-usage \
  -Wl,--no-warn-rwx-segments

##############################################################################
# Object file lists (preserve directory structure under build/)
##############################################################################

ALL_SRC_C  = $(APP_SRC) $(HAL_SRC) $(USB_SRC)
ALL_OBJ_C  = $(addprefix $(BUILD_DIR)/,$(ALL_SRC_C:.c=.o))
ALL_OBJ_S  = $(addprefix $(BUILD_DIR)/,$(ASM_SRC:.s=.o))
ALL_OBJS   = $(ALL_OBJ_C) $(ALL_OBJ_S)

##############################################################################
# Create build directory structure at Makefile parse time (Windows-compatible)
##############################################################################

$(shell mkdir -p $(BUILD_DIR))

##############################################################################
# Targets
##############################################################################

.PHONY: all flash clean debug

all: $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).bin
	$(SIZE) $(BUILD_DIR)/$(TARGET).elf

$(BUILD_DIR)/$(TARGET).elf: $(ALL_OBJS)
	$(CC) $(ALL_OBJS) $(LDFLAGS) -o $@

$(BUILD_DIR)/$(TARGET).bin: $(BUILD_DIR)/$(TARGET).elf
	$(OBJCOPY) -O binary $< $@

# Compile .c files — create subdirectory before compiling
$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS) $< -o $@

# Assemble .s files — create subdirectory before assembling
$(BUILD_DIR)/%.o: %.s
	@mkdir -p $(dir $@)
	$(AS) -c $(ASFLAGS) $< -o $@

# Flash firmware via OpenOCD (ST-Link V3)
flash: all
	openocd -f openocd.cfg \
	  -c "program $(BUILD_DIR)/$(TARGET).bin 0x08000000 verify reset exit"

# Start OpenOCD GDB server for VS Code Cortex-Debug
debug:
	openocd -f openocd.cfg

# Remove build output (Windows del/rmdir)
clean:
	@cmd /c "if exist $(BUILD_DIR) rmdir /s /q $(BUILD_DIR)" 2>nul || rm -rf $(BUILD_DIR)
	@echo Clean done.
