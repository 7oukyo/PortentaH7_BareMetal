# Portenta H7 Schematic Reference — Bare Metal Firmware

Extracted from official Arduino schematic SL-ABX00042 Rev 4.0 (10/26/2021).
Purpose: single-source hardware truth for Claude Code and manual debugging.

---

## 1. MCU — STM32H747XIH6 (U1)

Package: TFBGA240. Dual core: Cortex-M7 @ 480 MHz, Cortex-M4 @ 240 MHz.
This project uses CM7 only.

### 1.1 Power Pins

| Rail       | Source        |
|------------|---------------|
| VDD (3.3V) | PMIC SW3 (+3V1) |
| VSS (GND)  | GND           |
| VDDA       | PMIC SW3 (+3V1) |
| VSSA       | GND           |
| VREF+      | AREF+ net (Pulled up from SW1) |
| VREF-      | GND           |
| VBAT       | +VBAT from PMIC VSNVS (always-on 3.0V) |
| VCAP       | Internal LDO regulator  |
| VDDLDO     | Internal SMPS regulator |
| VDD33USB   | PMIC SW3 (+3V1) |
| VDD50USB   | PMIC SW3 (+3V1) |
| VDDSMPS    | PMIC SW3 (+3V1) |
| VLXSMPS    | Note: Internal SMPS output feeds VDDLDO |
| VFBSMPS    | Note: Internal SMPS feedback  |
| VDDDSI     | PMIC SW3 (+3V1) |
| VCAPDSI    | PMIC LDO3 (+1V2) |

### 1.2 Clock Inputs

| Clock     | Frequency | MCU Pin | Ball | Source IC          |
|-----------|-----------|---------|------|--------------------|
| HSE       | 25 MHz    | PH0     | J2   | DSC6151HI2B-025 (PMIC SW3 (+3V1), STANDBY=OSCEN) |
| LSE       | 32.768 kHz| PC14    | C2   | SIT1532AI-J4-DCC (PMIC SW3 (+3V1), free-running) |

OSCEN net: directly to 25 Mhz IC STANDBY (pin 1) and 27 Mhz IC STANDBY (pin 1) via pulldown resistors. Active high = oscillator ON. At reset, OSCEN is driven by the MCU (PH1, ball J1).

### 1.3 Boot and Reset

| Signal   | MCU Pin/Ball | Net        | External connection                |
|----------|-------------|------------|------------------------------------|
| NRST     | K1          | NRST       | R1 100k pullup to +3V1, HD connector J1 pin 73, MKR connector RST |
| BOOT0    | E8          | BOOT       | R3 100k pulldown to GND, HD connector J2 pin 1 |
| PDR_ON   | E7          | PDR_ON     | R4 100k pullup to +3V1 (enables power-on reset) |

### 1.4 Debug (SWD/JTAG)

| Signal   | MCU Pin   | Ball | Net          | HD Connector Pin |
|----------|-----------|------|--------------|------------------|
| SWDIO    | PA13      | C15  | TMS/SWD      | J1-75            |
| SWCLK    | PA14      | B14  | TCK/SCK      | J1-77            |
| SWO      | PB3       | C6   | TDO/SWO      | J1-79            |
| TDI      | PA15      | A14  | TDI          | J1-78 (JTAG_TDI) |
| TRST     | PB4       | B7   | TRST         | J1-80            |
| RESET    | NRST      | K1   | NRST         | J1-73            |

Test points: TP29 (PA13/SWDIO), TP30 (PA14/SWCLK), TP31 (PB3/SWO), TP32 (PB4/TRST).

---

## 2. PMIC — NXP PF1550 (U10)

I2C address: **0x08**. Connected on **I2C1** (PB6=SCL, PB7=SDA, AF4).
NOT I2C4. The schematic labels the net "PMIC_SCL" and "PMIC_SDA" which route to PB6/PB7.

### 2.1 I2C Bus Connection

| Signal    | MCU Pin | AF  | Pullup      |
|-----------|---------|-----|-------------|
| PMIC_SCL  | PB6     | AF4 (I2C1_SCL) | 1.8k to +3V1 (SW3) |
| PMIC_SDA  | PB7     | AF4 (I2C1_SDA) | 1.8k to +3V1 (SW3) |

### 2.2 Control Signals

| Signal     | MCU Pin | Controllable by MCU? | Notes |
|------------|---------|----------------------|-------|
| PMIC INT   | PK0     | Output to MCU        | open-drain, active low output. It is asserted when any interrupt occurs, pullup to +3V1 |
| PMIC STBY  | PJ0     | Input from MCU       | Standby signal, floating |
| POR        | NRST    | Output to MCU        | Pulled up to +3V1 and connected to MCU's NRST pin |
| PWRON      | n/a     | n/a                  | Pulled up to PF1550's VNSVS which is always on +3V0 unconfigurable. Also attached to breakout board's push switch |
| ONKEY      | n/a     | n/a                  | U10 pin 8, 100k to VSYS |
| WDI        | n/a     | n/a                  | U10 pin 1, 10k to +3V1 |

### 2.3 Power Output Rails

