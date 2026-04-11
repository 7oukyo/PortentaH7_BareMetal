/**
 * @file ina226.c
 * @brief INA226 I2C current/power monitor driver.
 *
 * I2C3 (PH7=SCL, PH8=SDA) on breakout I2C_0. Address 0x40.
 * Shunt resistor: INA226_SHUNT_OHM (default 0.1 ohm).
 * Calibration register computed so Current register reads in mA directly.
 */

#include "ina226.h"
#include "usbd_cdc_if.h"
#include <string.h>

static I2C_HandleTypeDef hi2c3;
static bool i2c_fault = false;

/* Current LSB in amps, computed during init. */
static float current_lsb_a = 0.0f;

/* Power LSB = 25 * current_lsb (INA226 datasheet). */
static float power_lsb_w = 0.0f;

/* INA226 I2C address shifted for HAL (7-bit addr << 1). */
#define INA226_ADDR_HAL  (INA226_I2C_ADDR << 1)

/* I2C timeout in ms. */
#define INA226_I2C_TIMEOUT  100U

/* ---- I2C3 initialization ---- */

/* Init I2C3 peripheral for INA226 communication.
 * I2C3 kernel clock = D2PCLK1 (120 MHz). Same timing as I2C1 (PMIC). */
static bool ina226_i2c_init(void)
{
    hi2c3.Instance              = I2C3;
    hi2c3.Init.Timing           = 0x307075B1U;  /* 120 MHz D2PCLK1, ~400 kHz */
    hi2c3.Init.OwnAddress1      = 0;
    hi2c3.Init.AddressingMode   = I2C_ADDRESSINGMODE_7BIT;
    hi2c3.Init.DualAddressMode  = I2C_DUALADDRESS_DISABLE;
    hi2c3.Init.OwnAddress2      = 0;
    hi2c3.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
    hi2c3.Init.GeneralCallMode  = I2C_GENERALCALL_DISABLE;
    hi2c3.Init.NoStretchMode    = I2C_NOSTRETCH_DISABLE;

    if (HAL_I2C_Init(&hi2c3) != HAL_OK) {
        return false;
    }
    /* Analog filter enabled, digital filter off. */
    if (HAL_I2CEx_ConfigAnalogFilter(&hi2c3, I2C_ANALOGFILTER_ENABLE) != HAL_OK) {
        return false;
    }
    if (HAL_I2CEx_ConfigDigitalFilter(&hi2c3, 0) != HAL_OK) {
        return false;
    }
    return true;
}

/* ---- Register access (16-bit big-endian) ---- */

/* Write 16-bit value to INA226 register. */
static bool ina226_write_reg(uint8_t reg, uint16_t val)
{
    uint8_t buf[3];
    buf[0] = reg;
    buf[1] = (uint8_t)(val >> 8);    /* MSB first */
    buf[2] = (uint8_t)(val & 0xFF);
    if (HAL_I2C_Master_Transmit(&hi2c3, INA226_ADDR_HAL, buf, 3,
                                 INA226_I2C_TIMEOUT) != HAL_OK) {
        i2c_fault = true;
        return false;
    }
    return true;
}

/* Read 16-bit value from INA226 register. */
static bool ina226_read_reg(uint8_t reg, uint16_t *val)
{
    uint8_t tx = reg;
    uint8_t rx[2];
    if (HAL_I2C_Master_Transmit(&hi2c3, INA226_ADDR_HAL, &tx, 1,
                                 INA226_I2C_TIMEOUT) != HAL_OK) {
        i2c_fault = true;
        return false;
    }
    if (HAL_I2C_Master_Receive(&hi2c3, INA226_ADDR_HAL, rx, 2,
                                INA226_I2C_TIMEOUT) != HAL_OK) {
        i2c_fault = true;
        return false;
    }
    *val = ((uint16_t)rx[0] << 8) | rx[1];
    return true;
}

/* ---- Public API ---- */

