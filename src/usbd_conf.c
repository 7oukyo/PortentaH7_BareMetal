/**
 * @file usbd_conf.c
 * @brief USB Device board support — PCD MSP init, LL driver interface callbacks.
 *
 * Configures USB_OTG_HS with external USB3320C ULPI PHY.
 * ULPI pins (12 total): PA3/PA5 (D0,CLK), PB0/1/5/10/11/12/13 (D1-D7),
 * PC0 (STP), PH4 (NXT), PI11 (DIR). All AF10.
 *
 * USB clock: PLL3Q = 48 MHz (HSE 25 MHz / M=25 * N=192 / Q=4).
 */

#include "stm32h7xx_hal.h"
#include "usbd_def.h"
#include "usbd_core.h"
#include "usbd_cdc.h"

PCD_HandleTypeDef hpcd_USB_OTG_HS;

void Error_Handler(void);

/* ---- PCD MSP (GPIO, clocks, NVIC) ---- */

void HAL_PCD_MspInit(PCD_HandleTypeDef *pcdHandle)
{
    if (pcdHandle->Instance != USB_OTG_HS)
        return;

    GPIO_InitTypeDef gpio = {0};
    RCC_PeriphCLKInitTypeDef clk = {0};

    /* PLL3: HSE 25 MHz / 25 * 192 = 192 MHz VCO, Q=4 -> 48 MHz USB clock */
    clk.PeriphClockSelection = RCC_PERIPHCLK_USB;
    clk.PLL3.PLL3M      = 25;
    clk.PLL3.PLL3N      = 192;
    clk.PLL3.PLL3P      = 2;
    clk.PLL3.PLL3Q      = 4;
    clk.PLL3.PLL3R      = 2;
    clk.PLL3.PLL3RGE    = RCC_PLL3VCIRANGE_0;
    clk.PLL3.PLL3VCOSEL = RCC_PLL3VCOWIDE;
    clk.PLL3.PLL3FRACN  = 0;
    clk.UsbClockSelection = RCC_USBCLKSOURCE_PLL3;
    if (HAL_RCCEx_PeriphCLKConfig(&clk) != HAL_OK)
        Error_Handler();

    HAL_PWREx_EnableUSBVoltageDetector();

    /* Enable GPIO clocks for ULPI pins */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();
    __HAL_RCC_GPIOI_CLK_ENABLE();

    /* GPIOB: D1(PB0), D2(PB1), D7(PB5), D3(PB10), D4(PB11), D5(PB12), D6(PB13) */
    gpio.Pin   = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_5 |
                 GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13;
    gpio.Mode  = GPIO_MODE_AF_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = GPIO_AF10_OTG2_HS;
    HAL_GPIO_Init(GPIOB, &gpio);

    /* GPIOA: D0(PA3), CLK(PA5) */
    gpio.Pin = GPIO_PIN_3 | GPIO_PIN_5;
    HAL_GPIO_Init(GPIOA, &gpio);

    /* GPIOC: STP(PC0) */
    gpio.Pin = GPIO_PIN_0;
    HAL_GPIO_Init(GPIOC, &gpio);

    /* GPIOH: NXT(PH4) */
    gpio.Pin = GPIO_PIN_4;
    HAL_GPIO_Init(GPIOH, &gpio);

    /* GPIOI: DIR(PI11) */
    gpio.Pin = GPIO_PIN_11;
    HAL_GPIO_Init(GPIOI, &gpio);

    /* Peripheral clocks */
    __HAL_RCC_USB_OTG_HS_CLK_ENABLE();
    __HAL_RCC_USB_OTG_HS_ULPI_CLK_ENABLE();

    /* Interrupt — lowest priority (same level as SysTick) */
    HAL_NVIC_SetPriority(OTG_HS_IRQn, 15, 0);
    HAL_NVIC_EnableIRQ(OTG_HS_IRQn);
}

void HAL_PCD_MspDeInit(PCD_HandleTypeDef *pcdHandle)
{
    if (pcdHandle->Instance != USB_OTG_HS)
        return;

    __HAL_RCC_USB_OTG_HS_CLK_DISABLE();
    __HAL_RCC_USB_OTG_HS_ULPI_CLK_DISABLE();

    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_5 |
                    GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13);
    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_3 | GPIO_PIN_5);
    HAL_GPIO_DeInit(GPIOC, GPIO_PIN_0);
    HAL_GPIO_DeInit(GPIOH, GPIO_PIN_4);
    HAL_GPIO_DeInit(GPIOI, GPIO_PIN_11);

    HAL_NVIC_DisableIRQ(OTG_HS_IRQn);
}

