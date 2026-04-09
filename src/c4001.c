/**
 * @file c4001.c
 * @brief DFRobot C4001 mmWave presence sensor driver.
 *
 * UART4 at 9600 baud (PA0=TX AF8, PI9=RX AF8).
 * Sensor sends ASCII lines terminated by \n.
 * Presence mode: "$DFHPD,0" or "$DFHPD,1".
 * Speed mode:    "$DFDMD,<targets>,0,<range>,<speed>,<energy>,0,0".
 *
 * RX is interrupt-driven (byte-at-a-time via HAL_UART_Receive_IT).
 * C4001_Poll() processes complete lines from a ring buffer.
 */

#include "c4001.h"
#include "usbd_cdc_if.h"
#include <string.h>

UART_HandleTypeDef huart4;

/* ---- Internal state ---- */

/* Ring buffer for ISR -> main loop */
static volatile uint8_t  rx_ring[C4001_RX_BUF_SIZE];
static volatile uint16_t rx_head = 0;   /* ISR writes here */
static volatile uint16_t rx_tail = 0;   /* Poll reads from here */

/* Single-byte target for HAL_UART_Receive_IT */
static uint8_t rx_byte;

/* Line assembly buffer */
static char line_buf[C4001_RX_BUF_SIZE];
static uint16_t line_pos = 0;

/* Latest parsed data */
static C4001_PresenceData_t presence_data;
static C4001_SpeedData_t    speed_data;

/* Diagnostics */
static uint32_t frame_count  = 0;   /* total valid $DFHPD/$DFDMD frames */
static uint32_t rx_byte_count = 0;  /* total bytes received from sensor */
static char last_raw[C4001_RX_BUF_SIZE]; /* last raw line from sensor */
static volatile bool new_frame = false;  /* set by parser, cleared by HasNewFrame */

/* ---- Forward declarations ---- */

static void parse_line(const char *line, uint16_t len);
static void send_cmd(const char *cmd);
static void write_config_cmd(const char *cmd1, const char *cmd2);
static char *uint_to_str(char *dst, uint32_t val);
/* ---- UART4 MSP (called by HAL_UART_Init) ---- */

void HAL_UART_MspInit(UART_HandleTypeDef *huart)
{
    if (huart->Instance != UART4) return;

    __HAL_RCC_UART4_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOI_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};

    /* PA0 = UART4_TX, AF8 */
    gpio.Pin       = C4001_TX_PIN;
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_NOPULL;
    gpio.Speed     = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = GPIO_AF8_UART4;
    HAL_GPIO_Init(C4001_TX_PORT, &gpio);

    /* PI9 = UART4_RX, AF8 */
    gpio.Pin       = C4001_RX_PIN;
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_PULLUP;
    gpio.Alternate = GPIO_AF8_UART4;
    HAL_GPIO_Init(C4001_RX_PORT, &gpio);

    HAL_NVIC_SetPriority(C4001_UART_IRQn, C4001_UART_IRQ_PRIO, 0);
    HAL_NVIC_EnableIRQ(C4001_UART_IRQn);
}

void HAL_UART_MspDeInit(UART_HandleTypeDef *huart)
{
    if (huart->Instance != UART4) return;

    __HAL_RCC_UART4_CLK_DISABLE();
    HAL_GPIO_DeInit(C4001_TX_PORT, C4001_TX_PIN);
    HAL_GPIO_DeInit(C4001_RX_PORT, C4001_RX_PIN);
    HAL_NVIC_DisableIRQ(C4001_UART_IRQn);
}

/* ---- RX interrupt callback ---- */

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != UART4) return;

    /* Push byte into ring buffer */
    rx_byte_count++;
    uint16_t next = (rx_head + 1U) % C4001_RX_BUF_SIZE;
    if (next != rx_tail) {
        rx_ring[rx_head] = rx_byte;
        rx_head = next;
    }
    /* Re-arm for next byte */
    HAL_UART_Receive_IT(&huart4, &rx_byte, 1);
}

