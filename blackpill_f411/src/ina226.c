/**
 * @file ina226.c
 * @brief INA226 I2C current/power monitor driver.
 *
 * BlackPill F411 port: I2C1 (PB6=SCL, PB7=SDA). Address 0x40.
 * Shunt resistor: INA226_SHUNT_OHM (10 mohm physical, 0.025 effective for clone).
 * Calibration register computed so Current register reads in mA directly.
 *
 * Key difference from H7: I2C1 uses ClockSpeed/DutyCycle (not Timing register).
 */

#include "ina226.h"
#include "usbd_cdc_if.h"
#include <string.h>

static I2C_HandleTypeDef hi2c1;
static bool i2c_fault = false;

static float current_lsb_a = 0.0f;
static float power_lsb_w = 0.0f;

#define INA226_ADDR_HAL  (INA226_I2C_ADDR << 1)
#define INA226_I2C_TIMEOUT  100U

/* ---- I2C1 initialization ---- */

/* Init I2C1 peripheral for INA226 communication.
 * I2C1 on APB1 (48 MHz). Fast mode 400 kHz. */
static bool ina226_i2c_init(void)
{
    hi2c1.Instance              = I2C1;
    hi2c1.Init.ClockSpeed       = 400000U;  /* 400 kHz Fast Mode */
    hi2c1.Init.DutyCycle        = I2C_DUTYCYCLE_2;
    hi2c1.Init.OwnAddress1      = 0;
    hi2c1.Init.AddressingMode   = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode  = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.OwnAddress2      = 0;
    hi2c1.Init.GeneralCallMode  = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode    = I2C_NOSTRETCH_DISABLE;

    if (HAL_I2C_Init(&hi2c1) != HAL_OK) {
        return false;
    }
    return true;
}

/* ---- Register access (16-bit big-endian) ---- */

static bool ina226_write_reg(uint8_t reg, uint16_t val)
{
    uint8_t buf[3];
    buf[0] = reg;
    buf[1] = (uint8_t)(val >> 8);
    buf[2] = (uint8_t)(val & 0xFF);
    if (HAL_I2C_Master_Transmit(&hi2c1, INA226_ADDR_HAL, buf, 3,
                                 INA226_I2C_TIMEOUT) != HAL_OK) {
        i2c_fault = true;
        return false;
    }
    return true;
}