| PMIC Output | Net Name  | Default Voltage | What It Powers |
|-------------|-----------|-----------------|----------------|
| SW1         | +3V1SW    | +3V3            | SDRAM VDD, Ethernet LAN8742 VDDIO, USB3320 VDDIO VDD3.3 VBAT |
| SW2         | +3V3      | +3V3            | MKR connector VCC (pin 12), HD connector VCC (All External Use), JTAG (ST-Link) VCC |
| SW3         | +3V1      | Always 3.1V Not Configurable | MCU VDD VDDDSI VDDA VDDUSB VDDSMPS, Onboard RGB LED , oscillators, Main power input for all PMIC LDO regulator, Literally everything critical not stated above  |
| VLDO1       | +1V0      | +1V0            | ANX7625 +1V0 (via L7), ANX7625 +1V0A (via L8) |
| VLDO2       | +1V8      | +1V8            | ANX7625 +1V8 / +1V8A (via L5), U18 27MHz oscillator VDD, I2C level shifter U8 VCCB, USB3320 VDD1.8, level shifter U7 VCCA |
| VLDO3       | +1V2      | +1V2            | Schematic shows VLDO3 to +1V2 net. Verify end consumers. |
| VSNVS       | +VBAT     | Always-on 3.0V  | MCU VBAT (B1), coin cell backup, Pull up the PWRON pin. |
| LICELL      | LICELL    | Coin cell input | U10 pin 31, from battery connector J4 pin 1 via C117 1uF |
| VSYS        | VSYS      | _passthrough_   | U10 pins 35/36 output. Fed from VIN via D1 (PMEG6020ER). Powers ONKEY pullup, fuel gauge U19 SYS. |

### 2.4 Power Input

| Input     | U10 Pin | Source                          |
|-----------|---------|---------------------------------|
| SW1IN     | 26      | VSYS (shared with SW2IN, SW3IN) from onbaord USB-C and breakout board +5V clamp connector  |
| SW2IN     | 17      | VSYS                            |
| SW3IN     | 14      | VSYS                            |
| VLDO1IN   | 28      | +3V1 (SW3 output)               |
| VLDO2IN   | 20      | +3V1 (SW3 output)               |
| VLDO3IN   | 11      | +3V1 (SW3 output)               |
| VBUSIN    | 37      | VIN net                         |
| VBATT1/2  | 33/34   | +VBAT                           |

### 2.6 Arduino Bootloader PMIC Settings

| Output | Cold Boot OTP Voltage | Bootloader Configs (Maybe Standby Mode?) |
|--------|-----------------------|--------------------|
| SW1    | +0V                   | +3V0               |
| SW2    | +0V                   | +3V0               |
| SW3    | +3V1                  | +3V1               |
| VLDO1  | +0V                   | +1V0               |
| VLDO2  | +0V                   | +1V8               |
| VLDO3  | +0V                   | +1V2               |

### 2.7 Fuel Gauge — MAX17262 (U19)

I2C address: uses PMIC_SCL/PMIC_SDA nets (same I2C1 bus as PMIC).
Pins: SCL=B1, SDA=C1, ALRT=B2, SYS=B3, BATT=A2, TH=A1, REG=C2.
BATT connects to PMIC_VBATT. SYS connects to VSYS. THM_BAT via R49 0R to THM net then R52 0R to U10 THM pin 32.

---

## 3. I2C Bus Map

### 3.1 I2C1 (PB6/PB7) — PMIC and Level-Shifted Device Bus

| Device                | Address | Voltage Level | Notes                          |
|-----------------------|---------|---------------|--------------------------------|
| NXP PF1550 (U10)      | 0x08    | 3.3V direct   | PMIC                           |
| MAX17262 (U19)        | 0x36    | 3.3V direct   | Fuel gauge (on same PMIC_SCL/SDA nets) |
| SE050C2 (U11)         | 0x48    | 1.8V via U8   | Secure element, behind level shifter |
| ATECC608A (U16)       | 0x60    | 1.8V via U8   | Crypto auth, behind level shifter |
| ANX7625 (U6)          | 0x54, 0x58, 0x70, 0x72, 0x7A, 0x7E, 0x84 | 1.8V via U8 | USB-C video bridge, multiple register pages |

Level shifter U8 (FXMA2102L8X): VCCA=+3V1 (MCU/PMIC side), VCCB=+1V8 (crypto/ANX side). OE tied to +3V1.
Pullups on 1.8V and 3.3V side.

### 3.2 I2C3 (PH7=SCL, PH8=SDA) — General Purpose

Directly connected to HD connector I2C0: J1-46 (SCL), J1-44 (SDA).
No on-board devices.

### 3.3 I2C4 (PH11=SCL, PH12=SDA) — Camera/Sensor

Directly connected to HD connector I2C2: J2-47 (SCL), J2-45 (SDA).
No on-board devices. Shared pins with CAM interface.

---

## 4. SPI Buses

### 4.1 SPI2 (directly to HD connector)

| Signal | MCU Pin | Ball | HD Connector |
|--------|---------|------|--------------|
| CK     | PI1     | A15  | J2-37 (SPI0_CK) |
| MISO   | PC2     | M3   | J2-39 (SPI0_MISO) |
| MOSI   | PC3     | M4   | J2-41 (SPI0_MOSI) |
| CS     | PI0     | A16  | J2-35 (SPI0_CS) |

Shared pins: PC2/PC3 shared with ANALOG and SAI2A.

### 4.2 Internal QSPI — Flash (U2 MX25L12833FZ2I-10G)

