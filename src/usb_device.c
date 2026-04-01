/**
 * @file usb_device.c
 * @brief USB Device initialization — registers CDC class and starts USB stack.
 *
 * Call MX_USB_DEVICE_Init() after PMIC_Init() (SW1 + LDO2 must be up for
 * USB3320 power) and after PJ4 reset toggle (USB3320 PHY reset).
 */

#include "usb_device.h"
#include "usbd_core.h"
#include "usbd_desc.h"
#include "usbd_cdc.h"
#include "usbd_cdc_if.h"

USBD_HandleTypeDef hUsbDeviceHS;

void MX_USB_DEVICE_Init(void)
{
    if (USBD_Init(&hUsbDeviceHS, &HS_Desc, DEVICE_HS) != USBD_OK)
        Error_Handler();

    if (USBD_RegisterClass(&hUsbDeviceHS, &USBD_CDC) != USBD_OK)
        Error_Handler();

    if (USBD_CDC_RegisterInterface(&hUsbDeviceHS, &USBD_Interface_fops_HS) != USBD_OK)
        Error_Handler();

    if (USBD_Start(&hUsbDeviceHS) != USBD_OK)
        Error_Handler();
}
