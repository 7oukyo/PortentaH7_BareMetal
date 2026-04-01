/**
 * @file usbd_desc.h
 * @brief USB device descriptor declarations.
 */

#ifndef USBD_DESC_H
#define USBD_DESC_H

#include "usbd_def.h"

/* STM32 unique device ID registers for serial number */
#define DEVICE_ID1    (UID_BASE)
#define DEVICE_ID2    (UID_BASE + 0x4U)
#define DEVICE_ID3    (UID_BASE + 0x8U)

#define USB_SIZ_STRING_SERIAL   0x1AU

extern USBD_DescriptorsTypeDef HS_Desc;

#endif /* USBD_DESC_H */