| Signal  | MCU Pin |
|---------|---------|
| CS      | PG6     |
| CLK     | PF10    |
| SI/SO0  | PD11    |
| SO/SO1  | PD12    |
| WP/SO2  | PF7     |
| HOLD/SO3| PD13    |

16 Mbit (2 MB) NOR flash. VCC from +3V1 (SW3).

---

## 5. UART/USART Connections

| UART Instance | TX Pin  | RX Pin  | RTS Pin | CTS Pin | Primary Use             | HD Connector Pins |
|---------------|---------|---------|---------|---------|-------------------------|-------------------|
| UART4         | PA0     | PI9     | PI10    | PI13    | HD SERIAL0              | J1-34, J1-36, J1-38, J1-40 |
| LPUART1       | PA9     | PA10    | PI14    | PI15    | HD SERIAL1 / MKR UART   | J1-33, J1-35, J1-37, J1-39 |
| UART6         | PG14    | PG9     | n/a     | n/a     | HD SERIAL2              | J2-26, J2-28, J2-30, J2-32 |
| UART8         | PJ8     | PJ9     | n/a     | n/a     | HD SERIAL3              | J2-25, J2-27, J2-29, J2-31 |
| UART7         | PA15    | PF6     | PF8     | PF9     | WiFi/BLE module BT UART | Internal only     |

---

## 6. CAN

HD connector CAN pins:

| HD Pin | Signal    | MCU Pin | Peripheral | Notes |
|--------|-----------|---------|------------|-------|
| J1-49  | CAN1_TX   | PH13    | FDCAN1_TX  | Available — routed on breakout board |
| J1-51  | CAN1_RX   | PB8     | FDCAN1_RX  | Available — routed on breakout board |
| J1-50  | CAN0_TX   | —       | —          | Breakout board label only — NOT connected on Portenta schematic |
| J1-52  | CAN0_RX   | —       | —          | Breakout board label only — NOT connected on Portenta schematic |

Only CAN1 (PH13/PB8, FDCAN1) is usable. CAN0 pins on the breakout board are dummy — no MCU connection.

---

## 7. USB

### 7.1 USB_FS (Full Speed) — MCU Internal PHY

| Signal | MCU Pin | Ball | Net         | HD Connector |
|--------|---------|------|-------------|--------------|
| D+     | PA12    | E16  | USB0_D_P    | J1-26 via R44 0R |
| D-     | PA11    | E17  | USB0_D_N    | J1-28 via R45 0R |

Test points: TP1 (USB0_D_P), TP2 (USB0_D_N).

### 7.2 USB_HS (High Speed) — External ULPI PHY (U3 USB3320C-EZK) to onboard USB-C

| ULPI Signal | MCU Pin | Ball | U3 Pin |
|-------------|---------|------|--------|
| DIR         | PI11    | R1   | 31     |
| NXT         | PH4     | P3   | 2      |
| STP         | PC0     | L2   | 29     |
| CLK         | PA5     | T3   | 1      |
| D0          | PA3     | U2   | 3      |
| D1          | PB0     | U5   | 4      |
| D2          | PB1     | T5   | 5      |
| D3          | PB10    | P11  | 6      |
| D4          | PB11    | P12  | 7      |
| D5          | PB12    | T14  | 9      |
| D6          | PB13    | U14  | 10     |
| D7          | PB5     | A5   | 13     |

USB3320 REFCLK (pin 26) from 27 MHz oscillator net.
HSU_D_P/HSU_D_N routed to HD J1-25/J1-27 and USB-C J3 via ESD filters.

### 7.3 USB-C Connector (J3 CX90B1-24P)

Full USB-C with CC1/CC2, SBU1/SBU2, SuperSpeed lanes. Routes through:

- ESD filters: U12, U13, U14 (PCMF2USB3B/CZ)
- Video bridge: U6 (ANX7625) for DisplayPort alt mode
- USB switch: U15 (NX18P3001UKZ) for VBUS/OTG control
- OTG_EN: PJ6 (N15) via SJ6 0R controls U15 EN pin

---

## 8. Ethernet — LAN8742AI-CZ (U5)

RMII interface.

| Signal   | MCU Pin | Ball | U5 Pin |
|----------|---------|------|--------|
| MDIO     | PA2     | N3   | 12     |
| MDC      | PC1     | M2   | 13     |
| RXD0     | PC4     | T4   | 8      |
| RXD1     | PC5     | U4   | 7      |
| CRS_DV   | PA7     | R5   | 11     |
| TXEN     | PG11    | B9   | 16     |
| TXD0     | PG13    | D9   | 17     |
| TXD1     | PG12    | C9   | 18     |
| REF_CLK  | PA1     | N4   | 14     |

No external crystal. REF_CLK mode (50 MHz from MCU or LAN8742 internal).
Reset: R6 10k + R29 1.8k RC circuit on pin 15 (nRST).
Power: VDD1A/VDD2A/VDDCR from +3V1SW, VDDIO from +3V1SW.
Ethernet magnetics via HD connector J1 pins 1-20 (differential pairs A/B/C/D + LEDs).

---

## 9. WiFi/BLE — LBEE5KL1DX (U9)

Murata Type 1DX module (CYW4343W inside).

### 9.1 SDIO Interface (WiFi Data)

Uses SDMMC1. MCU pins: PC8(D0), PC9(D1), PC10(D2), PC11(D3), PC12(CLK), PD2(CMD).

