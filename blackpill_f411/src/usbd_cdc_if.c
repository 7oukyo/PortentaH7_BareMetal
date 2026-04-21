/**
 * @file usbd_cdc_if.c
 * @brief CDC interface callbacks for USB Virtual COM Port (Full Speed).
 *
 * BlackPill F411 port: uses USB OTG FS (internal PHY, PA11/PA12).
 * All HS references changed to FS.
 */

#include "usbd_cdc_if.h"
#include "led.h"
#include "main.h"

static uint8_t UserRxBufferFS[APP_RX_DATA_SIZE];
static uint8_t UserTxBufferFS[APP_TX_DATA_SIZE];

static USBD_CDC_LineCodingTypeDef LineCoding = {
    .bitrate    = 115200,
    .format     = 0x00,
    .paritytype = 0x00,
    .datatype   = 0x08,
};

extern USBD_HandleTypeDef hUsbDeviceFS;

static int8_t CDC_Init_FS(void);
static int8_t CDC_DeInit_FS(void);
static int8_t CDC_Control_FS(uint8_t cmd, uint8_t *pbuf, uint16_t length);
static int8_t CDC_Receive_FS(uint8_t *pbuf, uint32_t *Len);
static int8_t CDC_TransmitCplt_FS(uint8_t *pbuf, uint32_t *Len, uint8_t epnum);

USBD_CDC_ItfTypeDef USBD_Interface_fops_FS = {
    CDC_Init_FS,
    CDC_DeInit_FS,
    CDC_Control_FS,
    CDC_Receive_FS,
    CDC_TransmitCplt_FS,
};

static int8_t CDC_Init_FS(void)
{
    USBD_CDC_SetTxBuffer(&hUsbDeviceFS, UserTxBufferFS, 0);
    USBD_CDC_SetRxBuffer(&hUsbDeviceFS, UserRxBufferFS);
    return USBD_OK;
}

static int8_t CDC_DeInit_FS(void) { return USBD_OK; }

static int8_t CDC_Control_FS(uint8_t cmd, uint8_t *pbuf, uint16_t length)
{
    (void)length;
    switch (cmd) {
    case CDC_SET_LINE_CODING:
        memcpy(&LineCoding, pbuf, sizeof(LineCoding));
        break;
    case CDC_GET_LINE_CODING:
        memcpy(pbuf, &LineCoding, sizeof(LineCoding));
        break;
    default:
        break;
    }
    return USBD_OK;
}

/** Receive from USB host: forward to command dispatcher, blink LED. */
static int8_t CDC_Receive_FS(uint8_t *Buf, uint32_t *Len)
{
    Led_BlinkOnRx();
    HandleSerialCmd((const char *)Buf, (uint16_t)*Len);

    USBD_CDC_SetRxBuffer(&hUsbDeviceFS, Buf);
    USBD_CDC_ReceivePacket(&hUsbDeviceFS);
    return USBD_OK;
}

static int8_t CDC_TransmitCplt_FS(uint8_t *pbuf, uint32_t *Len, uint8_t epnum)
{
    (void)pbuf; (void)Len; (void)epnum;
    return USBD_OK;
}

/** Public API: send data over USB CDC. Returns USBD_BUSY if TX in progress. */
uint8_t CDC_Transmit_FS(uint8_t *Buf, uint16_t Len)
{
    USBD_CDC_HandleTypeDef *hcdc =
        (USBD_CDC_HandleTypeDef *)hUsbDeviceFS.pClassData;

    if (hcdc == NULL) return USBD_FAIL;
    if (hcdc->TxState != 0) return USBD_BUSY;

    USBD_CDC_SetTxBuffer(&hUsbDeviceFS, Buf, Len);
    return USBD_CDC_TransmitPacket(&hUsbDeviceFS);
}
