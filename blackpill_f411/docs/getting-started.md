# BlackPill F411 — Getting Started

## Prerequisites

- **Compiler**: arm-none-eabi-gcc (ARM GNU Toolchain 13.x)
- **Build**: GNU Make
- **Flash/Debug**: OpenOCD with ST-Link V2/V3
- **IDE**: VS Code + Cortex-Debug (optional)

## Step 1: Get the STM32CubeF4 HAL drivers

The HAL and USB middleware live in [blackpill_f411/drivers/](../drivers/). They are not checked into the project repo — copy them from ST's official repo.

### Option A — Clone + submodule init (recommended, ~50 MB)

The STM32CubeF4 repo is a top-level meta-repo; the actual HAL and middleware are submodules. Clone once, then init only the three submodules you need:

```bash
# one-time: clone outside the project tree
cd x:/stm32cube
git clone --depth 1 https://github.com/STMicroelectronics/STM32CubeF4.git

# init only what we need (skip BSPs, demos, other MCU families)
cd STM32CubeF4
git submodule update --init --depth 1 \
  Drivers/CMSIS/Device/ST/STM32F4xx \
  Drivers/STM32F4xx_HAL_Driver \
  Middlewares/ST/STM32_USB_Device_Library
```

### Option B — Download submodule zips manually

Download each submodule as a ZIP from its own repo:

- [cmsis_device_f4](https://github.com/STMicroelectronics/cmsis_device_f4)
- [stm32f4xx_hal_driver](https://github.com/STMicroelectronics/stm32f4xx_hal_driver)
- [stm32-mw-usb-device](https://github.com/STMicroelectronics/stm32-mw-usb-device)

## Step 2: Copy the needed subdirectories into `drivers/`

Resulting layout in `blackpill_f411/drivers/`:

```
drivers/
├── CMSIS/
│   ├── Device/ST/STM32F4xx/Include/    <- from cmsis_device_f4
│   └── Include/                         <- CMSIS core (cmsis_gcc.h, core_cm4.h, ...)
├── STM32F4xx_HAL_Driver/
│   ├── Inc/                             <- from stm32f4xx_hal_driver
│   └── Src/
└── STM32_USB_Device_Library/
    ├── Core/    (Inc + Src)             <- from stm32-mw-usb-device
    └── Class/CDC/    (Inc + Src)
```

One-liner when using Option A (from project root):

```bash
mkdir -p blackpill_f411/drivers/CMSIS/Device/ST/STM32F4xx \
         blackpill_f411/drivers/CMSIS/Include \
         blackpill_f411/drivers/STM32F4xx_HAL_Driver \
         blackpill_f411/drivers/STM32_USB_Device_Library/Class

cp -r x:/stm32cube/STM32CubeF4/Drivers/CMSIS/Device/ST/STM32F4xx/Include \
      blackpill_f411/drivers/CMSIS/Device/ST/STM32F4xx/
cp -r x:/stm32cube/STM32CubeF4/Drivers/CMSIS/Include/. \
      blackpill_f411/drivers/CMSIS/Include/
cp -r x:/stm32cube/STM32CubeF4/Drivers/STM32F4xx_HAL_Driver/Inc \
      blackpill_f411/drivers/STM32F4xx_HAL_Driver/
cp -r x:/stm32cube/STM32CubeF4/Drivers/STM32F4xx_HAL_Driver/Src \
      blackpill_f411/drivers/STM32F4xx_HAL_Driver/
cp -r x:/stm32cube/STM32CubeF4/Middlewares/ST/STM32_USB_Device_Library/Core \
      blackpill_f411/drivers/STM32_USB_Device_Library/
cp -r x:/stm32cube/STM32CubeF4/Middlewares/ST/STM32_USB_Device_Library/Class/CDC \
      blackpill_f411/drivers/STM32_USB_Device_Library/Class/
```

The Makefile only compiles the HAL source files it explicitly lists in `HAL_SRC`, so copying the whole `Src/` directory is harmless — unused files are never touched.

## Step 3: Build

```bash
cd blackpill_f411
make
```

Expected output:

```
Memory region         Used Size  Region Size  %age Used
           FLASH:       ~52 KB        512 KB      ~10%
             RAM:       ~17 KB        128 KB      ~13%
```

Artifacts: `build/firmware.elf`, `build/firmware.bin`.

Benign warnings seen in a clean build:

- `unused parameter 'Banks'` inside ST's `stm32f4xx_hal_flash_ex.c` — vendor code, ignore
- `_close / _lseek / _read / _write is not implemented` — newlib-nano stubs from `-specs=nosys.specs`, never called at runtime

## Step 4: Flash via ST-Link

```bash
make flash
```

Uses OpenOCD with `interface/stlink.cfg` + `target/stm32f4x.cfg` to program `build/firmware.bin` at `0x08000000`.

## Step 5: Connect serial

Open a terminal on the USB CDC VCP enumerated by the BlackPill:

- Baud: **9600** (matches H7 firmware — over USB the baud rate is advisory but keep it for parity with the tooling)
- 8N1, no flow control

## Build Targets

| Command | Action |
|---------|--------|
| `make` | Build firmware |
| `make flash` | Build + flash via OpenOCD |
| `make clean` | Remove `build/` |
| `make debug` | Start OpenOCD GDB server |
| `make size` | Show memory usage |

## Debugging with VS Code

1. Install Cortex-Debug extension
2. Connect ST-Link to BlackPill SWD header (see [pin-assignment.md](pin-assignment.md))
3. Press F5 (add a `.vscode/launch.json` modeled on the H7 project's — update target config to `stm32f4x.cfg`)

## Conceptual Reading

Two H7-project docs cover concepts that apply to the F411 port verbatim:

- [../../docs/getting-started.md](../../docs/getting-started.md) — 32-section beginner walkthrough: power-on, startup, linker, clocks, GPIO, HAL, interrupts, DMA, build process, debugging. Almost everything except chip-specific register layouts transfers directly.
- [../../docs/mcu-senior-guide.md](../../docs/mcu-senior-guide.md) — 15-section senior-level deep dive: `volatile` and memory barriers, cache/DMA coherency, ISR concurrency, BASEPRI vs PRIMASK, stack-overflow detection, weak symbols, newlib retargeting, FPU lazy stacking, boot modes, watchdogs, power modes, errata, map-file forensics, HIL testing.