### 9.2 HCI UART Interface (Bluetooth)

Connected to UART7.

### 9.3 Control Signals

| Signal       | MCU Pin | Net            | U9 Pin |
|--------------|---------|----------------|--------|
| WL_ON        | PJ1     | WiFi CTRL ON   | 28     |
| HOST_WAKE    | PJ5     | WiFi CTRL HOST WAKE | 27  |
| BT_ON        | PJ12    | BLE CTRL ON    | 14     |
| DEVICE_WAKE  | PJ14    | BLE CTRL DEVICE WAKE | 39  |
| HOST_WAKE(BT)| PJ13    | BLE CTRL HOST WAKE | 38   |

### 9.4 Miscellaneous

- 32.768 kHz LPO_IN (pin 37) from Y1 oscillator.
- Antenna: U.FL J6 to chip antenna ANT1 (2450AT42E0100E) via R27 0R.
- Power: VBAT (pin 30) from +3V1 via L6 + C89 2.2uF. VIO (pin 36) from +3V1SW.

---

## 10. Video Bridge — ANX7625 (U6)

USB-C to MIPI-DSI bridge. Sheet 3.

### 10.1 MIPI-DSI Input (from MCU)

| Signal     | MCU Pin    | Ball    | U6 Pin |
|------------|------------|---------|--------|
| DSI_CK_P   | DSI_CKP    | L16     | G8     |
| DSI_CK_N   | DSI_CKN    | L17     | H8     |
| DSI_D0_P   | DSI_D0P    | M16     | E8     |
| DSI_D0_N   | DSI_D0N    | M17     | F8     |
| DSI_D1_P   | DSI_D1P    | K16     | H7     |
| DSI_D1_N   | DSI_D1N    | K17     | H6     |

### 10.2 Control (via U7 TXB0108YZPR level shifter, 3.3V to 1.8V)

| Function    | MCU Pin (3.3V) | Level-shifted | U6 Pin |
|-------------|----------------|---------------|--------|
| RESET_N     | PJ3            | RESETN_1V8    | B7     |
| POWER_EN    | PK2            | VIDEO_EN_1V8  | D2     |
| CABLE_DET   | PK3            | VIDEO_CABLE1V8| C3     |
| ALT mode    | PK4            | VIDEO_ALT1V8  | B8     |

### 10.3 Power

From PMIC's LDO and SW3 +3V1.

---

## 11. SDRAM — AS4C4M16SA-6BIN (U4)

8 MB (4M x 16-bit) SDRAM via FMC.

| Signal Group | MCU Pins |
|--------------|----------|
| Address A0-A11 | PF0-PF5 (A0-A5), PF12-PF15 (A6-A9), PG0-PG1 (A10-A11) |
| Data D0-D15  | PD14, PD15, PD1, PD0, PE7-PE15, PD8-PD10 |
| BA0/BA1      | PG4/PG5  |
| CAS#         | PG15     |
| RAS#         | PF11     |
| WE#          | PH5      |
| CKE          | PH2      |
| CLK          | PG8      |
| CS#          | PH3      |
| DQML/DQMH    | PE0/PE1  |

Power: VDD/VDDQ from +3V1SW (SW1).

---

## 12. Secure Element and Crypto

### 12.1 SE050C2HQ1 (U11)

I2C address: 0x48. On 1.8V side of level shifter U8.
ENA pin: CRYPTO_EN from PI12 (ball H1).
VIN: +3V1. Decoupling: C1 100nF, C131 100nF.

### 12.2 ATECC608A-MAHDA (U16)

On same 1.8V I2C bus. VCC: +3V1. Default I2C address.

---

## 13. Oscillators Summary

| Designator | Part Number              | Frequency  | VDD         | STANDBY/Enable | Consumers |
|------------|--------------------------|------------|-------------|----------------|-----------|
| Y1         | SIT1532AI-J4-DCC-32.768E | 32.768 kHz | +3V1 (SW3)  | Always on      | MCU LSE (PC14), WiFi LPO_IN |
| U17        | DSC6151HI2B-025.0000     | 25 MHz     | +3V1 (SW3)  | OSCEN (high=on)| MCU HSE (PH0), LAN8742AI    |
| U18        | DSC6151HI2B-027.0000T    | 27 MHz     | +1V8 (LDO2) | OSCEN (high=on)| ANX7625, USB3320 REFCLK     |

---

## 14. LEDs

RGB LED DL1 (SMLP34RGB2W3) — active low (sink current through MCU pins).

| Color | MCU Pin | Ball | Resistor | Anode |
|-------|---------|------|----------|-------|
| Red   | PK5     | A8   | R12 330R | +3V1 (SW3) |
| Green | PK6     | C7   | R11 330R | +3V1 (SW3) |
| Blue  | PK7     | D7   | R13 330R | +3V1 (SW3) |

Drive pin LOW to illuminate. HIGH = off.

Charge LED DL2 (HSMD-C190): PMIC CHGB output (U10 pin 40), R14 330R. Active low.

---

## 15. HD Connector Pin Map

### J1 — DF40C-80DP-0.4V(51)-LEFT