static bool ina226_read_reg(uint8_t reg, uint16_t *val)
{
    uint8_t tx = reg;
    uint8_t rx[2];
    if (HAL_I2C_Master_Transmit(&hi2c1, INA226_ADDR_HAL, &tx, 1,
                                 INA226_I2C_TIMEOUT) != HAL_OK) {
        i2c_fault = true;
        return false;
    }
    if (HAL_I2C_Master_Receive(&hi2c1, INA226_ADDR_HAL, rx, 2,
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

    uint16_t mfr_id = 0;
    if (!ina226_read_reg(INA226_REG_MFR_ID, &mfr_id) || mfr_id != 0x5449U) {
        i2c_fault = true;
        return false;
    }

    if (!ina226_write_reg(INA226_REG_CONFIG, INA226_CONFIG_RESET)) {
        return false;
    }
    HAL_Delay(1);

    uint16_t config = INA226_AVG_64
                    | INA226_VBUS_1100US
                    | INA226_VSH_1100US
                    | INA226_MODE_SHBUS_CONT;
    if (!ina226_write_reg(INA226_REG_CONFIG, config)) {
        return false;
    }

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
    int16_t signed_raw = (int16_t)raw;
    return (float)signed_raw * current_lsb_a * 1000.0f;
}

float INA226_ReadBusVoltage_mV(void)
{
    if (i2c_fault) return 0.0f;
    uint16_t raw = 0;
    if (!ina226_read_reg(INA226_REG_BUS_V, &raw)) return 0.0f;
    return (float)raw * 1.25f;
}

float INA226_ReadShuntVoltage_uV(void)
{
    if (i2c_fault) return 0.0f;
    uint16_t raw = 0;
    if (!ina226_read_reg(INA226_REG_SHUNT_V, &raw)) return 0.0f;
    int16_t signed_raw = (int16_t)raw;
    return (float)signed_raw * 1.0f;
}

float INA226_ReadPower_mW(void)
{
    if (i2c_fault) return 0.0f;
    uint16_t raw = 0;
    if (!ina226_read_reg(INA226_REG_POWER, &raw)) return 0.0f;
    return (float)raw * power_lsb_w * 1000.0f;
}

bool INA226_IsFault(void) { return i2c_fault; }

/* ---- Diagnostics ---- */

static char *uint_to_str(char *p, uint32_t v)
{
    char tmp[10]; int i = 0;
    if (v == 0) { *p++ = '0'; return p; }
    while (v) { tmp[i++] = '0' + (v % 10); v /= 10; }
    while (i--) *p++ = tmp[i];
    return p;
}

static char *hex16_to_str(char *p, uint16_t v)
{
    *p++ = '0'; *p++ = 'x';
    for (int i = 12; i >= 0; i -= 4) {
        uint8_t nibble = (v >> i) & 0xF;
        *p++ = nibble < 10 ? ('0' + nibble) : ('A' + nibble - 10);
    }
    return p;
}

static char *append_s(char *p, const char *s)
{
    while (*s) *p++ = *s++;
    return p;
}

static char *int_to_str(char *p, int32_t v)
{
    if (v < 0) { *p++ = '-'; v = -v; }
    return uint_to_str(p, (uint32_t)v);
}

static char *flt1_to_str(char *p, float v)
{
    if (v < 0.0f) { *p++ = '-'; v = -v; }
    uint32_t i = (uint32_t)v;
    uint32_t f = (uint32_t)((v - (float)i) * 10.0f);
    p = uint_to_str(p, i); *p++ = '.'; p = uint_to_str(p, f);
    return p;
}

static char *flt2_to_str(char *p, float v)
{
    if (v < 0.0f) { *p++ = '-'; v = -v; }
    uint32_t i = (uint32_t)v;
    uint32_t f = (uint32_t)((v - (float)i) * 100.0f);
    p = uint_to_str(p, i); *p++ = '.';
    if (f < 10) *p++ = '0';
    p = uint_to_str(p, f);
    return p;
}

static void cdc_line(const char *buf, uint16_t len)
{
    CDC_Transmit_FS((uint8_t *)buf, len);
    HAL_Delay(5);
}

void INA226_PrintDiag(void)
{
    char buf[200];
    char *p = buf;

    p = append_s(p, "[INA226] ");
    if (i2c_fault) {
        p = append_s(p, "FAULT (I2C error)\r\n");
        CDC_Transmit_FS((uint8_t *)buf, (uint16_t)(p - buf));
        return;
    }

    uint16_t config = 0, cal = 0, mfr = 0, die = 0;
    ina226_read_reg(INA226_REG_CONFIG, &config);
    ina226_read_reg(INA226_REG_CALIBRATION, &cal);
    ina226_read_reg(INA226_REG_MFR_ID, &mfr);
    ina226_read_reg(INA226_REG_DIE_ID, &die);

    p = append_s(p, "CFG="); p = hex16_to_str(p, config);
    p = append_s(p, " CAL="); p = uint_to_str(p, cal);
    p = append_s(p, " MFR="); p = hex16_to_str(p, mfr);
    p = append_s(p, " DIE="); p = hex16_to_str(p, die);
    p = append_s(p, "\r\n");
    cdc_line(buf, (uint16_t)(p - buf));

    p = buf;
    p = append_s(p, "[INA226] ");
    float bus_mv = INA226_ReadBusVoltage_mV();
    p = append_s(p, "Vbus="); p = flt1_to_str(p, bus_mv); p = append_s(p, "mV");
    float sh_uv = INA226_ReadShuntVoltage_uV();
    p = append_s(p, " Vsh="); p = int_to_str(p, (int32_t)sh_uv); p = append_s(p, "uV");
    float cur_ma = INA226_ReadCurrent_mA();
    p = append_s(p, " I="); p = flt1_to_str(p, cur_ma); p = append_s(p, "mA");
    float pwr_mw = INA226_ReadPower_mW();
    p = append_s(p, " P="); p = flt1_to_str(p, pwr_mw); p = append_s(p, "mW");
    p = append_s(p, "\r\n");
    cdc_line(buf, (uint16_t)(p - buf));
}

void INA226_SelfTest(void)
{
    char buf[200];
    char *p;

    p = buf;
    p = append_s(p, "[INA226] ========== SELF-TEST ==========\r\n");
    cdc_line(buf, (uint16_t)(p - buf));

    if (i2c_fault) {
        p = buf; p = append_s(p, "[INA226] ABORT: I2C fault. Check wiring.\r\n");
        cdc_line(buf, (uint16_t)(p - buf));
        return;
    }

    uint16_t r_cfg = 0, r_shv = 0, r_bus = 0, r_pwr = 0, r_cur = 0;
    uint16_t r_cal = 0, r_mask = 0, r_mfr = 0, r_die = 0;
    ina226_read_reg(INA226_REG_CONFIG, &r_cfg);
    ina226_read_reg(INA226_REG_SHUNT_V, &r_shv);
    ina226_read_reg(INA226_REG_BUS_V, &r_bus);
    ina226_read_reg(INA226_REG_POWER, &r_pwr);
    ina226_read_reg(INA226_REG_CURRENT, &r_cur);
    ina226_read_reg(INA226_REG_CALIBRATION, &r_cal);
    ina226_read_reg(INA226_REG_MASK_EN, &r_mask);
    ina226_read_reg(INA226_REG_MFR_ID, &r_mfr);
    ina226_read_reg(INA226_REG_DIE_ID, &r_die);

    p = buf;
    p = append_s(p, "[INA226] MFR="); p = hex16_to_str(p, r_mfr);
    p = append_s(p, " DIE="); p = hex16_to_str(p, r_die);
    p = append_s(p, " CFG="); p = hex16_to_str(p, r_cfg);
    p = append_s(p, " MASK="); p = hex16_to_str(p, r_mask);
    p = append_s(p, "\r\n");
    cdc_line(buf, (uint16_t)(p - buf));

    uint16_t cal_expect = (uint16_t)(0.00512f / (current_lsb_a * INA226_SHUNT_OHM));
    p = buf;
    p = append_s(p, "[INA226] CAL: wrote="); p = uint_to_str(p, cal_expect);
    p = append_s(p, "("); p = hex16_to_str(p, cal_expect);
    p = append_s(p, ") read="); p = uint_to_str(p, r_cal);
    p = append_s(p, "("); p = hex16_to_str(p, r_cal);
    p = append_s(p, ") "); p = append_s(p, r_cal == cal_expect ? "OK" : "MISMATCH!");
    p = append_s(p, "\r\n");
    cdc_line(buf, (uint16_t)(p - buf));

    int16_t shv_s = (int16_t)r_shv;
    float vsh_uv = (float)shv_s * 1.0f;
    float vbus_mv = (float)r_bus * 1.25f;
    int16_t cur_s = (int16_t)r_cur;
    float cur_ma = (float)cur_s * current_lsb_a * 1000.0f;

    p = buf;
    p = append_s(p, "[INA226] Vsh: raw="); p = int_to_str(p, (int32_t)shv_s);
    p = append_s(p, " ="); p = int_to_str(p, (int32_t)vsh_uv);
    p = append_s(p, "uV | Vbus: raw="); p = uint_to_str(p, r_bus);
    p = append_s(p, " ="); p = flt1_to_str(p, vbus_mv);
    p = append_s(p, "mV\r\n");
    cdc_line(buf, (uint16_t)(p - buf));

    p = buf;
    p = append_s(p, "[INA226] Cur: raw="); p = int_to_str(p, (int32_t)cur_s);
    p = append_s(p, " ="); p = flt1_to_str(p, cur_ma);
    p = append_s(p, "mA | Pwr: raw="); p = uint_to_str(p, r_pwr);
    p = append_s(p, " ="); p = flt1_to_str(p, (float)r_pwr * power_lsb_w * 1000.0f);
    p = append_s(p, "mW\r\n");
    cdc_line(buf, (uint16_t)(p - buf));

    float i_sw = vsh_uv / (INA226_SHUNT_OHM * 1000.0f);
    p = buf;
    p = append_s(p, "[INA226] I_sw=Vsh/(Rsh*1000)="); p = flt1_to_str(p, i_sw);
    p = append_s(p, "mA (bypass chip CAL, 1.0uV LSB)\r\n");
    cdc_line(buf, (uint16_t)(p - buf));

    int32_t cur_exp = ((int32_t)shv_s * (int32_t)r_cal) / 2048;
    float factor = (cur_exp != 0) ? ((float)cur_s / (float)cur_exp) : 0.0f;
    p = buf;
    p = append_s(p, "[INA226] CHIP: vsh*cal/2048="); p = int_to_str(p, cur_exp);
    p = append_s(p, " actual="); p = int_to_str(p, (int32_t)cur_s);
    p = append_s(p, " factor="); p = flt2_to_str(p, factor);
    p = append_s(p, "\r\n");
    cdc_line(buf, (uint16_t)(p - buf));

    ina226_write_reg(INA226_REG_CALIBRATION, 2048U);
    HAL_Delay(200);
    uint16_t r_cur2 = 0;
    ina226_read_reg(INA226_REG_CURRENT, &r_cur2);
    float cur2_ma = (float)((int16_t)r_cur2) * current_lsb_a * 1000.0f;
    p = buf;
    p = append_s(p, "[INA226] FIX: CAL=2048 -> raw="); p = int_to_str(p, (int32_t)(int16_t)r_cur2);
    p = append_s(p, " ="); p = flt1_to_str(p, cur2_ma);
    p = append_s(p, "mA\r\n");
    cdc_line(buf, (uint16_t)(p - buf));

    ina226_write_reg(INA226_REG_CALIBRATION, cal_expect);

    p = buf;
    float abs_vsh = vsh_uv < 0 ? -vsh_uv : vsh_uv;
    if (factor > 2.0f && factor < 3.0f) {
        p = append_s(p, "[INA226] VERDICT: factor~2.5 => clone internal constant wrong\r\n");
    } else if (factor > 0.8f && factor < 1.2f) {
        if (abs_vsh > 20000.0f)
            p = append_s(p, "[INA226] VERDICT: math OK, Vsh high => ADC LSB or Rsh wrong\r\n");
        else
            p = append_s(p, "[INA226] VERDICT: readings consistent for this load\r\n");
    } else {
        p = append_s(p, "[INA226] VERDICT: unexpected factor, check wiring/load\r\n");
    }
    cdc_line(buf, (uint16_t)(p - buf));

    p = buf;
    p = append_s(p, "[INA226] ========== END ==========\r\n");
    cdc_line(buf, (uint16_t)(p - buf));
}
