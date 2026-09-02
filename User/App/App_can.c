#include "App_can.h"

#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "can.h"
#include "App_protocol.h"
#include "Com_debug.h"

/* 自定义CAN寄存器协议：命令ID和应答ID保持固定，便于网关转发。 */
#define APP_CAN_COMMAND_ID       0x321U
#define APP_CAN_RESPONSE_ID      0x322U
#define APP_CAN_NODE_ID          0x01U
#define APP_CAN_RX_MIN_DLC       7U
#define APP_CAN_TX_WAIT_MS       5U
#define APP_CAN_DIAG_PERIOD_MS   5000U

#define APP_CAN_FUNCTION_READ    0x03U
#define APP_CAN_FUNCTION_WRITE   0x06U

#define APP_CAN_STATUS_OK        0x00U
#define APP_CAN_STATUS_BAD_NODE  0x01U
#define APP_CAN_STATUS_BAD_FUNC  0x02U
#define APP_CAN_STATUS_BAD_REG   0x03U
#define APP_CAN_STATUS_BAD_VALUE 0x04U
#define APP_CAN_STATUS_TX_FAIL   0x05U

static uint8_t s_canReady;
static TaskHandle_t s_canTaskHandle;
static volatile uint32_t s_canIrqCount;
static volatile uint32_t s_canRxCount;
static volatile uint32_t s_canTxCount;
static volatile uint32_t s_canTxFailCount;
static volatile uint32_t s_canBadCount;
static volatile uint8_t s_canInitError;
static volatile uint32_t s_canLastHalError;

static uint8_t App_CanMapProtocolStatus(AppProtocolStatus_t status)
{
    if (status == APP_PROTOCOL_OK) return APP_CAN_STATUS_OK;
    if (status == APP_PROTOCOL_BAD_REG) return APP_CAN_STATUS_BAD_REG;
    return APP_CAN_STATUS_BAD_VALUE;
}

static uint8_t App_CanSendResponse(uint8_t function, uint16_t reg,
                                   uint16_t value, uint8_t sequence, uint8_t status)
{
    CAN_TxHeaderTypeDef header;
    uint8_t data[8];
    uint32_t mailbox;
    TickType_t start;

    memset(&header, 0, sizeof(header));
    header.StdId = APP_CAN_RESPONSE_ID;
    header.IDE = CAN_ID_STD;
    header.RTR = CAN_RTR_DATA;
    header.DLC = 8U;
    header.TransmitGlobalTime = DISABLE;

    data[0] = APP_CAN_NODE_ID;
    data[1] = function;
    data[2] = (uint8_t)(reg >> 8);
    data[3] = (uint8_t)reg;
    data[4] = (uint8_t)(value >> 8);
    data[5] = (uint8_t)value;
    data[6] = sequence;
    data[7] = status;

    start = xTaskGetTickCount();
    while (HAL_CAN_GetTxMailboxesFreeLevel(&hcan1) == 0U)
    {
        if ((xTaskGetTickCount() - start) >= pdMS_TO_TICKS(APP_CAN_TX_WAIT_MS))
        {
            s_canTxFailCount++;
            return 0U;
        }
        vTaskDelay(pdMS_TO_TICKS(1U));
    }

    if (HAL_CAN_AddTxMessage(&hcan1, &header, data, &mailbox) != HAL_OK)
    {
        s_canTxFailCount++;
        return 0U;
    }
    s_canTxCount++;
    return 1U;
}

static void App_CanHandleFrame(const CAN_RxHeaderTypeDef *header, const uint8_t *data)
{
    uint8_t function;
    uint8_t sequence;
    uint8_t status;
    uint16_t reg;
    uint16_t value;

    if ((header == NULL) || (data == NULL) ||
        (header->IDE != CAN_ID_STD) || (header->StdId != APP_CAN_COMMAND_ID) ||
        (header->RTR != CAN_RTR_DATA) || (header->DLC < APP_CAN_RX_MIN_DLC))
    {
        s_canBadCount++;
        return;
    }

    s_canRxCount++;
    if (data[0] != APP_CAN_NODE_ID)
    {
        status = APP_CAN_STATUS_BAD_NODE;
        function = data[1];
        reg = 0U;
        value = 0U;
    }
    else
    {
        function = data[1];
        reg = (uint16_t)(((uint16_t)data[2] << 8) | data[3]);
        value = (uint16_t)(((uint16_t)data[4] << 8) | data[5]);
        if (function == APP_CAN_FUNCTION_READ)
        {
            status = App_CanMapProtocolStatus(App_ProtocolReadRegister(reg, &value));
            if (status != APP_CAN_STATUS_OK) value = 0U;
        }
        else if (function == APP_CAN_FUNCTION_WRITE)
        {
            status = App_CanMapProtocolStatus(App_ProtocolWriteRegister(reg, value));
        }
        else
        {
            status = APP_CAN_STATUS_BAD_FUNC;
        }
    }

    sequence = data[6];
    if (App_CanSendResponse(function, reg, value, sequence, status) == 0U)
    {
        /* 发送失败只记录诊断，不在此处触发业务故障。 */
    }
}

