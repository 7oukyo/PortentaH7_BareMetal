/**
 * @file usbd_conf.h
 * @brief USB Device Library configuration — constants, memory macros, debug stubs.
 */

#ifndef USBD_CONF_H
#define USBD_CONF_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "main.h"
#include "stm32f4xx_hal.h"

/* USB device configuration */
#define USBD_MAX_NUM_INTERFACES     1U
#define USBD_MAX_NUM_CONFIGURATION  1U
#define USBD_MAX_STR_DESC_SIZ       512U
#define USBD_DEBUG_LEVEL            0U
#define USBD_LPM_ENABLED            0U
#define USBD_SELF_POWERED           1U

/* Device ID (FS = internal PHY) */
#define DEVICE_FS   0
#define DEVICE_HS   1

/* Static memory allocation for USB class data (no malloc) */
void *USBD_static_malloc(uint32_t size);
void  USBD_static_free(void *p);

#define USBD_malloc     (void *)USBD_static_malloc
#define USBD_free       USBD_static_free
#define USBD_memset     memset
#define USBD_memcpy     memcpy
#define USBD_Delay      HAL_Delay

/* Debug macros (disabled at level 0) */
#if (USBD_DEBUG_LEVEL > 0)
#define USBD_UsrLog(...)   printf(__VA_ARGS__); printf("\n");
#else
#define USBD_UsrLog(...)
#endif

#if (USBD_DEBUG_LEVEL > 1)
#define USBD_ErrLog(...)   printf("ERROR: "); printf(__VA_ARGS__); printf("\n");
#else
#define USBD_ErrLog(...)
#endif

#if (USBD_DEBUG_LEVEL > 2)
#define USBD_DbgLog(...)   printf("DEBUG: "); printf(__VA_ARGS__); printf("\n");
#else
#define USBD_DbgLog(...)
#endif

#endif /* USBD_CONF_H */
