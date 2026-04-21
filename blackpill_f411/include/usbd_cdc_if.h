/**
 * @file usbd_cdc_if.h
 * @brief CDC interface for USB Virtual COM Port (Full Speed).
 */

#ifndef USBD_CDC_IF_H
#define USBD_CDC_IF_H

#include "usbd_cdc.h"

/* RX/TX buffer sizes */
#define APP_RX_DATA_SIZE  512U
#define APP_TX_DATA_SIZE  512U

extern USBD_CDC_ItfTypeDef USBD_Interface_fops_FS;

uint8_t CDC_Transmit_FS(uint8_t *Buf, uint16_t Len);

#endif /* USBD_CDC_IF_H */