/* ---- Core API ---- */

void C4001_Init(void)
{
    huart4.Instance          = UART4;
    huart4.Init.BaudRate     = C4001_UART_BAUD;
    huart4.Init.WordLength   = UART_WORDLENGTH_8B;
    huart4.Init.StopBits     = UART_STOPBITS_1;
    huart4.Init.Parity       = UART_PARITY_NONE;
    huart4.Init.Mode         = UART_MODE_TX_RX;
    huart4.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart4.Init.OverSampling = UART_OVERSAMPLING_16;

    if (HAL_UART_Init(&huart4) != HAL_OK) {
        Error_Handler();
    }

    /* Arm first RX interrupt */
    HAL_UART_Receive_IT(&huart4, &rx_byte, 1);

    /* Give sensor time to boot, then start in presence mode */
    HAL_Delay(200);
    C4001_SetMode(C4001_MODE_PRESENCE);
    C4001_Start();
}

void C4001_Poll(void)
{
    /* Drain ring buffer into line buffer, process complete lines */
    while (rx_tail != rx_head) {
        uint8_t ch = rx_ring[rx_tail];
        rx_tail = (rx_tail + 1U) % C4001_RX_BUF_SIZE;

        if (ch == '\n' || ch == '\r') {
            if (line_pos > 0) {
                line_buf[line_pos] = '\0';
                parse_line(line_buf, line_pos);
                line_pos = 0;
            }
        } else {
            if (line_pos < (C4001_RX_BUF_SIZE - 1U)) {
                line_buf[line_pos++] = (char)ch;
            }
        }
    }

}

C4001_PresenceData_t C4001_GetPresence(void)
{
    return presence_data;
}

C4001_SpeedData_t C4001_GetSpeed(void)
{
    return speed_data;
}

bool C4001_HasNewFrame(void)
{
    if (new_frame) { new_frame = false; return true; }
    return false;
}

uint32_t C4001_GetFrameCount(void)  { return frame_count; }
uint32_t C4001_GetRxByteCount(void) { return rx_byte_count; }
const char *C4001_GetLastRaw(void)  { return last_raw; }

/* ---- Sensor commands ---- */

void C4001_Start(void)
{
    send_cmd("sensorStart");
    HAL_Delay(200);
}

void C4001_Stop(void)
{
    send_cmd("sensorStop");
    HAL_Delay(200);
}

void C4001_SetMode(C4001_Mode_t mode)
{
    if (mode == C4001_MODE_PRESENCE) {
        write_config_cmd("setRunApp 0", NULL);
    } else {
        write_config_cmd("setRunApp 1", NULL);
    }
    HAL_Delay(1500);  /* mode change needs time */
}

void C4001_SetRange(uint16_t min_cm, uint16_t max_cm, uint16_t trig_cm)
{
    /* Format: "setRange <min_m> <max_m>" with decimal values */
    char cmd[40];
    char *p = cmd;
    const char *pre = "setRange ";
    while (*pre) *p++ = *pre++;
    p = uint_to_str(p, min_cm / 100U);
    *p++ = '.';
    p = uint_to_str(p, min_cm % 100U);
    *p++ = ' ';
    p = uint_to_str(p, max_cm / 100U);
    *p++ = '.';
    p = uint_to_str(p, max_cm % 100U);
    *p = '\0';

    /* Trigger range is set separately via setRange for the trig param */
    char cmd2[40];
    char *q = cmd2;
    const char *pre2 = "setRange ";
    while (*pre2) *q++ = *pre2++;
    q = uint_to_str(q, min_cm / 100U);
    *q++ = '.';
    q = uint_to_str(q, min_cm % 100U);
    *q++ = ' ';
    q = uint_to_str(q, max_cm / 100U);
    *q++ = '.';
    q = uint_to_str(q, max_cm % 100U);
    *q = '\0';

    write_config_cmd(cmd, NULL);
    (void)trig_cm;  /* TODO: trig range if needed */
}

