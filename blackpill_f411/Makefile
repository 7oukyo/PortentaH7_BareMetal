######################################################################
# Makefile — BlackPill STM32F411CEU6 bare-metal firmware
# Ported from Portenta H7 sofa controller project.
######################################################################

# Toolchain
PREFIX  = arm-none-eabi-
CC      = $(PREFIX)gcc
AS      = $(PREFIX)gcc -x assembler-with-cpp
CP      = $(PREFIX)objcopy
SZ      = $(PREFIX)size

# MCU flags — Cortex-M4 with single-precision FPU
CPU     = -mcpu=cortex-m4
FPU     = -mfpu=fpv4-sp-d16
FLOAT   = -mfloat-abi=hard
MCU     = $(CPU) -mthumb $(FPU) $(FLOAT)

# Paths
BUILD_DIR   = build
SRC_DIR     = src
INC_DIR     = include
STARTUP_DIR = startup
LINKER_DIR  = linker
DRV_DIR     = drivers

# Linker script
LDSCRIPT = $(LINKER_DIR)/STM32F411CEUx_FLASH.ld

# C defines
C_DEFS  = -DUSE_HAL_DRIVER -DSTM32F411xE

# Include paths
C_INCLUDES = \
	-I$(INC_DIR) \
	-I$(DRV_DIR)/CMSIS/Device/ST/STM32F4xx/Include \
	-I$(DRV_DIR)/CMSIS/Include \
	-I$(DRV_DIR)/STM32F4xx_HAL_Driver/Inc \
	-I$(DRV_DIR)/STM32F4xx_HAL_Driver/Inc/Legacy \
	-I$(DRV_DIR)/STM32_USB_Device_Library/Core/Inc \
	-I$(DRV_DIR)/STM32_USB_Device_Library/Class/CDC/Inc

######################################################################
# Source files
######################################################################

# Application sources
APP_SRC = \
	$(SRC_DIR)/main.c \
	$(SRC_DIR)/c4001.c \
	$(SRC_DIR)/ina226.c \
	$(SRC_DIR)/motor_relay.c \
	$(SRC_DIR)/led.c \
	$(SRC_DIR)/stm32f4xx_hal_msp.c \
	$(SRC_DIR)/stm32f4xx_it.c \
	$(SRC_DIR)/system_stm32f4xx.c \
	$(SRC_DIR)/usb_device.c \
	$(SRC_DIR)/usbd_cdc_if.c \
	$(SRC_DIR)/usbd_conf.c \
	$(SRC_DIR)/usbd_desc.c

# HAL driver sources
HAL_SRC = \
	$(DRV_DIR)/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal.c \
	$(DRV_DIR)/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_cortex.c \
	$(DRV_DIR)/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_rcc.c \
	$(DRV_DIR)/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_rcc_ex.c \
	$(DRV_DIR)/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_gpio.c \
	$(DRV_DIR)/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_dma.c \
	$(DRV_DIR)/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_i2c.c \
	$(DRV_DIR)/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_i2c_ex.c \
	$(DRV_DIR)/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_uart.c \
	$(DRV_DIR)/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_tim.c \
	$(DRV_DIR)/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_tim_ex.c \
	$(DRV_DIR)/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_pcd.c \
	$(DRV_DIR)/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_pcd_ex.c \
	$(DRV_DIR)/STM32F4xx_HAL_Driver/Src/stm32f4xx_ll_usb.c \
	$(DRV_DIR)/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_pwr.c \
	$(DRV_DIR)/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_pwr_ex.c \
	$(DRV_DIR)/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_flash.c \
	$(DRV_DIR)/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_flash_ex.c

# USB middleware sources
USB_SRC = \
	$(DRV_DIR)/STM32_USB_Device_Library/Core/Src/usbd_core.c \
	$(DRV_DIR)/STM32_USB_Device_Library/Core/Src/usbd_ctlreq.c \
	$(DRV_DIR)/STM32_USB_Device_Library/Core/Src/usbd_ioreq.c \
	$(DRV_DIR)/STM32_USB_Device_Library/Class/CDC/Src/usbd_cdc.c

# All C sources
C_SOURCES = $(APP_SRC) $(HAL_SRC) $(USB_SRC)

# Assembly sources
ASM_SOURCES = $(STARTUP_DIR)/startup_stm32f411cetx.s

######################################################################
# Compiler / linker flags
######################################################################

CFLAGS  = $(MCU) $(C_DEFS) $(C_INCLUDES)
CFLAGS += -Wall -Wextra -fdata-sections -ffunction-sections
CFLAGS += -g3 -O0 -std=c11

ASFLAGS = $(MCU) -g

LDFLAGS  = $(MCU) -T$(LDSCRIPT)
LDFLAGS += -Wl,--gc-sections -Wl,--print-memory-usage
LDFLAGS += -Wl,--no-warn-rwx-segments
LDFLAGS += -specs=nano.specs -specs=nosys.specs
LDFLAGS += -lc -lm -lnosys

######################################################################
# Build rules
######################################################################

# Object files
OBJECTS  = $(addprefix $(BUILD_DIR)/,$(notdir $(C_SOURCES:.c=.o)))
OBJECTS += $(addprefix $(BUILD_DIR)/,$(notdir $(ASM_SOURCES:.s=.o)))

vpath %.c $(sort $(dir $(C_SOURCES)))
vpath %.s $(sort $(dir $(ASM_SOURCES)))

# Default target
all: $(BUILD_DIR)/firmware.elf $(BUILD_DIR)/firmware.bin size

$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/%.o: %.s | $(BUILD_DIR)
	$(AS) -c $(ASFLAGS) $< -o $@

$(BUILD_DIR)/firmware.elf: $(OBJECTS)
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@

$(BUILD_DIR)/firmware.bin: $(BUILD_DIR)/firmware.elf
	$(CP) -O binary $< $@

$(BUILD_DIR):
	powershell -Command "if (!(Test-Path '$(BUILD_DIR)')) { New-Item -ItemType Directory -Path '$(BUILD_DIR)' | Out-Null }"

size: $(BUILD_DIR)/firmware.elf
	$(SZ) $<

######################################################################
# Flash / debug targets
######################################################################

OPENOCD      = openocd
OPENOCD_CFG  = -f interface/stlink.cfg -f target/stm32f4x.cfg

flash: all
	$(OPENOCD) $(OPENOCD_CFG) -c "program $(BUILD_DIR)/firmware.bin 0x08000000 verify reset exit"

debug:
	$(OPENOCD) $(OPENOCD_CFG)

clean:
	powershell -Command "if (Test-Path '$(BUILD_DIR)') { Remove-Item -Recurse -Force '$(BUILD_DIR)' }"

.PHONY: all flash debug clean size