| Pin | Signal          | MCU Pin  | Pin | Signal          | MCU Pin  |
|-----|-----------------|----------|-----|-----------------|----------|
| 1   | ETH_A_P         | (PHY)    | 2   | DSI_D3_P        | n/a      |
| 3   | ETH_A_N         | (PHY)    | 4   | DSI_D3_N        | n/a      |
| 5   | ETH_B_P         | (PHY)    | 6   | DSI_D2_P        | n/a      |
| 7   | ETH_B_N         | (PHY)    | 8   | DSI_D2_N        | n/a      |
| 9   | ETH_C_P         | (PHY)    | 10  | DSI_D1_P        | n/a      |
| 11  | ETH_C_N         | (PHY)    | 12  | DSI_D1_N        | n/a      |
| 13  | ETH_D_P         | (PHY)    | 14  | DSI_D0_P        | n/a      |
| 15  | ETH_D_N         | (PHY)    | 16  | DSI_D0_N        | n/a      |
| 17  | ETH_LED1        | (PHY)    | 18  | DSI_CK_P        | n/a      |
| 19  | ETH_LED2        | (PHY)    | 20  | DSI_CK_N        | n/a      |
| 21  | VIN             | Power    | 22  | GND             | n/a      |
| 23  | USB1_VBUS       | VBUS_USBC| 24  | USB0_VBUS       | n/a      |
| 25  | USB1_D_P        | HSU_D_P  | 26  | USB0_D_P        | PA12     |
| 27  | USB1_D_N        | HSU_D_N  | 28  | USB0_D_N        | PA11     |
| 29  | USB1_ID         | PJ6      | 30  | USB0_ID         | n/a      |
| 31  | GND             | n/a      | 32  | VIN             | Power    |
| 33  | SERIAL1_TX      | PA9      | 34  | SERIAL0_TX      | PA0      |
| 35  | SERIAL1_RX      | PA10     | 36  | SERIAL0_RX      | PI9      |
| 37  | SERIAL1_RTS     | PI14     | 38  | SERIAL0_RTS     | PI10     |
| 39  | SERIAL1_CTS     | PI15     | 40  | SERIAL0_CTS     | PI13     |
| 41  | VIN             | Power    | 42  | GND             | n/a      |
| 43  | I2C1_SDA        | PB7      | 44  | I2C0_SDA        | PH8      |
| 45  | I2C1_SCL        | PB6      | 46  | I2C0_SCL        | PH7      |
| 47  | GND             | n/a      | 48  | VIN             | Power    |
| 49  | CAN1_TX         | PH13     | 50  | CAN0_TX         | n/a (dummy) |
| 51  | CAN1_RX         | PB8      | 52  | CAN0_RX         | n/a (dummy) |
| 53  | VSYS            | VSYS     | 54  | GND             | n/a      |
| 55  | SDC_CLK         | PD6      | 56  | I2S_CK          | PD3      |
| 57  | SDC_CMD         | PD7      | 58  | I2S_WS          | PB9      |
| 59  | SDC_D0          | PB14     | 60  | I2S_SDI         | PI2      |
| 61  | SDC_D1          | PB15     | 62  | I2S_SDO         | PI3      |
| 63  | SDC_D2          | PB3      | 64  | VSYS            | VSYS     |
| 65  | SDC_D3          | PB4      | 66  | PDM_CK          | PE2      |
| 67  | SDC_CD          | n/a      | 68  | PDM_D0          | PB2      |
| 69  | SDC_WP          | n/a      | 70  | PDM_D1          | PE3      |
| 71  | SDC_RST         | n/a      | 72  | VCC             | +3V3     |
| 73  | JTAG_RESET      | NRST     | 74  | SPDIF_TX        | n/a      |
| 75  | JTAG_TMS/SWD    | PA13     | 76  | SPDIF_RX        | n/a      |
| 77  | JTAG_TCK/SCK    | PA14     | 78  | JTAG_TDI        | PA15     |
| 79  | JTAG_TDO/SWO    | PB3      | 80  | JTAG_TRST       | PB4      |

### J2 — DF40C-80DP-0.4V(51)-RIGHT