/** Set trigger sensitivity (0-9). Format: "setSensitivity 255 <trig>".
 *  255 = sentinel meaning "don't change keep sensitivity". */
void C4001_SetTrigSensitivity(uint8_t sens)
{
    if (sens > 9) sens = 9;
    char cmd[] = "setSensitivity 255 0";
    cmd[19] = '0' + sens;
    write_config_cmd(cmd, NULL);
}

/** Set keep sensitivity (0-9). Format: "setSensitivity <keep> 255".
 *  255 = sentinel meaning "don't change trig sensitivity". */
void C4001_SetKeepSensitivity(uint8_t sens)
{
    if (sens > 9) sens = 9;
    char cmd[] = "setSensitivity 0 255";
    cmd[15] = '0' + sens;
    write_config_cmd(cmd, NULL);
}

void C4001_SetDelay(uint8_t trig_delay, uint16_t keep_timeout)
{
    char cmd[32];
    char *p = cmd;
    const char *pre = "setLatency ";
    while (*pre) *p++ = *pre++;
    p = uint_to_str(p, trig_delay);
    *p++ = ' ';
    p = uint_to_str(p, keep_timeout);
    *p = '\0';
    write_config_cmd(cmd, NULL);
}

void C4001_SetMicroMotion(bool enable)
{
    write_config_cmd(enable ? "setMicroMotion 1" : "setMicroMotion 0", NULL);
}

void C4001_FactoryReset(void)
{
    send_cmd("sensorStop");
    HAL_Delay(200);
    send_cmd("resetCfg");
    HAL_Delay(1500);
    send_cmd("sensorStart");
    HAL_Delay(200);
}

/** Pass-through: forward a command from USB CDC to the sensor. */
void C4001_HandleSerialCmd(const char *cmd, uint16_t len)
{
    /* Strip trailing \r\n from USB packet for clean forwarding */
    while (len > 0 && (cmd[len - 1] == '\r' || cmd[len - 1] == '\n')) {
        len--;
    }
    if (len == 0) return;

    /* Echo command back to user so they see what was sent */
    char echo[80];
    char *ep = echo;
    const char *pre = "> ";
    while (*pre) *ep++ = *pre++;
    for (uint16_t i = 0; i < len && ep < echo + sizeof(echo) - 3; i++) {
        *ep++ = cmd[i];
    }
    *ep++ = '\r';
    *ep++ = '\n';
    CDC_Transmit_HS((uint8_t *)echo, (uint16_t)(ep - echo));

    /* Forward to sensor with \r\n terminator */
    HAL_UART_Transmit(&huart4, (uint8_t *)cmd, len, 100);
    uint8_t crlf[] = "\r\n";
    HAL_UART_Transmit(&huart4, crlf, 2, 50);
}

/* ---- Line parser ---- */

/** Simple integer parser for ASCII decimal. Returns 0 on empty/invalid. */
static uint32_t parse_uint(const char *s, const char **end)
{
    uint32_t val = 0;
    while (*s >= '0' && *s <= '9') {
        val = val * 10U + (uint32_t)(*s - '0');
        s++;
    }
    if (end) *end = s;
    return val;
}

/** Parse a simple float like "2.45" — integer + fraction, no exponent. */
static float parse_float(const char *s, const char **end)
{
    int negative = 0;
    if (*s == '-') { negative = 1; s++; }

    float val = 0.0f;
    while (*s >= '0' && *s <= '9') {
        val = val * 10.0f + (float)(*s - '0');
        s++;
    }
    if (*s == '.') {
        s++;
        float frac = 0.1f;
        while (*s >= '0' && *s <= '9') {
            val += (float)(*s - '0') * frac;
            frac *= 0.1f;
            s++;
        }
    }
    if (end) *end = s;
    return negative ? -val : val;
}

