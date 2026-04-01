/**
 * @file led_pwm.c
 * @brief LED feedback blink for serial RX and periodic serial heartbeat.
 *
 * Replaces rainbow PWM with simple green blink on serial receive.
 * TIM6 still runs at 10kHz to time the blink duration (~50ms).
 * Also provides a 5-second heartbeat that prints uptime over CDC.
 *
 * RM0399 §43 — TIM6 basic timer.
 */

#include "led_pwm.h"
#include "usbd_cdc_if.h"

#define BLINK_TICKS     500U   /* 500 ticks @ 10kHz = 50ms blink */
#define HEARTBEAT_MS    5000U  /* 5 seconds */

TIM_HandleTypeDef htim6;

static volatile uint16_t blink_counter = 0;
static volatile uint8_t  blink_active  = 0;

/** Trigger a short green LED blink. Called from CDC receive callback. */
void LedPwm_BlinkOnRx(void)
{
    /* Turn green LED on (active LOW) */
    HAL_GPIO_WritePin(LED_GPIO_PORT, LED_GREEN_PIN, GPIO_PIN_RESET);
    blink_counter = BLINK_TICKS;
    blink_active  = 1;
}

void LedPwm_Init(void)
{
    htim6.Instance               = TIM6;
    htim6.Init.Prescaler         = 2399U;  /* 240MHz / 2400 = 100kHz tick */
    htim6.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim6.Init.Period            = 9U;     /* 100kHz / 10 = 10kHz overflow */
    htim6.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;

    if (HAL_TIM_Base_Init(&htim6) != HAL_OK) {
        Error_Handler();
    }
    if (HAL_TIM_Base_Start_IT(&htim6) != HAL_OK) {
        Error_Handler();
    }
}

/** TIM6 ISR callback — counts down blink timer and turns LED off. */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance != TIM6) {
        return;
    }

    if (blink_active) {
        if (blink_counter > 0) {
            blink_counter--;
        } else {
            /* Turn green LED off (active LOW: SET = off) */
            HAL_GPIO_WritePin(LED_GPIO_PORT, LED_GREEN_PIN, GPIO_PIN_SET);
            blink_active = 0;
        }
    }
}

/** Write decimal number into buffer, return pointer past last digit. */
static char *uint_to_str(char *dst, uint32_t val)
{
    char tmp[10];
    int i = 0;
    if (val == 0) {
        *dst++ = '0';
        return dst;
    }
    while (val > 0) {
        tmp[i++] = '0' + (char)(val % 10U);
        val /= 10U;
    }
    while (i > 0) {
        *dst++ = tmp[--i];
    }
    return dst;
}

/** Write 3-digit zero-padded milliseconds. */
static char *ms_to_str(char *dst, uint32_t ms)
{
    dst[0] = '0' + (char)((ms / 100U) % 10U);
    dst[1] = '0' + (char)((ms / 10U) % 10U);
    dst[2] = '0' + (char)(ms % 10U);
    return dst + 3;
}

/** Call from main loop — sends uptime every 5 seconds over CDC. */
void LedPwm_HeartbeatPoll(void)
{
    static uint32_t last_tick = 0;
    uint32_t now = HAL_GetTick();

    if (now - last_tick >= HEARTBEAT_MS) {
        last_tick = now;

        char buf[32];
        char *p = buf;
        *p++ = '[';
        p = uint_to_str(p, now / 1000U);
        *p++ = '.';
        p = ms_to_str(p, now % 1000U);
        *p++ = ']';
        *p++ = ' ';

        /* Copy "heartbeat\r\n" */
        const char *msg = "alive\r\n";
        while (*msg) { *p++ = *msg++; }

        CDC_Transmit_HS((uint8_t *)buf, (uint16_t)(p - buf));

        /* Brief blue LED pulse to show MCU is alive */
        HAL_GPIO_WritePin(LED_GPIO_PORT, LED_BLUE_PIN, GPIO_PIN_RESET);
        HAL_Delay(30);
        HAL_GPIO_WritePin(LED_GPIO_PORT, LED_BLUE_PIN, GPIO_PIN_SET);
    }
}