| Pin | Signal          | MCU Pin  | Pin | Signal          | MCU Pin  |
|-----|-----------------|----------|-----|-----------------|----------|
| 1   | BOOT            | BOOT0    | 2   | CAM_D7/D3_P     | PI7      |
| 3   | BT_SEL          | n/a      | 4   | CAM_D6/D3_N     | PH14     |
| 5   | PWRON           | PK1      | 6   | CAM_D5/D2_P     | PH12     |
| 7   | LICELL          | LICELL   | 8   | CAM_D4/D2_N     | PH11     |
| 9-21| PCIE (NC)       | n/a      | 10  | CAM_D3/D1_P     | PH10     |
|     |                 |          | 12  | CAM_D2/D1_N     | PH9      |
|     |                 |          | 14  | CAM_D1/D0_P     | PI6      |
|     |                 |          | 16  | CAM_D0/D0_N     | PI5      |
|     |                 |          | 18  | CAM_VS/CK_P     | PI4      |
|     |                 |          | 20  | CAM_CK/CK_N     | PA6      |
|     |                 |          | 22  | CAM_HS          | PA4      |
| 23  | VCC             | +3V3     | 24  | GND             | n/a      |
| 25  | SERIAL3_TX      | PJ8      | 26  | SERIAL2_TX      | PG14     |
| 27  | SERIAL3_RX      | PJ9      | 28  | SERIAL2_RX      | PG9      |
| 29  | SERIAL3_RTS     | n/a      | 30  | SERIAL2_RTS     | PG12     |
| 31  | SERIAL3_CTS     | n/a      | 32  | SERIAL2_CTS     | PG13     |
| 33  | GND             | n/a      | 34  | VCC             | +3V3     |
| 35  | SPI0_CS         | PI0      | 36  | SPI1_CS         | PC13     |
| 37  | SPI0_CK         | PI1      | 38  | SPI1_CK         | PC15     |
| 39  | SPI0_MISO       | PC2      | 40  | SPI1_MISO       | PD4      |
| 41  | SPI0_MOSI       | PC3      | 42  | SPI1_MOSI       | PD5      |
| 43  | VCC             | +3V3     | 44  | GND             | n/a      |
| 45  | I2C2_SDA        | PH12     | 46  | GPIO_0          | PC13     |
| 47  | I2C2_SCL        | PH11     | 48  | GPIO_1          | PC15     |
| 49  | SAI_CK          | PE5      | 50  | GPIO_2          | PD4      |
| 51  | SAI_FS          | PE4      | 52  | GPIO_3          | PD5      |
| 53  | SAI_D0          | PE6      | 54  | GPIO_4          | PE3      |
| 55  | SAI_D1          | PI5      | 56  | GPIO_5          | PG3      |
| 57  | GND             | n/a      | 58  | GPIO_6          | PG10     |
| 59  | PWM_0           | PA8      | 60  | PWM_5           | PK1      |
| 61  | PWM_1           | PC6      | 62  | PWM_6           | PH15     |
| 63  | PWM_2           | PC7      | 64  | PWM_7           | PJ7      |
| 65  | PWM_3           | PG7      | 66  | PWM_8           | PJ10     |
| 67  | PWM_4           | PJ11     | 68  | PWM_9           | PH6      |
| 69  | VCC             | +3V3     | 70  | GND             | n/a      |
| 71  | ANALOG_VREF_P   | AREF+    | 72  | ANALOG_VREF_N   | GND      |
| 73  | ANALOG_A0       | PA0_C    | 74  | ANALOG_A4       | PA4      |
| 75  | ANALOG_A1       | PA1_C    | 76  | ANALOG_A5       | PA6      |
| 77  | ANALOG_A2       | PC2_C    | 78  | ANALOG_A6       | PC2      |
| 79  | ANALOG_A3       | PC3_C    | 80  | ANALOG_A7       | PC3      |

---

## 16. Shared Pin Conflicts

| MCU Pin | Bus 1 Function   | Bus 2 Function  |
|---------|-------------------|-----------------|
| PB3     | JTAG (TDO/SWO)   | SDC2 (D2)       |
| PH12    | CAM (D4/D2_N)    | I2C4 SCL / I2C2_SDA on HD |
| PH11    | CAM (D3/D1_P)    | I2C4 SDA / I2C2_SCL on HD |
| PA6     | CAM (CK/CK_N)    | ANALOG A5       |
| PA4     | CAM (HS)          | ANALOG A4       |
| PC3     | ANALOG A7         | SPI2 MOSI       |
| PC2     | ANALOG A6         | SPI2 MISO       |
| PI6     | SAI2A             | CAM (D1/D0_P)   |
| PI5     | SAI2A             | CAM (D0/D0_N)   |
| PI7     | SAI2A             | CAM (D7/D3_P)   |
| PB4     | JTAG (TRST)       | SDC2 (D3)       |

---

## 17. GPIO Direct Pins (HD J2)

| HD Pin | Signal  | MCU Pin |
|--------|---------|---------|
| 46     | GPIO_0  | PC13    |
| 48     | GPIO_1  | PC15    |
| 50     | GPIO_2  | PD4     |
| 52     | GPIO_3  | PD5     |
| 54     | GPIO_4  | PE3     |
| 56     | GPIO_5  | PG3     |
| 58     | GPIO_6  | PG10    |

---

## 18. PWM Pins (HD J2)

| HD Pin | Signal  | MCU Pin | Timer AF        |
|--------|---------|---------|-----------------|
| 59     | PWM_0   | PA8     | TIM1_CH1, HRTIM_CHB2           |
| 61     | PWM_1   | PC6     | HRTIM_CHA1, TIM3_CH1, TIM8_CH1 |
| 63     | PWM_2   | PC7     | HRTIM_CHA2, TIM3_CH2, TIM8_CH2 |
| 65     | PWM_3   | PG7     | HRTIM_CHE2                     |
| 67     | PWM_4   | PJ11    | TIM1_CH2, TIM8_CH2N            |
| 60     | PWM_5   | PK1     | TIM1_CH1, TIM8_CH3N            |
| 62     | PWM_6   | PH15    | TIM8_CH3N                      |
| 64     | PWM_7   | PJ7     | TIM8_CH2N                      |
| 66     | PWM_8   | PJ10    | TIM1_CH2N, TIM8_CH2            |
| 68     | PWM_9   | PH6     | TIM12_CH1                      |

---

## 19. ADC/Analog Pins (HD J2)