static void App_CanDrainFifo(void)
{
    CAN_RxHeaderTypeDef header;
    uint8_t data[8];

    while (HAL_CAN_GetRxFifoFillLevel(&hcan1, CAN_RX_FIFO0) > 0U)
    {
        if (HAL_CAN_GetRxMessage(&hcan1, CAN_RX_FIFO0, &header, data) == HAL_OK)
        {
            App_CanHandleFrame(&header, data);
        }
        else
        {
            s_canBadCount++;
        }
    }
}

static void App_CanTask(void *argument)
{
    TickType_t lastDiag;

    (void)argument;
    lastDiag = xTaskGetTickCount();
    for (;;)
    {
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(APP_CAN_DIAG_PERIOD_MS));
        if (s_canReady != 0U)
        {
            App_CanDrainFifo();
            (void)HAL_CAN_ActivateNotification(&hcan1, CAN_IT_RX_FIFO0_MSG_PENDING);
        }
        if ((xTaskGetTickCount() - lastDiag) >= pdMS_TO_TICKS(APP_CAN_DIAG_PERIOD_MS))
        {
            lastDiag = xTaskGetTickCount();
            s_canLastHalError = HAL_CAN_GetError(&hcan1);
            debug_printfln("CAN ready:%u irq:%lu rx:%lu tx:%lu txerr:%lu bad:%lu hal:%08lX",
                           s_canReady, s_canIrqCount, s_canRxCount, s_canTxCount,
                           s_canTxFailCount, s_canBadCount, s_canLastHalError);
        }
    }
}

uint8_t App_CanInit(void)
{
    CAN_FilterTypeDef filter;

    memset(&filter, 0, sizeof(filter));
    filter.FilterBank = 0U;
    filter.FilterMode = CAN_FILTERMODE_IDMASK;
    filter.FilterScale = CAN_FILTERSCALE_32BIT;
    filter.FilterIdHigh = (uint16_t)(APP_CAN_COMMAND_ID << 5);
    filter.FilterMaskIdHigh = (uint16_t)(0x7FFU << 5);
    filter.FilterFIFOAssignment = CAN_RX_FIFO0;
    filter.FilterActivation = ENABLE;
    filter.SlaveStartFilterBank = 14U;

    if (HAL_CAN_ConfigFilter(&hcan1, &filter) != HAL_OK)
    {
        s_canInitError = 1U;
        s_canLastHalError = HAL_CAN_GetError(&hcan1);
        return 0U;
    }
    if (HAL_CAN_Start(&hcan1) != HAL_OK)
    {
        s_canInitError = 2U;
        s_canLastHalError = HAL_CAN_GetError(&hcan1);
        return 0U;
    }
    if (HAL_CAN_ActivateNotification(&hcan1, CAN_IT_RX_FIFO0_MSG_PENDING) != HAL_OK)
    {
        s_canInitError = 3U;
        s_canLastHalError = HAL_CAN_GetError(&hcan1);
        return 0U;
    }
    s_canReady = 1U;
    s_canInitError = 0U;
    return 1U;
}

uint8_t App_CanCreateTask(void)
{
    if (xTaskCreate(App_CanTask, "CAN", 384U, NULL,
                    tskIDLE_PRIORITY + 2U, &s_canTaskHandle) != pdPASS)
    {
        s_canInitError = 4U;
        return 0U;
    }
    return 1U;
}

/* CAN中断只唤醒任务，报文解析和应答均在任务上下文完成。 */
void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
    BaseType_t needYield = pdFALSE;

    if ((hcan == NULL) || (hcan->Instance != CAN1)) return;
    s_canIrqCount++;
    (void)HAL_CAN_DeactivateNotification(hcan, CAN_IT_RX_FIFO0_MSG_PENDING);
    if (s_canTaskHandle != NULL)
    {
        vTaskNotifyGiveFromISR(s_canTaskHandle, &needYield);
        if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
        {
            portYIELD_FROM_ISR(needYield);
        }
    }
}