/* ---- PCD callbacks -> USB Device Library ---- */

void HAL_PCD_SetupStageCallback(PCD_HandleTypeDef *hpcd)
{
    USBD_LL_SetupStage((USBD_HandleTypeDef *)hpcd->pData, (uint8_t *)hpcd->Setup);
}

void HAL_PCD_DataOutStageCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
{
    USBD_LL_DataOutStage((USBD_HandleTypeDef *)hpcd->pData, epnum,
                         hpcd->OUT_ep[epnum].xfer_buff);
}

void HAL_PCD_DataInStageCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
{
    USBD_LL_DataInStage((USBD_HandleTypeDef *)hpcd->pData, epnum,
                        hpcd->IN_ep[epnum].xfer_buff);
}

void HAL_PCD_SOFCallback(PCD_HandleTypeDef *hpcd)
{
    USBD_LL_SOF((USBD_HandleTypeDef *)hpcd->pData);
}

void HAL_PCD_ResetCallback(PCD_HandleTypeDef *hpcd)
{
    USBD_SpeedTypeDef speed = USBD_SPEED_FULL;
    if (hpcd->Init.speed == PCD_SPEED_HIGH)
        speed = USBD_SPEED_HIGH;

    USBD_LL_SetSpeed((USBD_HandleTypeDef *)hpcd->pData, speed);
    USBD_LL_Reset((USBD_HandleTypeDef *)hpcd->pData);
}

void HAL_PCD_SuspendCallback(PCD_HandleTypeDef *hpcd)
{
    USBD_LL_Suspend((USBD_HandleTypeDef *)hpcd->pData);
    __HAL_PCD_GATE_PHYCLOCK(hpcd);
}

void HAL_PCD_ResumeCallback(PCD_HandleTypeDef *hpcd)
{
    USBD_LL_Resume((USBD_HandleTypeDef *)hpcd->pData);
}

void HAL_PCD_ISOOUTIncompleteCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
{
    USBD_LL_IsoOUTIncomplete((USBD_HandleTypeDef *)hpcd->pData, epnum);
}

void HAL_PCD_ISOINIncompleteCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
{
    USBD_LL_IsoINIncomplete((USBD_HandleTypeDef *)hpcd->pData, epnum);
}

void HAL_PCD_ConnectCallback(PCD_HandleTypeDef *hpcd)
{
    USBD_LL_DevConnected((USBD_HandleTypeDef *)hpcd->pData);
}

void HAL_PCD_DisconnectCallback(PCD_HandleTypeDef *hpcd)
{
    USBD_LL_DevDisconnected((USBD_HandleTypeDef *)hpcd->pData);
}

/* ---- LL Driver Interface (USB Device Library -> PCD HAL) ---- */