bool INA226_Init(void)
{
    i2c_fault = false;

    if (!ina226_i2c_init()) {
        i2c_fault = true;
        return false;
    }

    /* Verify device: read Manufacturer ID (should be 0x5449 = "TI") */
    uint16_t mfr_id = 0;
    if (!ina226_read_reg(INA226_REG_MFR_ID, &mfr_id) || mfr_id != 0x5449U) {
        i2c_fault = true;
        return false;
    }

    /* Reset device */
    if (!ina226_write_reg(INA226_REG_CONFIG, INA226_CONFIG_RESET)) {
        return false;
    }
    HAL_Delay(1);  /* Wait for reset */

    /* Configure: 64x averaging, 1.1ms conversion for both, continuous shunt+bus */
    uint16_t config = INA226_AVG_64
                    | INA226_VBUS_1100US
                    | INA226_VSH_1100US
                    | INA226_MODE_SHBUS_CONT;
    if (!ina226_write_reg(INA226_REG_CONFIG, config)) {
        return false;
    }

    /* Calibration register:
     * Current_LSB = Max_Expected_Current / 2^15
     * Cal = 0.00512 / (Current_LSB * R_shunt)
     *
     * With INA226_MAX_CURRENT_A = 3.2768 A:
     *   Current_LSB = 3.2768 / 32768 = 0.0001 A = 0.1 mA
     *   Cal = 0.00512 / (0.0001 * 0.1) = 512
     *
     * This gives Current register LSB = 0.1 mA, nice round number.
     */
    current_lsb_a = INA226_MAX_CURRENT_A / 32768.0f;
    power_lsb_w = 25.0f * current_lsb_a;

    uint16_t cal = (uint16_t)(0.00512f / (current_lsb_a * INA226_SHUNT_OHM));
    if (!ina226_write_reg(INA226_REG_CALIBRATION, cal)) {
        return false;
    }

    return true;
}

float INA226_ReadCurrent_mA(void)
{
    if (i2c_fault) return 0.0f;

    uint16_t raw = 0;
    if (!ina226_read_reg(INA226_REG_CURRENT, &raw)) return 0.0f;

    /* Current register is signed 16-bit. LSB = current_lsb_a. */
    int16_t signed_raw = (int16_t)raw;
    return (float)signed_raw * current_lsb_a * 1000.0f;  /* mA */
}

float INA226_ReadBusVoltage_mV(void)
{
    if (i2c_fault) return 0.0f;

    uint16_t raw = 0;
    if (!ina226_read_reg(INA226_REG_BUS_V, &raw)) return 0.0f;

    /* Bus voltage LSB = 1.25 mV (unsigned). */
    return (float)raw * 1.25f;
}

float INA226_ReadShuntVoltage_uV(void)
{
    if (i2c_fault) return 0.0f;

    uint16_t raw = 0;
    if (!ina226_read_reg(INA226_REG_SHUNT_V, &raw)) return 0.0f;

    /* Shunt voltage LSB = 2.5 uV (signed). */
    int16_t signed_raw = (int16_t)raw;
    return (float)signed_raw * 2.5f;
}

float INA226_ReadPower_mW(void)
{
    if (i2c_fault) return 0.0f;

    uint16_t raw = 0;
    if (!ina226_read_reg(INA226_REG_POWER, &raw)) return 0.0f;

    /* Power LSB = 25 * current_lsb. */
    return (float)raw * power_lsb_w * 1000.0f;  /* mW */
}

bool INA226_IsFault(void)
{
    return i2c_fault;
}

/* ---- Diagnostics ---- */

/* Helper: append unsigned integer to buffer. */
static char *uint_to_str(char *p, uint32_t v)
{
    char tmp[10];
    int i = 0;
    if (v == 0) { *p++ = '0'; return p; }
    while (v) { tmp[i++] = '0' + (v % 10); v /= 10; }
    while (i--) *p++ = tmp[i];
    return p;
}

