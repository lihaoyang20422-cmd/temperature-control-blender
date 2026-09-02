#include "App_modbus.h"

#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "App_protocol.h"
#include "Com_debug.h"
#include "app_freemodbus_port.h"
#include "mb.h"

#define APP_MODBUS_SLAVE_ID       0x01U
#define APP_MODBUS_PORT           4U
#define APP_MODBUS_BAUDRATE       115200UL
#define APP_MODBUS_DIAG_PERIOD_MS 5000U

static uint8_t s_modbusReady;
static volatile uint32_t s_readCount;
static volatile uint32_t s_writeOkCount;
static volatile uint32_t s_writeErrorCount;
static volatile uint16_t s_lastRegister;
static volatile uint16_t s_lastValue;

static void App_ModbusTask(void *argument)
{
    (void)argument;
    if (s_modbusReady != 0U)
    {
        (void)eMBEnable();
    }
    for (;;)
    {
        if (s_modbusReady != 0U)
        {
            (void)eMBPoll();
        }
        else
        {
            vTaskDelay(pdMS_TO_TICKS(100U));
        }
    }
}

static void App_ModbusDiagTask(void *argument)
{
    (void)argument;
    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(APP_MODBUS_DIAG_PERIOD_MS));
        debug_printfln("Modbus ready:%u rx:%lu drop:%lu tx:%lu txerr:%lu timer:%lu event:%lu/%lu read:%lu write:%lu/%lu last[%u]=%u",
                       s_modbusReady,
                       App_FreeModbusSerialGetRxCount(),
                       App_FreeModbusSerialGetRxDropCount(),
                       App_FreeModbusSerialGetTxCount(),
                       App_FreeModbusSerialGetTxFailCount(),
                       App_FreeModbusTimerGetExpiredCount(),
                       App_FreeModbusEventGetPostCount(),
                       App_FreeModbusEventGetGetCount(),
                       s_readCount, s_writeOkCount, s_writeErrorCount,
                       s_lastRegister, s_lastValue);
    }
}

uint8_t App_ModbusInit(void)
{
    eMBErrorCode result;

    result = eMBInit(MB_RTU, APP_MODBUS_SLAVE_ID, APP_MODBUS_PORT,
                     APP_MODBUS_BAUDRATE, MB_PAR_NONE, 1U);
    s_modbusReady = (result == MB_ENOERR) ? 1U : 0U;
    return s_modbusReady;
}

uint8_t App_ModbusCreateTask(void)
{
    if (s_modbusReady == 0U)
    {
        return 0U;
    }
    if (xTaskCreate(App_ModbusTask, "Modbus", 512U, NULL,
                    tskIDLE_PRIORITY + 2U, NULL) != pdPASS)
    {
        return 0U;
    }
    if (xTaskCreate(App_ModbusDiagTask, "MbDiag", 256U, NULL,
                    tskIDLE_PRIORITY + 1U, NULL) != pdPASS)
    {
        return 0U;
    }
    return 1U;
}

void App_ModbusRxCpltCallback(UART_HandleTypeDef *huart)
{
    App_FreeModbusSerialRxCpltCallback(huart);
}

/* FreeModbus地址从1开始，转换为统一寄存器表的0-based编号。 */
eMBErrorCode eMBRegHoldingCB(UCHAR *buffer, USHORT address,
                             USHORT registers, eMBRegisterMode mode)
{
    uint16_t reg;
    uint16_t value;
    uint16_t index;
    UCHAR *writeBuffer;
    uint8_t needsCommit = 0U;

    if ((buffer == NULL) || (registers == 0U) || (address == 0U))
    {
        return MB_EINVAL;
    }
    reg = (uint16_t)(address - 1U);
    if (((uint32_t)reg + registers - 1U) > APP_REG_MAX)
    {
        return MB_ENOREG;
    }

    if (mode == MB_REG_READ)
    {
        s_readCount++;
        for (index = 0U; index < registers; index++)
        {
            if (App_ProtocolReadRegister((uint16_t)(reg + index), &value) != APP_PROTOCOL_OK)
            {
                return MB_ENOREG;
            }
            *buffer++ = (UCHAR)(value >> 8);
            *buffer++ = (UCHAR)value;
        }
        return MB_ENOERR;
    }

    /* 先验证整批写入，避免批量写中途发现非法寄存器而留下半更新状态。 */
    writeBuffer = buffer;
    for (index = 0U; index < registers; index++)
    {
        value = (uint16_t)(((uint16_t)writeBuffer[0] << 8) | writeBuffer[1]);
        writeBuffer += 2;
        if (App_ProtocolValidateWrite((uint16_t)(reg + index), value) != APP_PROTOCOL_OK)
        {
            s_writeErrorCount++;
            return MB_EINVAL;
        }
    }

    for (index = 0U; index < registers; index++)
    {
        value = (uint16_t)(((uint16_t)buffer[0] << 8) | buffer[1]);
        buffer += 2;
        if (App_ProtocolWriteRegisterDeferred((uint16_t)(reg + index), value,
                                               &needsCommit) != APP_PROTOCOL_OK)
        {
            s_writeErrorCount++;
            return MB_EINVAL;
        }
        s_writeOkCount++;
        s_lastRegister = (uint16_t)(reg + index);
        s_lastValue = value;
    }
    if (App_ProtocolCommitDeferred(needsCommit) == 0U)
    {
        s_writeErrorCount++;
        return MB_EIO;
    }
    return MB_ENOERR;
}

/* 当前版本只开放保持寄存器，其他Modbus对象返回“不支持”。 */
eMBErrorCode eMBRegInputCB(UCHAR *buffer, USHORT address, USHORT registers)
{
    (void)buffer; (void)address; (void)registers;
    return MB_ENOREG;
}

eMBErrorCode eMBRegCoilsCB(UCHAR *buffer, USHORT address,
                           USHORT coils, eMBRegisterMode mode)
{
    (void)buffer; (void)address; (void)coils; (void)mode;
    return MB_ENOREG;
}

eMBErrorCode eMBRegDiscreteCB(UCHAR *buffer, USHORT address, USHORT inputs)
{
    (void)buffer; (void)address; (void)inputs;
    return MB_ENOREG;
}

eMBErrorCode eMBRegFileCB(UCHAR *buffer, USHORT file, USHORT record,
                          USHORT length, eMBRegisterMode mode)
{
    (void)buffer; (void)file; (void)record; (void)length; (void)mode;
    return MB_ENOREG;
}
