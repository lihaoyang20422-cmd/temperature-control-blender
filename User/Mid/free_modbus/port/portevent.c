/*
 * FreeModbus 事件队列适配。
 * 协议栈事件统一通过 FreeRTOS 公开队列 API 传递，并区分任务/中断版本。
 */
#include "mb.h"
#include "mbport.h"
#include "app_freemodbus_port.h"
#include "stm32f1xx_hal.h"
#include "FreeRTOS.h"
#include "queue.h"

static QueueHandle_t s_mbEventQueue = NULL;
static volatile ULONG s_eventPostCount = 0U;
static volatile ULONG s_eventGetCount = 0U;

BOOL xMBPortEventInit(void)
{
    if (s_mbEventQueue == NULL)
    {
        s_mbEventQueue = xQueueCreate(8U, sizeof(eMBEventType));
    }

    return (s_mbEventQueue != NULL) ? TRUE : FALSE;
}

BOOL xMBPortEventPost(eMBEventType eEvent)
{
    BaseType_t needYield = pdFALSE;
    BaseType_t result;

    if (s_mbEventQueue == NULL)
    {
        return FALSE;
    }

    if (__get_IPSR() != 0U)
    {
        result = xQueueSendFromISR(s_mbEventQueue, &eEvent, &needYield);
        if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
        {
            portYIELD_FROM_ISR(needYield);
        }
    }
    else
    {
        result = xQueueSend(s_mbEventQueue, &eEvent, 0U);
    }

    if (result == pdPASS)
    {
        s_eventPostCount++;
    }

    return (result == pdPASS) ? TRUE : FALSE;
}

BOOL xMBPortEventGet(eMBEventType *eEvent)
{
    if ((s_mbEventQueue == NULL) || (eEvent == NULL))
    {
        return FALSE;
    }

    if (xQueueReceive(s_mbEventQueue, eEvent, portMAX_DELAY) == pdPASS)
    {
        s_eventGetCount++;
        return TRUE;
    }

    return FALSE;
}

ULONG App_FreeModbusEventGetPostCount(void)
{
    return s_eventPostCount;
}

ULONG App_FreeModbusEventGetGetCount(void)
{
    return s_eventGetCount;
}