/* Helper: append 16-bit hex to buffer. */
static char *hex16_to_str(char *p, uint16_t v)
{
    *p++ = '0'; *p++ = 'x';
    for (int i = 12; i >= 0; i -= 4) {
        uint8_t nibble = (v >> i) & 0xF;
        *p++ = nibble < 10 ? ('0' + nibble) : ('A' + nibble - 10);
    }
    return p;
}

void INA226_PrintDiag(void)
{
    char buf[200];
    char *p = buf;
    const char *s;

    /* Line 1: register dump */
    s = "[INA226] ";
    while (*s) *p++ = *s++;

    if (i2c_fault) {
        s = "FAULT (I2C error)\r\n";
        while (*s) *p++ = *s++;
        CDC_Transmit_HS((uint8_t *)buf, (uint16_t)(p - buf));
        return;
    }

    uint16_t config = 0, cal = 0, mfr = 0, die = 0;
    ina226_read_reg(INA226_REG_CONFIG, &config);
    ina226_read_reg(INA226_REG_CALIBRATION, &cal);
    ina226_read_reg(INA226_REG_MFR_ID, &mfr);
    ina226_read_reg(INA226_REG_DIE_ID, &die);

    s = "CFG=";
    while (*s) *p++ = *s++;
    p = hex16_to_str(p, config);
    s = " CAL=";
    while (*s) *p++ = *s++;
    p = uint_to_str(p, cal);
    s = " MFR=";
    while (*s) *p++ = *s++;
    p = hex16_to_str(p, mfr);
    s = " DIE=";
    while (*s) *p++ = *s++;
    p = hex16_to_str(p, die);
    *p++ = '\r'; *p++ = '\n';
    CDC_Transmit_HS((uint8_t *)buf, (uint16_t)(p - buf));
    HAL_Delay(5);  /* USB CDC needs time between transmits */

    /* Line 2: measurements */
    p = buf;
    s = "[INA226] ";
    while (*s) *p++ = *s++;

    /* Bus voltage */
    float bus_mv = INA226_ReadBusVoltage_mV();
    s = "Vbus=";
    while (*s) *p++ = *s++;
    uint32_t bus_int = (uint32_t)bus_mv;
    uint32_t bus_frac = (uint32_t)((bus_mv - (float)bus_int) * 10.0f);
    p = uint_to_str(p, bus_int);
    *p++ = '.';
    p = uint_to_str(p, bus_frac);
    s = "mV";
    while (*s) *p++ = *s++;

    /* Shunt voltage */
    float sh_uv = INA226_ReadShuntVoltage_uV();
    s = " Vsh=";
    while (*s) *p++ = *s++;
    int32_t sh_int = (int32_t)sh_uv;
    if (sh_int < 0) { *p++ = '-'; sh_int = -sh_int; }
    p = uint_to_str(p, (uint32_t)sh_int);
    s = "uV";
    while (*s) *p++ = *s++;

    /* Current */
    float cur_ma = INA226_ReadCurrent_mA();
    s = " I=";
    while (*s) *p++ = *s++;
    if (cur_ma < 0.0f) { *p++ = '-'; cur_ma = -cur_ma; }
    uint32_t i_int = (uint32_t)cur_ma;
    uint32_t i_frac = (uint32_t)((cur_ma - (float)i_int) * 10.0f);
    p = uint_to_str(p, i_int);
    *p++ = '.';
    p = uint_to_str(p, i_frac);
    s = "mA";
    while (*s) *p++ = *s++;

    /* Power */
    float pwr_mw = INA226_ReadPower_mW();
    s = " P=";
    while (*s) *p++ = *s++;
    uint32_t pw_int = (uint32_t)pwr_mw;
    uint32_t pw_frac = (uint32_t)((pwr_mw - (float)pw_int) * 10.0f);
    p = uint_to_str(p, pw_int);
    *p++ = '.';
    p = uint_to_str(p, pw_frac);
    s = "mW";
    while (*s) *p++ = *s++;

    *p++ = '\r'; *p++ = '\n';
    CDC_Transmit_HS((uint8_t *)buf, (uint16_t)(p - buf));
}