| HD Pin | Signal      | MCU Pin | ADC Channel (**Not verify**) |
|--------|-------------|---------|----------------------|
| 73     | ANALOG_A0   | PA0_C   | ADC1/2_INP0 (direct) |
| 75     | ANALOG_A1   | PA1_C   | ADC1/2_INP1 (direct) |
| 77     | ANALOG_A2   | PC2_C   | ADC3_INP0 (direct)   |
| 79     | ANALOG_A3   | PC3_C   | ADC3_INP1 (direct)   |
| 74     | ANALOG_A4   | PC2     | ADC1/2_INP18 (shared CAM_HS) |
| 76     | ANALOG_A5   | PC3     | ADC1/2_INP3 (shared CAM_CK) |
| 78     | ANALOG_A6   | PA4     | ADC1/2_INP12 (shared SPI2) |
| 80     | ANALOG_A7   | PA6     | ADC1/2_INP13 (shared SPI2) |
| 71     | VREF_P      | AREF+   | R34 0R from +3V1SW, C96 2.2uF |
| 72     | VREF_N      | GND     | n/a         |

_C suffix pins bypass GPIO mux for best ADC performance.

---

## 20. Power Architecture Summary

```
USB-C (J3) or VIN (J4 battery connector)
    |
    v
D1 PMEG6020ER (reverse polarity protection)
    |
    v
VSYS --------------------------------+
    |                                |
    v                                v
PF1550 SW1IN/SW2IN/SW3IN          U19 MAX17262 (fuel gauge)
    |
    +-- SW1 -> +3V1SW -> SDRAM VDD, Ethernet LAN8742 VDDIO, USB3320 VDDIO VDD3.3 VBAT
    +-- SW2 -> +3V3   -> MKR connector VCC (pin 12), HD connector VCC (All External Use), JTAG (ST-Link) VCC
    +-- SW3 -> +3V1   -> MCU VDD VDDDSI VDDA VDDUSB VDDSMPS, Onboard RGB LED , oscillators, Main power input for all PMIC LDO regulator, Literally everything critical not stated above
    |    |
    |    +-- VLDO1IN <- +3V1
    |    +-- VLDO2IN <- +3V1
    |    +-- VLDO3IN <- +3V1
    |
    +-- VLDO1 -> +1V0  -> ANX7625 core
    +-- VLDO2 -> +1V8  -> ANX7625 I/O, 27MHz osc, USB3320, level shifters
    +-- VLDO3 -> +1V2  -> TBD
    +-- VSNVS -> +VBAT -> MCU VBAT (always-on 3.0V)
```

---

## 21. Init Sequence for Bare Metal (No Bootloader)

Firmware is linked at 0x08000000 (no bootloader offset). The vector table and Reset_Handler
in startup .s file handle stack pointer init and .bss/.data setup before calling main().
The PMIC SW3 rail (+3V1) is always on from OTP defaults — the MCU, oscillators, and I2C
pullups are powered before any firmware runs. SW1/SW2/LDOs start at 0V from OTP and must
be configured by firmware.

**Verified working sequence (matches reference project and cold-boot tested):**

| Step | Action | Why (from schematic) |
|------|--------|----------------------|
| 1 | **Drive PJ0 LOW** (PMIC_STBY) — enable GPIOJ clock, write LOW, configure as push-pull output. | PJ0 is wired to PF1550 STBY pin (§2.2). Must be LOW for PMIC RUN mode. If floating or HIGH, PMIC enters standby and drops SW1/SW2. Do this before HAL_Init so PMIC stays in RUN from the earliest moment. |
| 2 | **HAL_Init()** | Configures SysTick, NVIC priority grouping, internal HAL state. Runs on OTP-default voltages (SW3 +3V1 is already up). Does NOT require PMIC to be configured first. |
| 3 | **Drive PH1 HIGH** (OSCEN) — enable GPIOH clock, configure PH1 as push-pull with pullup, delay 10ms, set HIGH. | PH1 ball J1 drives the OSCEN net (§1.2) which gates the STANDBY pin on the 25 MHz (HSE) and 27 MHz (USB) oscillators. Active HIGH = oscillators running. Must be HIGH before SystemClock_Config uses HSE as PLL source. The 10ms delay allows oscillator startup. |
| 4 | **SystemClock_Config()** — HSE 25 MHz → PLL1 → 480 MHz SYSCLK for CM7. | Configures PWR supply mode (SMPS+LDO), voltage scaling, PLL1 with HSE as source. After this, AHB/APB clocks are at their final frequencies. I2C timing values depend on these clocks being set first. |
| 5 | **PeriphCommonClock_Config()** — PLL2 for SPI/peripheral clocks. | Sets up PLL2 (133 MHz out) used by SPI peripherals. Independent of PMIC but must follow SystemClock_Config. |
| 6 | **GPIO init** — configure LED pins (PK5=Red, PK6=Green, PK7=Blue) as push-pull outputs, set HIGH (LEDs are active-low). | RGB LED on Portenta is active-low, powered from +3V1 (§14). HIGH = off. Init before PMIC so any PMIC failure is visible (LED stays off, not randomly lit). |
| 7 | **I2C1 init + PMIC_Init()** — configure PB6/PB7 as AF4 open-drain with pullups, init HAL I2C1 at 100 kHz (timing 0x307075B1 for 120 MHz D2PCLK1). Then write PMIC registers. | I2C1 bus (§3.1) has 1.8k external pullups to +3V1. Timing value is valid only after SystemClock_Config sets D2PCLK1 to 120 MHz. PMIC address is 0x08 (7-bit). Write SW1=+3V3, SW2=+3V3, enable LDO1/2/3 as needed. |
| 8 | **Remaining peripheral init** — UART, SPI, Ethernet, SDRAM, etc. as needed. | SW1 (+3V1SW) must be up before Ethernet (LAN8742 §8), SDRAM (§11), or USB3320 (§7.2) can operate. SW2 powers external connectors. LDO2 (+1V8) needed for secure elements and ANX7625. |

