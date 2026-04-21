/**
 * @file c4001.h
 * @brief DFRobot C4001 mmWave presence sensor driver over USART1.
 *
 * BlackPill F411 port: USART1 at 9600 baud (PA9=TX AF7, PA10=RX AF7).
 * Sensor sends ASCII frames: "$DFHPD,<0|1>" in presence mode.
 */

#ifndef C4001_H
#define C4001_H

#include "main.h"
#include <stdint.h>
#include <stdbool.h>

/* USART1 pins: PA9 = TX (AF7), PA10 = RX (AF7) */
#define C4001_UART_INSTANCE     USART1
#define C4001_UART_BAUD         9600U
#define C4001_UART_IRQn         USART1_IRQn
#define C4001_UART_IRQ_PRIO     6U

#define C4001_TX_PORT           GPIOA
#define C4001_TX_PIN            GPIO_PIN_9
#define C4001_RX_PORT           GPIOA
#define C4001_RX_PIN            GPIO_PIN_10

#define C4001_RX_BUF_SIZE       128U

/* Sensor operating modes */
typedef enum {
    C4001_MODE_PRESENCE = 0,    /* Existence / presence detection */
    C4001_MODE_SPEED    = 1     /* Speed + ranging */
} C4001_Mode_t;

/* Sensor status returned by C4001_GetStatus() */
typedef struct {
    uint8_t work_status;    /* 0 = stopped, 1 = running */
    uint8_t work_mode;      /* 0 = presence, 1 = speed */
    uint8_t init_status;    /* 0 = not init, 1 = initialized */
} C4001_Status_t;

/* Presence mode data */
typedef struct {
    bool     present;       /* true if human detected */
    uint32_t last_update;   /* HAL_GetTick() of last valid frame */
} C4001_PresenceData_t;

/* Speed mode data (for future use) */
typedef struct {
    uint8_t  target_count;
    float    range_m;
    float    speed_mps;
    uint32_t energy;
    uint32_t last_update;
} C4001_SpeedData_t;

extern UART_HandleTypeDef huart1;

/* ---- Core API ---- */

/** Initialize USART1 and start receiving from C4001. */
void C4001_Init(void);

/** Process received UART data. Call from main loop. */
void C4001_Poll(void);

/** Get latest presence data. */
C4001_PresenceData_t C4001_GetPresence(void);

/** Get latest speed/range data (speed mode only). */
C4001_SpeedData_t C4001_GetSpeed(void);

/** Returns true once per new parsed frame, then clears. Call from main loop. */
bool C4001_HasNewFrame(void);

/** Get total valid frames received since boot. */
uint32_t C4001_GetFrameCount(void);

/** Get total bytes received from sensor since boot. */
uint32_t C4001_GetRxByteCount(void);

/** Get last raw line from sensor (for debug output). */
const char *C4001_GetLastRaw(void);

/* ---- Sensor commands ---- */

/** Start sensor data collection. */
void C4001_Start(void);

/** Stop sensor data collection. */
void C4001_Stop(void);

/** Set operating mode (presence or speed). Stops/restarts sensor. */
void C4001_SetMode(C4001_Mode_t mode);

/** Set detection range in cm. min: 30-2000, max: 30-2000, trig: 240-2000. */
void C4001_SetRange(uint16_t min_cm, uint16_t max_cm, uint16_t trig_cm);

/** Set trigger sensitivity (0-9). Higher = more sensitive. */
void C4001_SetTrigSensitivity(uint8_t sens);

/** Set keep sensitivity (0-9). Higher = more sensitive. */
void C4001_SetKeepSensitivity(uint8_t sens);

/** Set trigger delay (0-200, unit 0.01s) and keep timeout (4-3000, unit 0.5s). */
void C4001_SetDelay(uint8_t trig_delay, uint16_t keep_timeout);

/** Enable or disable micromotion / fretting detection. */
void C4001_SetMicroMotion(bool enable);

/** Factory reset all parameters. */
void C4001_FactoryReset(void);

/** Process a command string received from USB CDC (pass-through). */
void C4001_HandleSerialCmd(const char *cmd, uint16_t len);

#endif /* C4001_H */
