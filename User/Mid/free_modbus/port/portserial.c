/*
 * FreeModbus UART4 端口适配。
 * 中断回调只保存一个字节并驱动协议栈接收状态机；发送沿用协议栈逐字节回调。
 */
#include "mb.h"
#include "mbport.h"
#include "app_freemodbus_port.h"
#include "usart.h"

#define MB_SERIAL_TX_TIMEOUT_MS     20U

static UCHAR s_rxByte = 0U;
static UCHAR s_lastRxByte = 0U;
static volatile BOOL s_txEnabled = FALSE;
static volatile BOOL s_rxEnabled = FALSE;
static volatile ULONG s_rxIrqCount = 0U;
static volatile ULONG s_rxDropCount = 0U;
static volatile ULONG s_txCount = 0U;
static volatile ULONG s_txFailCount = 0U;
static volatile UCHAR s_lastTxByte = 0U;

BOOL xMBPortSerialInit(UCHAR ucPort, ULONG ulBaudRate, UCHAR ucDataBits,
                       eMBParity eParity, UCHAR ucStopBits)
{
    (void)ucPort;
    (void)ulBaudRate;
    (void)ucDataBits;
    (void)eParity;
    (void)ucStopBits;

    return TRUE;
}

void vMBPortClose(void)
{
    xMBPortSerialClose();
    xMBPortTimersClose();
}

void xMBPortSerialClose(void)
{
    s_rxEnabled = FALSE;
    s_txEnabled = FALSE;
    (void)HAL_UART_AbortReceive_IT(&huart4);
}

void vMBPortSerialEnable(BOOL xRxEnable, BOOL xTxEnable)
{
    s_rxEnabled = xRxEnable;
    s_txEnabled = xTxEnable;

    if (xRxEnable != FALSE)
    {
        (void)HAL_UART_Receive_IT(&huart4, &s_rxByte, 1U);
    }
    else
    {
        (void)HAL_UART_AbortReceive_IT(&huart4);
    }

    if (xTxEnable != FALSE)
    {
        while (s_txEnabled != FALSE)
        {
            if (pxMBFrameCBTransmitterEmpty == NULL)
            {
                break;
            }
            (void)pxMBFrameCBTransmitterEmpty();
        }
    }
}

BOOL xMBPortSerialGetByte(CHAR *pucByte)
{
    if (pucByte == NULL)
    {
        return FALSE;
    }

    *pucByte = (CHAR)s_lastRxByte;
    return TRUE;
}

BOOL xMBPortSerialPutByte(CHAR ucByte)
{
    UCHAR txByte = (UCHAR)ucByte;
    HAL_StatusTypeDef status;

    status = HAL_UART_Transmit(&huart4, &txByte, 1U, MB_SERIAL_TX_TIMEOUT_MS);
    if (status == HAL_OK)
    {
        s_txCount++;
        s_lastTxByte = txByte;
        return TRUE;
    }

    s_txFailCount++;
    return FALSE;
}

void App_FreeModbusSerialRxCpltCallback(UART_HandleTypeDef *huart)
{
    if ((huart == NULL) || (huart->Instance != UART4))
    {
        return;
    }

    s_lastRxByte = s_rxByte;
    if ((s_rxEnabled != FALSE) && (pxMBFrameCBByteReceived != NULL))
    {
        s_rxIrqCount++;
        (void)pxMBFrameCBByteReceived();
        (void)HAL_UART_Receive_IT(&huart4, &s_rxByte, 1U);
    }
    else
    {
        s_rxDropCount++;
    }
}

ULONG App_FreeModbusSerialGetRxCount(void)
{
    return s_rxIrqCount;
}

ULONG App_FreeModbusSerialGetRxDropCount(void)
{
    return s_rxDropCount;
}

ULONG App_FreeModbusSerialGetTxCount(void)
{
    return s_txCount;
}

ULONG App_FreeModbusSerialGetTxFailCount(void)
{
    return s_txFailCount;
}

UCHAR App_FreeModbusSerialGetLastTxByte(void)
{
    return s_lastTxByte;
}