USBD_StatusTypeDef USBD_LL_Init(USBD_HandleTypeDef *pdev)
{
    if (pdev->id != DEVICE_HS)
        return USBD_OK;

    /* Cross-link PCD handle and USB device handle */
    hpcd_USB_OTG_HS.pData = pdev;
    pdev->pData = &hpcd_USB_OTG_HS;

    hpcd_USB_OTG_HS.Instance = USB_OTG_HS;
    hpcd_USB_OTG_HS.Init.dev_endpoints       = 9;
    hpcd_USB_OTG_HS.Init.speed               = PCD_SPEED_FULL;
    hpcd_USB_OTG_HS.Init.dma_enable          = DISABLE;
    hpcd_USB_OTG_HS.Init.phy_itface          = USB_OTG_ULPI_PHY;
    hpcd_USB_OTG_HS.Init.Sof_enable          = DISABLE;
    hpcd_USB_OTG_HS.Init.low_power_enable    = DISABLE;
    hpcd_USB_OTG_HS.Init.lpm_enable          = DISABLE;
    hpcd_USB_OTG_HS.Init.vbus_sensing_enable = DISABLE;
    hpcd_USB_OTG_HS.Init.use_dedicated_ep1   = DISABLE;
    hpcd_USB_OTG_HS.Init.use_external_vbus   = DISABLE;

    if (HAL_PCD_Init(&hpcd_USB_OTG_HS) != HAL_OK)
        Error_Handler();

    /* FIFO allocation (in 32-bit words) */
    HAL_PCDEx_SetRxFiFo(&hpcd_USB_OTG_HS, 0x200);    /* 512 words shared RX */
    HAL_PCDEx_SetTxFiFo(&hpcd_USB_OTG_HS, 0, 0x80);  /* EP0 TX: 128 words */
    HAL_PCDEx_SetTxFiFo(&hpcd_USB_OTG_HS, 1, 0x174); /* EP1 TX: 372 words */

    return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_DeInit(USBD_HandleTypeDef *pdev)
{
    HAL_PCD_DeInit(pdev->pData);
    return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_Start(USBD_HandleTypeDef *pdev)
{
    return (HAL_PCD_Start(pdev->pData) == HAL_OK) ? USBD_OK : USBD_FAIL;
}

USBD_StatusTypeDef USBD_LL_Stop(USBD_HandleTypeDef *pdev)
{
    return (HAL_PCD_Stop(pdev->pData) == HAL_OK) ? USBD_OK : USBD_FAIL;
}

USBD_StatusTypeDef USBD_LL_OpenEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr,
                                   uint8_t ep_type, uint16_t ep_mps)
{
    return (HAL_PCD_EP_Open(pdev->pData, ep_addr, ep_mps, ep_type) == HAL_OK)
           ? USBD_OK : USBD_FAIL;
}

USBD_StatusTypeDef USBD_LL_CloseEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
    return (HAL_PCD_EP_Close(pdev->pData, ep_addr) == HAL_OK)
           ? USBD_OK : USBD_FAIL;
}

USBD_StatusTypeDef USBD_LL_FlushEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
    return (HAL_PCD_EP_Flush(pdev->pData, ep_addr) == HAL_OK)
           ? USBD_OK : USBD_FAIL;
}

USBD_StatusTypeDef USBD_LL_StallEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
    return (HAL_PCD_EP_SetStall(pdev->pData, ep_addr) == HAL_OK)
           ? USBD_OK : USBD_FAIL;
}

USBD_StatusTypeDef USBD_LL_ClearStallEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
    return (HAL_PCD_EP_ClrStall(pdev->pData, ep_addr) == HAL_OK)
           ? USBD_OK : USBD_FAIL;
}

uint8_t USBD_LL_IsStallEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
    PCD_HandleTypeDef *hpcd = (PCD_HandleTypeDef *)pdev->pData;
    if ((ep_addr & 0x80) == 0x80)
        return hpcd->IN_ep[ep_addr & 0x7F].is_stall;
    else
        return hpcd->OUT_ep[ep_addr & 0x7F].is_stall;
}

USBD_StatusTypeDef USBD_LL_SetUSBAddress(USBD_HandleTypeDef *pdev, uint8_t dev_addr)
{
    return (HAL_PCD_SetAddress(pdev->pData, dev_addr) == HAL_OK)
           ? USBD_OK : USBD_FAIL;
}

USBD_StatusTypeDef USBD_LL_Transmit(USBD_HandleTypeDef *pdev, uint8_t ep_addr,
                                     uint8_t *pbuf, uint32_t size)
{
    return (HAL_PCD_EP_Transmit(pdev->pData, ep_addr, pbuf, size) == HAL_OK)
           ? USBD_OK : USBD_FAIL;
}

USBD_StatusTypeDef USBD_LL_PrepareReceive(USBD_HandleTypeDef *pdev, uint8_t ep_addr,
                                           uint8_t *pbuf, uint32_t size)
{
    return (HAL_PCD_EP_Receive(pdev->pData, ep_addr, pbuf, size) == HAL_OK)
           ? USBD_OK : USBD_FAIL;
}

uint32_t USBD_LL_GetRxDataSize(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
    return HAL_PCD_EP_GetRxCount((PCD_HandleTypeDef *)pdev->pData, ep_addr);
}

/* Static memory allocation for CDC class handle (no heap) */
void *USBD_static_malloc(uint32_t size)
{
    (void)size;
    static uint32_t mem[(sizeof(USBD_CDC_HandleTypeDef) / 4) + 1];
    return mem;
}

void USBD_static_free(void *p)
{
    (void)p;
}

void USBD_LL_Delay(uint32_t Delay)
{
    HAL_Delay(Delay);
}
