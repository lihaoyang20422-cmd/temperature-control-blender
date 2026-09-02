/*
 * FreeModbus RTU 帧间隔定时器适配。
 * 使用 FreeRTOS 公开的软件定时器 API 实现单次 t3.5 超时，并区分任务/中断上下文。
 */
#include "mb.h"
#include "mbport.h"
#include "app_freemodbus_port.h"
#include "stm32f1xx_hal.h"
#include "FreeRTOS.h"
#include "timers.h"
#include "task.h"

static TimerHandle_t s_mbTimer = NULL;
static USHORT s_timeout50us = 0U;
static volatile ULONG s_timerExpiredCount = 0U;

static void App_FreeModbusTimerCallback(TimerHandle_t timer)
{
    (void)timer;

    if (pxMBPortCBTimerExpired != NULL)
    {
        s_timerExpiredCount++;
        (void)pxMBPortCBTimerExpired();
    }
}

static TickType_t App_FreeModbusTimeoutTicks(USHORT timeout50us)
{
    uint32_t timeoutUs = (uint32_t)timeout50us * 50UL;
    uint32_t timeoutMs = (timeoutUs + 999UL) / 1000UL;

    if (timeoutMs == 0UL)
    {
        timeoutMs = 1UL;
    }

    return pdMS_TO_TICKS(timeoutMs);
}

BOOL xMBPortTimersInit(USHORT usTimeOut50us)
{
    s_timeout50us = usTimeOut50us;

    if (s_mbTimer == NULL)
    {
        s_mbTimer = xTimerCreate("mbt35",
                                 App_FreeModbusTimeoutTicks(s_timeout50us),
                                 pdFALSE,
                                 NULL,
                                 App_FreeModbusTimerCallback);
    }

    return (s_mbTimer != NULL) ? TRUE : FALSE;
}

void xMBPortTimersClose(void)
{
    if (s_mbTimer != NULL)
    {
        (void)xTimerStop(s_mbTimer, 0U);
    }
}

void vMBPortTimersEnable(void)
{
    if (s_mbTimer != NULL)
    {
        if (__get_IPSR() != 0U)
        {
            BaseType_t needYield = pdFALSE;
            (void)xTimerResetFromISR(s_mbTimer, &needYield);
            if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
            {
                portYIELD_FROM_ISR(needYield);
            }
        }
        else
        {
            (void)xTimerReset(s_mbTimer, 0U);
        }
    }
}

void vMBPortTimersDisable(void)
{
    if (s_mbTimer != NULL)
    {
        if (__get_IPSR() != 0U)
        {
            BaseType_t needYield = pdFALSE;
            (void)xTimerStopFromISR(s_mbTimer, &needYield);
            if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
            {
                portYIELD_FROM_ISR(needYield);
            }
        }
        else
        {
            (void)xTimerStop(s_mbTimer, 0U);
        }
    }
}

void vMBPortTimersDelay(USHORT usTimeOutMS)
{
    vTaskDelay(pdMS_TO_TICKS(usTimeOutMS));
}

ULONG App_FreeModbusTimerGetExpiredCount(void)
{
    return s_timerExpiredCount;
}
