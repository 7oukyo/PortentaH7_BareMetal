/**
 * @file ina226.h
 * @brief INA226 I2C current/power monitor driver.
 *
 * Connected on I2C3 (PH7=SCL, PH8=SDA) via breakout I2C_0.
 * 7-bit address 0x40 (A0=GND, A1=GND).
 * Shunt resistor: configurable via INA226_SHUNT_OHM (default 0.1 ohm).
 */

#ifndef INA226_H
#define INA226_H

#include "main.h"
#include <stdbool.h>

/* ---- Configuration ---- */

/* 7-bit I2C address (A0=GND, A1=GND = 0b1000000) */
#define INA226_I2C_ADDR         0x40U

/* Shunt resistor value in ohms. Change if your module differs. */
#define INA226_SHUNT_OHM        0.1f

/* Maximum expected current in amps (for calibration register). */
#define INA226_MAX_CURRENT_A    3.2768f

/* ---- INA226 register addresses ---- */

#define INA226_REG_CONFIG       0x00U
#define INA226_REG_SHUNT_V      0x01U   /* Shunt voltage (2.5 uV/bit, signed) */
#define INA226_REG_BUS_V        0x02U   /* Bus voltage (1.25 mV/bit, unsigned) */
#define INA226_REG_POWER        0x03U   /* Power (25x current LSB * bus V) */
#define INA226_REG_CURRENT      0x04U   /* Current (signed, LSB from calibration) */
#define INA226_REG_CALIBRATION  0x05U
#define INA226_REG_MASK_EN      0x06U
#define INA226_REG_ALERT_LIM    0x07U
#define INA226_REG_MFR_ID       0xFEU   /* Manufacturer ID: 0x5449 ("TI") */
#define INA226_REG_DIE_ID       0xFFU   /* Die ID: 0x2260 */

/* ---- Config register bits ---- */

/* Averaging mode (bits 11:9) */
#define INA226_AVG_1            (0U << 9)
#define INA226_AVG_4            (1U << 9)
#define INA226_AVG_16           (2U << 9)
#define INA226_AVG_64           (3U << 9)
#define INA226_AVG_128          (4U << 9)
#define INA226_AVG_256          (5U << 9)
#define INA226_AVG_512          (6U << 9)
#define INA226_AVG_1024         (7U << 9)

/* Bus voltage conversion time (bits 8:6) */
#define INA226_VBUS_140US       (0U << 6)
#define INA226_VBUS_204US       (1U << 6)
#define INA226_VBUS_332US       (2U << 6)
#define INA226_VBUS_588US       (3U << 6)
#define INA226_VBUS_1100US      (4U << 6)
#define INA226_VBUS_2116US      (5U << 6)
#define INA226_VBUS_4156US      (6U << 6)
#define INA226_VBUS_8244US      (7U << 6)

/* Shunt voltage conversion time (bits 5:3) */
#define INA226_VSH_140US        (0U << 3)
#define INA226_VSH_204US        (1U << 3)
#define INA226_VSH_332US        (2U << 3)
#define INA226_VSH_588US        (3U << 3)
#define INA226_VSH_1100US       (4U << 3)
#define INA226_VSH_2116US       (5U << 3)
#define INA226_VSH_4156US       (6U << 3)
#define INA226_VSH_8244US       (7U << 3)

/* Operating mode (bits 2:0) */
#define INA226_MODE_SHUTDOWN    0U
#define INA226_MODE_SH_TRIG    1U
#define INA226_MODE_BUS_TRIG   2U
#define INA226_MODE_SHBUS_TRIG 3U
#define INA226_MODE_SHUTDOWN2  4U
#define INA226_MODE_SH_CONT   5U
#define INA226_MODE_BUS_CONT  6U
#define INA226_MODE_SHBUS_CONT 7U   /* Shunt + bus, continuous */

/* Reset bit (bit 15) */
#define INA226_CONFIG_RESET     (1U << 15)

/* ---- Public API ---- */

/** Initialize I2C3 and configure INA226. Returns false on comm failure. */
bool INA226_Init(void);

/** Read current in milliamps (signed: positive = into VIN+). */
float INA226_ReadCurrent_mA(void);

/** Read bus voltage in millivolts. */
float INA226_ReadBusVoltage_mV(void);

/** Read shunt voltage in microvolts (signed). */
float INA226_ReadShuntVoltage_uV(void);

/** Read power in milliwatts. */
float INA226_ReadPower_mW(void);

/** True if I2C communication failed or device not found. */
bool INA226_IsFault(void);

/** Print all registers and computed values over USB VCP. */
void INA226_PrintDiag(void);

#endif /* INA226_H */
