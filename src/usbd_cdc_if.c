/**
 * @file usbd_cdc_if.c
 * @brief CDC interface callbacks for USB Virtual COM Port.
 *
 * Provides Init/DeInit/Control/Receive/TransmitCplt callbacks for the
 * USB Device CDC class. CDC_Transmit_HS() is the public API for sending data.
 *
 * No RTOS — bare-metal receive callback simply echoes back for now.
 */

#include "usbd_cdc_if.h"
#include "led_pwm.h"

/* RX/TX buffers */
static uint8_t UserRxBufferHS[APP_RX_DATA_SIZE];
static uint8_t UserTxBufferHS[APP_TX_DATA_SIZE];

/* Line coding state (baud rate, stop bits, parity, data bits) */
static USBD_CDC_LineCodingTypeDef LineCoding = {
    .bitrate    = 115200,
    .format     = 0x00,  /* 1 stop bit */
    .paritytype = 0x00,  /* none */
    .datatype   = 0x08,  /* 8 bits */
};

extern USBD_HandleTypeDef hUsbDeviceHS;

static int8_t CDC_Init_HS(void);
static int8_t CDC_DeInit_HS(void);
static int8_t CDC_Control_HS(uint8_t cmd, uint8_t *pbuf, uint16_t length);
static int8_t CDC_Receive_HS(uint8_t *pbuf, uint32_t *Len);
static int8_t CDC_TransmitCplt_HS(uint8_t *pbuf, uint32_t *Len, uint8_t epnum);

USBD_CDC_ItfTypeDef USBD_Interface_fops_HS = {
    CDC_Init_HS,
    CDC_DeInit_HS,
    CDC_Control_HS,
    CDC_Receive_HS,
    CDC_TransmitCplt_HS,
};

static int8_t CDC_Init_HS(void)
{
    USBD_CDC_SetTxBuffer(&hUsbDeviceHS, UserTxBufferHS, 0);
    USBD_CDC_SetRxBuffer(&hUsbDeviceHS, UserRxBufferHS);
    return USBD_OK;
}

static int8_t CDC_DeInit_HS(void)
{
    return USBD_OK;
}

static int8_t CDC_Control_HS(uint8_t cmd, uint8_t *pbuf, uint16_t length)
{
    (void)length;

    switch (cmd) {
    case CDC_SET_LINE_CODING:
        memcpy(&LineCoding, pbuf, sizeof(LineCoding));
        break;
    case CDC_GET_LINE_CODING:
        memcpy(pbuf, &LineCoding, sizeof(LineCoding));
        break;
    case CDC_SET_CONTROL_LINE_STATE:
    case CDC_SEND_ENCAPSULATED_COMMAND:
    case CDC_GET_ENCAPSULATED_RESPONSE:
    case CDC_SET_COMM_FEATURE:
    case CDC_GET_COMM_FEATURE:
    case CDC_CLEAR_COMM_FEATURE:
    case CDC_SEND_BREAK:
    default:
        break;
    }
    return USBD_OK;
}

/** Bare-metal receive: echo received data back to host, blink green LED. */
static int8_t CDC_Receive_HS(uint8_t *Buf, uint32_t *Len)
{
    LedPwm_BlinkOnRx();

    /* Echo back what was received */
    CDC_Transmit_HS(Buf, (uint16_t)*Len);

    /* Re-arm receive for next packet */
    USBD_CDC_SetRxBuffer(&hUsbDeviceHS, Buf);
    USBD_CDC_ReceivePacket(&hUsbDeviceHS);
    return USBD_OK;
}

static int8_t CDC_TransmitCplt_HS(uint8_t *pbuf, uint32_t *Len, uint8_t epnum)
{
    (void)pbuf;
    (void)Len;
    (void)epnum;
    return USBD_OK;
}

/** Public API: send data over USB CDC. Returns USBD_BUSY if TX in progress. */
uint8_t CDC_Transmit_HS(uint8_t *Buf, uint16_t Len)
{
    USBD_CDC_HandleTypeDef *hcdc =
        (USBD_CDC_HandleTypeDef *)hUsbDeviceHS.pClassData;

    if (hcdc == NULL)
        return USBD_FAIL;
    if (hcdc->TxState != 0)
        return USBD_BUSY;

    USBD_CDC_SetTxBuffer(&hUsbDeviceHS, Buf, Len);
    return USBD_CDC_TransmitPacket(&hUsbDeviceHS);
}