### 21.1 What Boots Without Firmware Intervention

From PMIC OTP defaults alone (no register writes):

| Rail | State at Power-On | Consequence |
|------|-------------------|-------------|
| SW3 (+3V1) | **ON, +3.1V** | MCU core, oscillators, I2C pullups, LED anode — all powered. MCU can boot and execute code. |
| VSNVS (+VBAT) | **ON, +3.0V** | MCU VBAT, always on. |
| SW1 (+3V1SW) | **OFF (0V)** | No SDRAM, no Ethernet, no USB3320 VDDIO. |
| SW2 (+3V3) | **OFF (0V)** | No HD/MKR connector VCC, no ST-Link VCC from board. |
| VLDO1 (+1V0) | **OFF (0V)** | No ANX7625 core. |
| VLDO2 (+1V8) | **OFF (0V)** | No ANX7625 I/O, no 27 MHz oscillator, no level shifters, no USB3320 VDD1.8. |
| VLDO3 (+1V2) | **OFF (0V)** | Verify consumers. |

This means: on a true cold boot (no Arduino bootloader having configured PMIC first), only the MCU itself and I2C1 are functional until firmware runs PMIC_Init() at step 7.

### 21.2 Critical Gotchas

- **PJ0 (PMIC_STBY) must be driven LOW immediately.** It floats at reset. If the PMIC interprets it as HIGH, it enters standby mode and may drop SW3 — bricking the MCU until power cycle.
- **PH1 (OSCEN) must be driven HIGH before clock config.** The HSE oscillator has a STANDBY pin gated by this GPIO. If PH1 is not set, HSE won't oscillate and PLL lock will fail or hang.
- **I2C timing depends on clock config.** The timing register value 0x307075B1 is valid for D2PCLK1 = 120 MHz (after SystemClock_Config). Using it before clock config gives wrong SCL frequency.
- **Do NOT init PMIC before HAL_Init.** Early bare-register I2C attempts failed on cold boot because the I2C peripheral clock tree wasn't properly configured. The working sequence is always: HAL_Init → clocks → I2C → PMIC.
- **CM4 core is disabled by fuse on Portenta H7.** Must be explicitly enabled via `HAL_RCCEx_EnableBootCore(RCC_BOOT_C2)`. Skip this if no CM4 firmware exists — enabling it runs stale flash bank 2 code (old Arduino bootloader) which interferes with CM7.

---

## 22. Key Test Points (**Not Verified**)

| TP  | Signal      | Notes |
|-----|-------------|-------|
| TP1 | USB0_D_P    | USB FS D+ |
| TP2 | USB0_D_N    | USB FS D- |
| TP4 | +1V2        | VLDO3 output |
| TP5 | GND         | Ground reference |
| TP6 | DSI/SAI     | Video/audio |
| TP7 | DSI/SAI     | Video/audio |
| TP8-TP12 | WiFi/BLE | BT_GPIO test points |
| TP13| LICELL      | Coin cell voltage |
| TP14| VIN         | Input voltage |
| TP15| VSYS        | System voltage |
| TP16| PWRON       | PMIC power-on |
| TP17| +3V1        | SW3 output |
| TP18| VDDOTP      | PMIC OTP voltage |
| TP24| INT2P7      | PMIC interrupt |
| TP27| POR         | Power-on reset |
| TP29| PA13/SWDIO  | SWD debug |
| TP30| PA14/SWCLK  | SWD debug |
| TP31| PB3/SWO     | Debug trace |
| TP32| PB4/TRST    | Debug JTAG |
| TP33| VBUS_USBC   | USB-C VBUS |

---

## 23. Power Nets Quick Reference (**Not Verified**)

| Net Name | Voltage | Source | Notes |
|----------|---------|-------|-------|
| VIN      | 4.1-6V  | USB-C or J4 | Board input, through D1 to VSYS |
| VSYS     | ~VIN    | D1 output | PMIC buck input, fuel gauge |
| +3V1SW   | 3.3V    | PMIC SW1 | SDRAM VDD, Ethernet VDDIO, USB3320 VDDIO |
| +3V3     | 3.3V    | PMIC SW2 | HD/MKR VCC, JTAG VCC |
| +3V1     | 3.1V    | PMIC SW3 | MCU VDD/VDDA/VDDUSB, oscillators, LEDs, LDO input |
| +1V0     | 1.0V    | PMIC VLDO1 | ANX7625 core |
| +1V8     | 1.8V    | PMIC VLDO2 | ANX7625 I/O, USB3320 |
| +1V2     | 1.2V    | PMIC VLDO3 | TBD |
| +VREFDDR | ~1.65V  | PMIC VREFDDR | SDRAM |
| +VBAT    | 3.0V    | PMIC VSNVS | MCU VBAT, always on |
| VBUS_USBC| 5V      | USB-C VBUS | USB power, OTG |
| AREF+    | 3.1V    | R34 0R from +3V1SW | ADC reference |