/** Find next field after comma. Returns pointer to char after comma, or NULL. */
static const char *next_field(const char *s)
{
    while (*s && *s != ',') s++;
    return (*s == ',') ? (s + 1) : NULL;
}

static void parse_line(const char *line, uint16_t len)
{
    /* Save every raw line for diagnostics */
    uint16_t copy_len = (len < C4001_RX_BUF_SIZE - 1U) ? len : (C4001_RX_BUF_SIZE - 1U);
    memcpy(last_raw, line, copy_len);
    last_raw[copy_len] = '\0';

    /* Presence mode: "$DFHPD,<0|1>" */
    if (strncmp(line, "$DFHPD,", 7) == 0) {
        presence_data.present     = (line[7] == '1');
        presence_data.last_update = HAL_GetTick();
        frame_count++;
        new_frame = true;
        return;
    }

    /* Speed mode: "$DFDMD,<targets>,<x>,<range>,<speed>,<energy>,<x>,<x>" */
    if (strncmp(line, "$DFDMD,", 7) == 0) {
        const char *p = line + 7;
        speed_data.target_count = (uint8_t)parse_uint(p, NULL);

        p = next_field(p); if (!p) return;  /* skip field 2 */
        p = next_field(p); if (!p) return;  /* range */
        speed_data.range_m = parse_float(p, NULL);

        p = next_field(p); if (!p) return;  /* speed */
        speed_data.speed_mps = parse_float(p, NULL);

        p = next_field(p); if (!p) return;  /* energy */
        speed_data.energy = parse_uint(p, NULL);

        speed_data.last_update = HAL_GetTick();
        /* Derive presence from target count so LED works in speed mode too */
        presence_data.present     = (speed_data.target_count > 0);
        presence_data.last_update = speed_data.last_update;
        frame_count++;
        new_frame = true;
        return;
    }

    /* Any other line from sensor (responses, errors, etc.) — forward to CDC */
    char fwd[160];
    char *fp = fwd;
    const char *pre = "[sensor] ";
    while (*pre) *fp++ = *pre++;
    for (uint16_t i = 0; i < len && fp < fwd + sizeof(fwd) - 3; i++) {
        *fp++ = line[i];
    }
    *fp++ = '\r';
    *fp++ = '\n';
    CDC_Transmit_HS((uint8_t *)fwd, (uint16_t)(fp - fwd));
}

/* ---- Internal helpers ---- */

/** Send a plain command string to sensor with \r\n terminator. */
static void send_cmd(const char *cmd)
{
    HAL_UART_Transmit(&huart4, (uint8_t *)cmd, (uint16_t)strlen(cmd), 100);
    uint8_t crlf[] = "\r\n";
    HAL_UART_Transmit(&huart4, crlf, 2, 50);
}

/**
 * Write a configuration command: stop sensor, send cmd, save, restart.
 * If cmd2 is non-NULL it is sent as a second command before save.
 */
static void write_config_cmd(const char *cmd1, const char *cmd2)
{
    send_cmd("sensorStop");
    HAL_Delay(200);

    send_cmd(cmd1);
    HAL_Delay(100);

    if (cmd2) {
        send_cmd(cmd2);
        HAL_Delay(100);
    }

    send_cmd("saveConfig");
    HAL_Delay(100);

    send_cmd("sensorStart");
    HAL_Delay(100);
}

/** Write decimal number into buffer, return pointer past last digit. */
static char *uint_to_str(char *dst, uint32_t val)
{
    char tmp[10];
    int i = 0;
    if (val == 0) { *dst++ = '0'; return dst; }
    while (val > 0) {
        tmp[i++] = '0' + (char)(val % 10U);
        val /= 10U;
    }
    while (i > 0) *dst++ = tmp[--i];
    return dst;
}

